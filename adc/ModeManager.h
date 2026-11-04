// ModeManager.h - ModeManager: the operating-mode state machine (D1 mode diagram)
//
// Responsibility: hold the current mode and allow only the legal transitions P2-M01..M09.
//                 Every transition runs the same entry actions: lock everything when leaving
//                 Normal, and log MODE_CHANGE (FR-LOG-01).
// Secret (the private: section): how the mode is stored, the transition guards and the
//         entry actions.
// Spec: P2-M01..M09. FR-MOD-01..08.
// Uses: LockDriver (lock all), EventLog (MODE_CHANGE entries).
// Callers pass "technician" from AccessControl::isPermitted(...) - this module does not
// check credentials itself.
// Owner: Rawaad
#pragma once
#include <string>
#include "EventLog.h"
#include "LockDriver.h"
#include "common.h"

class ModeManager {
public:
    ModeManager(LockDriver &locks, EventLog &log);   // P2-M01: starts in Startup, all locked

    Mode mode() const { return mode_; }

    Result startupResult(bool passed);               // P2-M02 / P2-M03 (Startup only)
    Result enterMaintenance(const std::string &user, bool technician,
                            bool transactionActive);  // P2-M04 (Normal only)
    Result endMaintenance(const std::string &user, bool technician);   // P2-M05 -> Startup
    void   enterFault(const std::string &why);       // P2-M06 (from any mode)
    Result recovered(const std::string &user);       // P2-M08 (Fault only; FaultMonitor checks the guard)

private:
    LockDriver &locks_;
    EventLog   &log_;
    Mode        mode_ = Mode::Startup;

    void change(Mode to, const std::string &user, const std::string &why);   // entry actions
    Result reject(const std::string &event, const std::string &user, Reason reason); // P2-M09
};
