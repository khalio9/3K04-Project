// CabinetPlatform.h - the real Platform for AccessControl / InventoryManager /
//                     DispenseController (fakes.h is the test stand-in)
//
// Responsibility: give Areeb's modules what platform.h asks for, using the real modules:
//                 mode -> ModeManager, lock/unlock -> LockDriver, log -> EventLog,
//                 alarms -> CommManager, faults -> FaultMonitor.
// Secret (the private: section): how Areeb's text log calls are turned into EventLog
//         entries (event name -> EventType, first word -> Outcome, "a=b c=d" -> "a=b;c=d").
// Uses: ModeManager, FaultMonitor, LockDriver, EventLog, CommManager, Clock.
// Owner: Rawaad (integration)
#pragma once
#include <string>
#include "CommManager.h"
#include "EventLog.h"
#include "FaultMonitor.h"
#include "LockDriver.h"
#include "ModeManager.h"
#include "platform.h"

class CabinetPlatform : public Platform {
public:
    // firstDay = day number of clock time 0 (expiry dates are day numbers)
    CabinetPlatform(ModeManager &modes, FaultMonitor &faults, LockDriver &locks,
                    EventLog &log, CommManager &comm, Clock clock, int firstDay);

    int  today() override;
    Mode mode() override;
    void log(const std::string &event, const std::string &user, const std::string &text) override;

    // P2-A01 hardware guard: Normal mode, no Critical, location not blocked by a Warning,
    // drawer confirmed closed and lock confirmed locked. Otherwise nothing moves.
    bool unlock(const std::string &location) override;
    bool lock(const std::string &location) override;

    void raiseAlarm(const std::string &code) override;    // e.g. EMERGENCY_OVERRIDE -> MS (FR-DSP-08)
    void reportFault(const std::string &code, const std::string &location) override;

private:
    ModeManager  &modes_;
    FaultMonitor &faults_;
    LockDriver   &locks_;
    EventLog     &log_;
    CommManager  &comm_;
    Clock         clock_;
    int           firstDay_;
};
