// Protocol.cpp - the wire format (CommManager's secret).
//   One message = one text line:   TYPE|adc_id|seq|time_ms|body|mac
//   mac = keyed hash of everything before it, so messages without the shared key are
//   rejected (NFR-SEC-03).
// NOTE: the keyed FNV hash is a demo placeholder. A real deployment uses TLS / HMAC
// (NFR-SEC-02); swapping it changes only this file.
#include "Protocol.h"

#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <sstream>

namespace Protocol {

static const char *SHARED_KEY = "3K04-G19-ADC-MS";
static const char *NAMES[] = {
    "HEARTBEAT", "STATUS", "INVENTORY", "ALARM", "MAINTENANCE", "HEALTH", "EVENT",
    "HEARTBEAT_REPLY", "PERMISSION_UPDATE", "SCHEDULE", "CONFIG_CHANGE", "ACK"
};
static const int NAME_COUNT = 12;

std::string typeName(MessageType t) { return NAMES[static_cast<int>(t)]; }

static std::string mac(const std::string &text)
{
    uint64_t h = 1469598103934665603ULL;
    for (char c : std::string(SHARED_KEY) + text) {
        h ^= static_cast<unsigned char>(c);
        h *= 1099511628211ULL;
    }
    std::ostringstream o;
    o << std::hex << std::setw(16) << std::setfill('0') << h;
    return o.str();
}

static std::string clean(std::string s)
{
    for (char &c : s)
        if (c == '|' || c == '\n' || c == '\r') c = '/';
    return s;
}

std::string encode(const Message &m)
{
    std::ostringstream o;
    o << typeName(m.type) << '|' << clean(m.adcId) << '|' << m.seq << '|' << m.timeMs
      << '|' << clean(m.body);
    std::string text = o.str();
    return text + '|' + mac(text);
}

bool decode(const std::string &raw, Message &out)
{
    std::string s = raw;
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();

    size_t bar = s.rfind('|');                                  // last field is the mac
    if (bar == std::string::npos) return false;
    std::string text = s.substr(0, bar);
    if (s.substr(bar + 1) != mac(text)) return false;           // fails authentication

    std::string f[5];
    int n = 0;
    for (char c : text) {
        if (c == '|' && n < 4) n++;
        else f[n] += c;
    }
    if (n != 4) return false;                                   // malformed

    int type = -1;
    for (int i = 0; i < NAME_COUNT; i++)
        if (f[0] == NAMES[i]) type = i;
    if (type < 0) return false;

    out.type   = static_cast<MessageType>(type);
    out.adcId  = f[1];
    out.seq    = static_cast<unsigned>(std::strtoul(f[2].c_str(), nullptr, 10));
    out.timeMs = std::strtoll(f[3].c_str(), nullptr, 10);
    out.body   = f[4];
    return true;
}

std::string bodyGet(const std::string &body, const std::string &key)
{
    std::istringstream in(body);
    std::string pair;
    while (std::getline(in, pair, ';')) {
        size_t eq = pair.find('=');
        if (eq != std::string::npos && pair.substr(0, eq) == key) return pair.substr(eq + 1);
    }
    return "";
}

}