// CabinetPlatform.cpp - translating Areeb's Platform calls (the secret).
#include "CabinetPlatform.h"

#include <sstream>
#include <vector>

CabinetPlatform::CabinetPlatform(ModeManager &modes, FaultMonitor &faults, LockDriver &locks,
                                 EventLog &log, CommManager &comm, Clock clock, int firstDay)
    : modes_(modes), faults_(faults), locks_(locks), log_(log), comm_(comm), clock_(clock),
      firstDay_(firstDay) {}

int CabinetPlatform::today()
{
    long long ms = clock_ ? clock_() : 0;
    return firstDay_ + static_cast<int>(ms / 86400000LL);
}

Mode CabinetPlatform::mode() { return modes_.mode(); }

static EventType eventFor(const std::string &name)
{
    if (name == "AUTH_ATTEMPT")       return EventType::AUTH_ATTEMPT;
    if (name == "CREDENTIAL_LOCKOUT") return EventType::CREDENTIAL_LOCKOUT;
    if (name == "DISPENSE")           return EventType::DISPENSE;
    if (name == "REPLENISH")          return EventType::REPLENISH;
    if (name == "EMERGENCY_OVERRIDE") return EventType::EMERGENCY_OVERRIDE;
    return EventType::ALARM;
}

static bool outcomeFor(const std::string &word, Outcome &out)
{
    if (word == "SUCCESS")  { out = Outcome::SUCCESS;  return true; }
    if (word == "FAILED")   { out = Outcome::FAILED;   return true; }
    if (word == "REJECTED") { out = Outcome::REJECTED; return true; }
    if (word == "GRANTED")  { out = Outcome::GRANTED;  return true; }
    if (word == "COMPLETE") { out = Outcome::COMPLETE; return true; }
    return false;
}

// "FAILED WRONG_PIN badge=B1"  ->  outcome FAILED, details "reason=WRONG_PIN;badge=B1"
// "patient=PT-1" becomes "patient_id=PT-1": EventLog only accepts the patient identifier
// (NFR-SEC-04, row L2).
void CabinetPlatform::log(const std::string &event, const std::string &user, const std::string &text)
{
    std::istringstream in(text);
    std::vector<std::string> words;
    std::string w;
    while (in >> w) words.push_back(w);

    Outcome outcome = Outcome::SUCCESS;
    size_t first = 0;
    if (!words.empty() && outcomeFor(words[0], outcome)) first = 1;

    std::string details;
    for (size_t i = first; i < words.size(); i++) {
        std::string part = words[i];
        if (part.find('=') == std::string::npos)
            part = (event == "CREDENTIAL_LOCKOUT" ? "badge=" : "reason=") + part;
        else if (part.compare(0, 8, "patient=") == 0)
            part = "patient_id=" + part.substr(8);
        details += (details.empty() ? "" : ";") + part;
    }
    if (eventFor(event) == EventType::ALARM) details = "event=" + event + (details.empty() ? "" : ";" + details);

    try {
        log_.append(eventFor(event), outcome, user, details);
    } catch (const LogStorageError &) {
        faults_.report("STORAGE_ERROR", "");
    } catch (const std::exception &) {
        // rejected details (row L2): nothing is written; never fall back to raw text
    }
}

bool CabinetPlatform::unlock(const std::string &location)
{
    faults_.tick();                                   // fresh check before a privileged action
    if (modes_.mode() != Mode::Normal || faults_.hasCritical() || faults_.blocked(location))
        return false;                                 // refused by policy: no command is sent
    return locks_.release(location);
}

bool CabinetPlatform::lock(const std::string &location) { return locks_.lock(location); }

void CabinetPlatform::raiseAlarm(const std::string &code)
{
    try {
        comm_.send(MessageType::ALARM, "code=" + code + ";severity=WARNING");
    } catch (const CommStorageError &) {
        faults_.report("STORAGE_ERROR", "");
    }
}

// DispenseController reports LOCK_FAILURE when unlock() returns false. FaultMonitor only
// raises it if the lock hardware really failed - a policy refusal (blocked location,
// wrong mode) does not create a false Critical.
void CabinetPlatform::reportFault(const std::string &code, const std::string &location)
{
    faults_.report(code, location);
}
