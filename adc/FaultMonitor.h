// FaultMonitor.h - FaultMonitor: detects every Table 1 fault and applies its response
//
// Responsibility: once per tick, check the hardware, storage and link (FR-FLT-01), raise a
//                 local alarm + log entry + MS alarm for each new fault (FR-FLT-03, -04),
//                 block a location on a Warning, force Fault mode on a Critical (FR-FLT-02,
//                 -05), and run the self-test and recovery (FR-MOD-02, -03, -08).
// Secret (the private: section): the detection rules, the fault keys used to raise each
//         fault only once, and the clear/recovery policy (FR-FLT-06).
// Spec: P2-F01..F08 (detection), P2-C01 (clear), P2-M02/M03/M05/M07/M08 (self-test, recovery).
// Uses: DrawerSensor, LockDriver, ModeManager, EventLog, CommManager, Clock.
// Owner: Rawaad
#pragma once
#include <map>
#include <string>
#include <vector>
#include "CommManager.h"
#include "DrawerSensor.h"
#include "EventLog.h"
#include "LockDriver.h"
#include "ModeManager.h"
#include "Types.h"
#include "common.h"

enum class Severity { WARNING, CRITICAL };

struct Fault {
    int         id;
    std::string code;          // DRAWER_OPEN_TOO_LONG, LOCK_FAILURE, SENSOR_FAILURE, ...
    std::string location;      // "" for cabinet-wide faults
    Severity    severity;
    long long   raisedAt;
    bool        acknowledged;  // a technician has seen it (clearfault) but it is still present
};

class FaultMonitor {
public:
    FaultMonitor(DrawerSensor &sensor, LockDriver &locks, ModeManager &modes,
                 EventLog &log, CommManager &comm, Clock clock);

    // Call at least once per second, and before any privileged action.
    void tick();

    // P2-M02 / P2-M03. Only in Startup. Pass -> Normal, fail -> SELF_TEST fault + Fault.
    Result startupTest();

    // P2-C01. Technician, Maintenance or Fault. Retries the locks, re-checks storage, and
    // clears every fault whose condition is gone. Present faults are only acknowledged.
    // SELF_TEST is never cleared here - only a passing self-test clears it.
    Result clearFaults(const std::string &user, bool technician);

    // The UI "normal" command. Maintenance: end it, run the self-test (P2-M05).
    // Fault: needs no Critical left except SELF_TEST and no transaction, then runs the
    // self-test; pass -> Normal (P2-M07 / P2-M08).
    Result returnToNormal(const std::string &user, bool technician, bool transactionActive);

    // Another module tells us something failed (DispenseController's reportFault, a failed
    // log write). The hardware evidence decides: we just check again now.
    void report(const std::string &code, const std::string &location);

    // ---- queries (read-only, copies) ----
    std::vector<Fault> active() const;
    bool blocked(const std::string &location) const;   // a Warning at this location
    bool hasCritical() const;

private:
    struct Condition { std::string code, location; Severity severity; };

    DrawerSensor &sensor_;
    LockDriver   &locks_;
    ModeManager  &modes_;
    EventLog     &log_;
    CommManager  &comm_;
    Clock         clock_;

    std::map<std::string, Fault> active_;      // key = code@location
    int  nextId_ = 1;
    bool storageBad_ = false;                  // latched until a technician's storage probe succeeds
    bool lastHealthy_ = true;                  // to notice log/outbox health turning false
    bool selfTestFailed_ = false;              // latched until a self-test passes

    long long now() const;
    std::map<std::string, Condition> detect();         // P2-F01..F08: what is wrong right now
    void raise(const std::string &key, const Condition &c);
    void clear(const std::string &key, const std::string &user);
    bool selfTest();
    bool record(EventType type, Outcome outcome, const std::string &user, const std::string &details);
    void notifyMs(const Fault &f, const std::string &state);
};

std::string toString(Severity s);
