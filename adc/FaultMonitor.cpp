// FaultMonitor.cpp - detection rules and the fault lifecycle (the secret).
//
//  Spec    Fault                  Detected when                                  Severity
//  P2-F01  DRAWER_OPEN_TOO_LONG   released drawer still OPEN >= 10 s after release Warning  (block location)
//  P2-F02  LOCK_FAILURE           command rejected, or feedback != command >= 250 ms Critical
//  P2-F03  SENSOR_FAILURE         drawer or lock reading invalid / stale (5 s)     Warning  (block location)
//  P2-F04  SENSOR_BUS_FAILURE     shared sensor invalid / stale / reports bad      Critical
//  P2-F05  STORAGE_ERROR          log or outbox write fails                        Critical
//  P2-F06  SELF_TEST              a self-test check fails                          Critical
//  P2-F07  COMM_LOSS              CommManager link DEGRADED (3 missed heartbeats)  Warning  (status only)
//  P2-F08  UNEXPECTED_OPEN        drawer OPEN with no release                      Critical (added hazard)
//
//  Lifecycle: each (code, location) is raised once. A Warning clears itself when the sensor
//  shows it has recovered. A Critical stays until a technician runs clearfault and the
//  condition is gone (FR-FLT-06). Any Critical forces Fault mode (FR-MOD-07).
#include "FaultMonitor.h"

#include <sstream>

static std::string num(long long v) { std::ostringstream o; o << v; return o.str(); }

std::string toString(Severity s) { return s == Severity::CRITICAL ? "CRITICAL" : "WARNING"; }

FaultMonitor::FaultMonitor(DrawerSensor &sensor, LockDriver &locks, ModeManager &modes,
                           EventLog &log, CommManager &comm, Clock clock)
    : sensor_(sensor), locks_(locks), modes_(modes), log_(log), comm_(comm), clock_(clock) {}

long long FaultMonitor::now() const { return clock_ ? clock_() : 0; }

// Log one entry. A failed write latches STORAGE_ERROR (P2-F05).
bool FaultMonitor::record(EventType type, Outcome outcome, const std::string &user,
                          const std::string &details)
{
    try {
        log_.append(type, outcome, user, details);
        return true;
    } catch (const LogStorageError &) {
        storageBad_ = true;
    } catch (const std::exception &) {
    }
    return false;
}

// FR-FLT-04: tell the MS. Queued in the outbox even when the link is down.
void FaultMonitor::notifyMs(const Fault &f, const std::string &state)
{
    try {
        comm_.send(MessageType::ALARM, "fault_id=" + num(f.id) + ";code=" + f.code +
                   ";severity=" + toString(f.severity) + ";location=" + f.location + ";state=" + state);
    } catch (const CommStorageError &) {
        storageBad_ = true;
    }
}

// ---------- P2-F01..F08 ----------
std::map<std::string, FaultMonitor::Condition> FaultMonitor::detect()
{
    std::map<std::string, Condition> found;
    auto add = [&](const std::string &code, const std::string &loc, Severity s) {
        found[code + "@" + loc] = Condition{code, loc, s};
    };

    for (const std::string &l : sensor_.locations()) {
        DrawerState d = sensor_.drawer(l);
        LockState   k = sensor_.lock(l);
        if (d == DrawerState::UNKNOWN || k == LockState::UNKNOWN) add("SENSOR_FAILURE", l, Severity::WARNING);  // F03
        if (locks_.mismatch(l))                                   add("LOCK_FAILURE", l, Severity::CRITICAL);  // F02
        if (locks_.openTooLong(l))                                add("DRAWER_OPEN_TOO_LONG", l, Severity::WARNING); // F01
        if (d == DrawerState::OPEN && !locks_.cycleActive(l))     add("UNEXPECTED_OPEN", l, Severity::CRITICAL); // F08
    }
    if (!sensor_.busHealthy()) add("SENSOR_BUS_FAILURE", "", Severity::CRITICAL);                  // F04

    bool healthy = log_.isHealthy() && comm_.isHealthy();                                          // F05
    if (!healthy && lastHealthy_) storageBad_ = true;
    lastHealthy_ = healthy;
    if (storageBad_) add("STORAGE_ERROR", "", Severity::CRITICAL);

    if (selfTestFailed_) add("SELF_TEST", "", Severity::CRITICAL);                                  // F06
    if (comm_.linkStatus() == LinkStatus::DEGRADED) add("COMM_LOSS", "", Severity::WARNING);       // F07
    return found;
}

void FaultMonitor::raise(const std::string &key, const Condition &c)
{
    Fault f{nextId_++, c.code, c.location, c.severity, now(), false};
    active_[key] = f;
    record(EventType::FAULT_RAISED, Outcome::SUCCESS, "",
           "fault_id=" + num(f.id) + ";code=" + f.code + ";severity=" + toString(f.severity) +
           ";location=" + f.location);
    notifyMs(f, "RAISED");
}

void FaultMonitor::clear(const std::string &key, const std::string &user)
{
    Fault f = active_[key];
    active_.erase(key);
    record(EventType::FAULT_CLEARED, Outcome::SUCCESS, user,
           "fault_id=" + num(f.id) + ";code=" + f.code + ";location=" + f.location);
    notifyMs(f, "CLEARED");
}

