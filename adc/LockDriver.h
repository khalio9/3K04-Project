// LockDriver.h - LockDriver: commands the per-location locks and tracks each access cycle
//
// Responsibility: release one location, lock one or all locations, notice when a released
//                 drawer has been opened and closed again (then re-lock, FR-DSP-05), and say
//                 whether a lock did what it was told (P2-F02).
// Secret (the private: section): the actuator command bookkeeping (what was asked, when,
//         and whether it was accepted), the 250 ms settling allowance and the cycle state.
// Spec: P2-A01 (release), P2-F01 (drawer open too long), P2-F02 (lock does not move).
// Uses: Hardware (commands), DrawerSensor (feedback), Clock.
// Owner: Rawaad
#pragma once
#include <functional>
#include <map>
#include <string>
#include <vector>
#include "DrawerSensor.h"
#include "Hardware.h"
#include "Types.h"

namespace Defaults {
    const long long LOCK_SETTLE_MS    = 250;     // time a lock gets to move (assumption, not in D1)
    const long long DRAWER_TIMEOUT_MS = 10000;   // Table 1: drawer not closed after 10 s
}

// Called once when a released drawer has been opened and closed (or was never opened
// before the timeout). main.cpp connects this to DispenseController::drawerClosed().
using ClosedHandler = std::function<void(const std::string &location)>;

class LockDriver {
public:
    LockDriver(Hardware &hw, DrawerSensor &sensor, Clock clock,
               long long settleMs = Defaults::LOCK_SETTLE_MS,
               long long drawerTimeoutMs = Defaults::DRAWER_TIMEOUT_MS);

    // P2-A01. Releases only this location, and only if its drawer is confirmed CLOSED and its
    // lock confirmed LOCKED. Starts an access cycle. false = refused or actuator rejected.
    bool release(const std::string &location);

    bool lock(const std::string &location);     // false = actuator rejected the command
    void lockAll();                             // tries every location, even if one fails

    // Call every tick: watches released drawers, re-locks on closure, calls the ClosedHandler.
    void tick();
    void setClosedHandler(ClosedHandler handler) { onClosed_ = handler; }

    // ---- queries (read-only) ----
    LockState   state(const std::string &location);         // from the sensor, may be UNKNOWN
    DrawerState drawer(const std::string &location) { return sensor_.drawer(location); }
    bool mismatch(const std::string &location);             // P2-F02: command failed, or feedback != command after settling
    bool openTooLong(const std::string &location);          // P2-F01
    bool cycleActive(const std::string &location) const;    // released and not yet re-locked
    bool wantLocked(const std::string &location) const;
    std::vector<std::string> locations() const { return sensor_.locations(); }

private:
    struct Bay {
        bool      wantLocked = true;
        bool      commandFailed = false;
        long long commandAt = 0;
        bool      cycle = false;         // released, waiting for open + close
        bool      sawOpen = false;
        long long releasedAt = 0;
    };

    Hardware     &hw_;
    DrawerSensor &sensor_;
    Clock         clock_;
    long long     settleMs_, drawerTimeoutMs_;
    std::map<std::string, Bay> bays_;
    ClosedHandler onClosed_;

    long long now() const;
    bool command(const std::string &location, bool locked);
    void endCycle(const std::string &location);
};
