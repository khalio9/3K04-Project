// EventLog.cpp - how the log is stored (the secret).
//   File format: one line per entry:  seq|time|adc|type|user|outcome|details|hash
//   Hash chain:  hash = FNV-1a(previous hash + this line). Editing any old line breaks it.
//   Durability:  fflush + fsync before append() returns. A half-written last line is dropped at load.
// Replacing any of these (a database, SHA-256, ...) changes only this file.
#include "EventLog.h"

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <sstream>

#ifdef _WIN32
#include <io.h>
static int syncFile(std::FILE *f) { return _commit(_fileno(f)); }
#else
#include <unistd.h>
static int syncFile(std::FILE *f) { return fsync(fileno(f)); }
#endif

// ---------- helpers (private to this file) ----------
static std::string clean(const std::string &s)
{
    std::string out = s;                     // '|' and newlines would break the file format
    for (char &c : out)
        if (c == '|' || c == '\n' || c == '\r') c = '/';
    return out;
}

static std::string lower(std::string s)
{
    for (char &c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

static std::vector<std::string> split(const std::string &s, char sep)
{
    std::vector<std::string> parts;
    std::string cur;
    for (char c : s) {
        if (c == sep) { parts.push_back(cur); cur.clear(); }
        else cur += c;
    }
    parts.push_back(cur);
    return parts;
}

static std::string hex16(uint64_t v)
{
    std::ostringstream o;
    o << std::hex << std::setw(16) << std::setfill('0') << v;
    return o.str();
}

// ---------- the secret ----------
uint64_t EventLog::chainHash(uint64_t prev, const std::string &line)
{
    uint64_t h = 1469598103934665603ULL;
    for (char c : hex16(prev) + line) {
        h ^= static_cast<unsigned char>(c);
        h *= 1099511628211ULL;
    }
    return h;
}

std::string EventLog::lineFor(const LogEntry &e)
{
    std::ostringstream o;
    o << e.seq << '|' << e.timeMs << '|' << e.adcId << '|' << static_cast<int>(e.type) << '|'
      << e.userId << '|' << static_cast<int>(e.outcome) << '|' << e.details;
    return o.str();
}

// Row L2: a credential secret, or patient data other than patient_id.
bool EventLog::forbidden(const std::string &details)
{
    for (const std::string &pair : split(details, ';')) {
        std::string key = lower(pair.substr(0, pair.find('=')));
        while (!key.empty() && key[0] == ' ') key.erase(0, 1);
        if (key == "pin" || key == "password" || key == "secret" || key == "credential") return true;
        if (key.compare(0, 7, "patient") == 0 && key != "patient_id") return true;
    }
    return false;
}

// SPEC-LOG-3
void EventLog::load()
{
    std::ifstream in(path_, std::ios::binary);
    if (!in) return;                                             // row G1: no file
    std::string all((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();

    // Row G3: a last line with no '\n' never finished, so it was never an entry. Drop it.
    size_t complete = all.rfind('\n');
    complete = (complete == std::string::npos) ? 0 : complete + 1;
    if (complete < all.size()) {
        all.resize(complete);
        std::ofstream out(path_, std::ios::binary | std::ios::trunc);
        out << all;
    }

    std::istringstream lines(all);
    std::string line;
    while (std::getline(lines, line)) {
        std::vector<std::string> f = split(line, '|');
        bool ok = f.size() == 8;
        LogEntry e{};
        if (ok) {
            e.seq     = static_cast<unsigned>(std::strtoul(f[0].c_str(), nullptr, 10));
            e.timeMs  = std::strtoll(f[1].c_str(), nullptr, 10);
            e.adcId   = f[2];
            int t = std::atoi(f[3].c_str()), o = std::atoi(f[5].c_str());
            ok = t >= 0 && t <= static_cast<int>(EventType::COMM_RESTORED) &&
                 o >= 0 && o <= static_cast<int>(Outcome::ABANDONED);
            e.type    = static_cast<EventType>(t);
            e.userId  = f[4];
            e.outcome = static_cast<Outcome>(o);
            e.details = f[6];
        }
        uint64_t h = ok ? chainHash(lastHash_, lineFor(e)) : 0;
        if (!ok || e.seq != lastSeq() + 1 || hex16(h) != f[7]) {
            healthy_ = false;                                    // row G4: gap, bad hash, bad record
            break;
        }
        entries_.push_back(e);                                   // row G2
        lastHash_ = h;
    }
}

EventLog::EventLog(const std::string &adcId, const std::string &path, Clock clock)
    : adcId_(clean(adcId)), path_(path), clock_(clock)
{
    load();
    file_ = std::fopen(path_.c_str(), "ab");
    if (!file_) healthy_ = false;
}

EventLog::~EventLog()
{
    if (file_) std::fclose(file_);
}

// SPEC-LOG-1
LogEntry EventLog::append(EventType type, Outcome outcome,
                          const std::string &userId, const std::string &details)
{
    // Row L1 (enum class already blocks most bad values at compile time)
    if (static_cast<int>(type) < 0 || type > EventType::COMM_RESTORED ||
        static_cast<int>(outcome) < 0 || outcome > Outcome::ABANDONED)
        throw std::invalid_argument("unknown event type or outcome");
    // Row L2
    if (forbidden(details))
        throw std::invalid_argument("details contain a credential secret or patient data");

    LogEntry e;
    e.seq     = lastSeq() + 1;
    e.timeMs  = clock_ ? clock_() : 0;
    if (!entries_.empty() && e.timeMs < entries_.back().timeMs)
        e.timeMs = entries_.back().timeMs;                       // LOG-I2: time never goes backwards
    e.adcId   = adcId_;
    e.type    = type;
    e.userId  = clean(userId);
    e.outcome = outcome;
    e.details = clean(details);

    std::string line = lineFor(e);
    uint64_t h = chainHash(lastHash_, line);
    std::string text = line + '|' + hex16(h) + '\n';

    // Row L3: write, flush and sync before returning (OUT-4)
    bool ok = file_ != nullptr && !diskFails_ &&
              std::fwrite(text.data(), 1, text.size(), file_) == text.size() &&
              std::fflush(file_) == 0 && syncFile(file_) == 0;
    if (!ok) {
        healthy_ = false;
        throw LogStorageError("log write failed");
    }

    // Row L4
    entries_.push_back(e);
    lastHash_ = h;
    return e;
}

std::vector<LogEntry> EventLog::entries(unsigned fromSeq, unsigned toSeq) const
{
    std::vector<LogEntry> out;
    for (const LogEntry &e : entries_)
        if (e.seq >= fromSeq && (toSeq == 0 || e.seq <= toSeq)) out.push_back(e);
    return out;                                                  // copies: callers cannot edit
}

unsigned EventLog::lastSeq() const { return entries_.empty() ? 0 : entries_.back().seq; }
bool     EventLog::isHealthy() const { return healthy_; }