void FaultMonitor::tick()
{
    locks_.tick();                                       // re-lock closed drawers first

    std::map<std::string, Condition> found = detect();
    for (const auto &kv : found)
        if (!active_.count(kv.first)) raise(kv.first, kv.second);

    std::vector<std::string> gone;                       // Warnings clear when the sensor recovers
    for (const auto &kv : active_)
        if (kv.second.severity == Severity::WARNING && !found.count(kv.first)) gone.push_back(kv.first);
    for (const std::string &k : gone) clear(k, "");

    if (hasCritical()) modes_.enterFault("critical_fault");   // FR-FLT-02 / FR-MOD-07
}

void FaultMonitor::report(const std::string &code, const std::string &location)
{
    (void)location;
    if (code == "STORAGE_ERROR") storageBad_ = true;
    tick();
}

// ---------- self-test ----------
// Locks everything, then checks: every drawer CLOSED, every lock LOCKED and obeying,
// shared sensor healthy, and the log can be written (the result entry is the probe).
bool FaultMonitor::selfTest()
{
    locks_.lockAll();
    std::string failed;
    for (const std::string &l : sensor_.locations()) {
        bool ok = sensor_.drawer(l) == DrawerState::CLOSED && sensor_.lock(l) == LockState::LOCKED &&
                  !locks_.mismatch(l);
        if (!ok) failed += (failed.empty() ? "" : ",") + l;
    }
    bool pass = failed.empty() && sensor_.busHealthy();
    std::string details = std::string("check=self_test;result=") + (pass ? "PASS" : "FAIL");
    if (!failed.empty()) details += ";failed=" + failed;
    if (!sensor_.busHealthy()) details += ";bus=FAIL";
    bool written = record(EventType::ALARM, pass ? Outcome::SUCCESS : Outcome::FAILED, "", details);
    return pass && written;
}

Result FaultMonitor::startupTest()
{
    if (modes_.mode() != Mode::Startup) return Fail(Reason::WrongMode);
    bool pass = selfTest();
    Result r = modes_.startupResult(pass);               // Startup -> Normal / Fault
    if (!pass) {
        selfTestFailed_ = true;
        tick();                                          // raises SELF_TEST (and anything else found)
    }
    return r;
}

// ---------- P2-C01 ----------
Result FaultMonitor::clearFaults(const std::string &user, bool technician)
{
    if (!technician) return Fail(Reason::NotPermitted);
    if (modes_.mode() != Mode::Maintenance && modes_.mode() != Mode::Fault) return Fail(Reason::WrongMode);

    locks_.lockAll();                                    // retry every lock command
    if (storageBad_) {                                   // storage probe: one real write
        storageBad_ = false;
        if (!record(EventType::ALARM, Outcome::SUCCESS, user, "check=storage_probe")) storageBad_ = true;
        try { comm_.send(MessageType::HEALTH, "check=storage_probe"); }
        catch (const CommStorageError &) { storageBad_ = true; }
    }

    std::map<std::string, Condition> found = detect();
    std::vector<std::string> gone;
    for (auto &kv : active_) {
        if (kv.second.code == "SELF_TEST") continue;     // only a passing self-test clears it
        if (found.count(kv.first)) {
            if (!kv.second.acknowledged)
                record(EventType::ALARM, Outcome::REJECTED, user,
                       "ack_fault_id=" + num(kv.second.id) + ";code=" + kv.second.code + ";still_present=1");
            kv.second.acknowledged = true;
        } else {
            gone.push_back(kv.first);
        }
    }
    for (const std::string &k : gone) clear(k, user);
    tick();                                              // anything new is raised now

    for (const auto &kv : active_)
        if (kv.second.severity == Severity::CRITICAL && kv.second.code != "SELF_TEST")
            return Fail(Reason::FaultActive);
    return Ok();
}

// ---------- P2-M05 / M07 / M08 ----------
Result FaultMonitor::returnToNormal(const std::string &user, bool technician, bool transactionActive)
{
    if (modes_.mode() == Mode::Maintenance) {
        Result r = modes_.endMaintenance(user, technician);   // -> Startup
        if (!r.ok) return r;
        return startupTest();
    }
    if (modes_.mode() != Mode::Fault) return Fail(Reason::WrongMode);
    if (!technician)       return Fail(Reason::NotPermitted);
    if (transactionActive) return Fail(Reason::Busy);

    tick();
    for (const auto &kv : active_)                       // FR-MOD-08: Criticals must be cleared first
        if (kv.second.severity == Severity::CRITICAL && kv.second.code != "SELF_TEST")
            return Fail(Reason::FaultActive);

    if (!selfTest()) {                                   // stays in Fault (self-loop)
        selfTestFailed_ = true;
        tick();
        return Fail(Reason::SelfTestFailed);
    }
    selfTestFailed_ = false;                             // the fresh pass replaces the old failure
    if (active_.count("SELF_TEST@")) clear("SELF_TEST@", user);
    return modes_.recovered(user);                       // Fault -> Normal
}

// ---------- queries ----------
std::vector<Fault> FaultMonitor::active() const
{
    std::vector<Fault> out;
    for (const auto &kv : active_) out.push_back(kv.second);
    return out;
}

bool FaultMonitor::blocked(const std::string &location) const
{
    for (const auto &kv : active_)
        if (kv.second.severity == Severity::WARNING && kv.second.location == location) return true;
    return false;
}

bool FaultMonitor::hasCritical() const
{
    for (const auto &kv : active_)
        if (kv.second.severity == Severity::CRITICAL) return true;
    return false;
}
