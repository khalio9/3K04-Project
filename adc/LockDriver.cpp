// LockDriver.cpp - command bookkeeping and the access cycle (the secret).
//   Access cycle:  release -> drawer seen OPEN -> drawer seen CLOSED -> re-lock -> ClosedHandler
//   If the drawer is never opened before the timeout, it is re-locked the same way
//   (an unlocked, unattended drawer is a risk - design decision D-P2-3).
#include "LockDriver.h"

LockDriver::LockDriver(Hardware &hw, DrawerSensor &sensor, Clock clock,
                       long long settleMs, long long drawerTimeoutMs)
    : hw_(hw), sensor_(sensor), clock_(clock), settleMs_(settleMs), drawerTimeoutMs_(drawerTimeoutMs)
{
    for (const std::string &l : hw_.locations()) bays_[l] = Bay();
}

long long LockDriver::now() const { return clock_ ? clock_() : 0; }

bool LockDriver::command(const std::string &location, bool locked)
{
    Bay &b = bays_[location];
    b.wantLocked = locked;
    b.commandAt = now();
    b.commandFailed = !hw_.commandLock(location, locked);
    return !b.commandFailed;
}

// P2-A01
bool LockDriver::release(const std::string &location)
{
    if (!bays_.count(location)) return false;
    if (sensor_.drawer(location) != DrawerState::CLOSED || sensor_.lock(location) != LockState::LOCKED)
        return false;                               // never release on unconfirmed state (NFR-SAF-02)
    if (!command(location, false)) return false;
    Bay &b = bays_[location];
    b.cycle = true;
    b.sawOpen = false;
    b.releasedAt = now();
    return true;
}

bool LockDriver::lock(const std::string &location)
{
    if (!bays_.count(location)) return false;
    // Locking a released drawer that is still closed ends the cycle (e.g. a cancelled dispense).
    if (bays_[location].cycle && sensor_.drawer(location) == DrawerState::CLOSED)
        bays_[location].cycle = false;
    return command(location, true);
}

void LockDriver::lockAll()
{
    for (auto &kv : bays_) lock(kv.first);          // no short-circuit: every lock is tried
}

void LockDriver::endCycle(const std::string &location)
{
    bays_[location].cycle = false;
    command(location, true);                        // FR-DSP-05: re-lock on closure
    if (onClosed_) onClosed_(location);
}

void LockDriver::tick()
{
    for (auto &kv : bays_) {
        Bay &b = kv.second;
        if (!b.cycle) continue;
        DrawerState d = sensor_.drawer(kv.first);
        if (d == DrawerState::OPEN) b.sawOpen = true;
        else if (d == DrawerState::CLOSED) {
            bool neverOpened = !b.sawOpen && now() - b.releasedAt >= drawerTimeoutMs_;
            if (b.sawOpen || neverOpened) endCycle(kv.first);
        }                                           // UNKNOWN: wait, the sensor fault is reported elsewhere
    }
}

LockState LockDriver::state(const std::string &location) { return sensor_.lock(location); }

// P2-F02
bool LockDriver::mismatch(const std::string &location)
{
    if (!bays_.count(location)) return false;
    const Bay &b = bays_[location];
    if (b.commandFailed) return true;
    LockState s = sensor_.lock(location);
    if (s == LockState::UNKNOWN) return false;      // cannot tell; P2-F03 reports the sensor
    bool engaged = s == LockState::LOCKED;
    return engaged != b.wantLocked && now() - b.commandAt >= settleMs_;
}

// P2-F01: timer starts at release (D1 clarification), equality counts as too long
bool LockDriver::openTooLong(const std::string &location)
{
    if (!bays_.count(location)) return false;
    const Bay &b = bays_[location];
    return b.cycle && sensor_.drawer(location) == DrawerState::OPEN &&
           now() - b.releasedAt >= drawerTimeoutMs_;
}

bool LockDriver::cycleActive(const std::string &location) const
{
    auto it = bays_.find(location);
    return it != bays_.end() && it->second.cycle;
}

bool LockDriver::wantLocked(const std::string &location) const
{
    auto it = bays_.find(location);
    return it == bays_.end() || it->second.wantLocked;
}
