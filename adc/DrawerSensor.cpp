// DrawerSensor.cpp - the usable-reading rule (the secret).
//   usable  <=>  valid  AND  sampledAt <= now  AND  now - sampledAt < 5000 ms
//   Exactly 5000 ms old is stale. Anything not usable is reported as UNKNOWN.
#include "DrawerSensor.h"

DrawerSensor::DrawerSensor(Hardware &hw, Clock clock, long long staleMs)
    : hw_(hw), clock_(clock), staleMs_(staleMs) {}

bool DrawerSensor::usable(const Reading &r) const
{
    long long now = clock_ ? clock_() : 0;
    return r.valid && r.sampledAt <= now && now - r.sampledAt < staleMs_;
}

static bool known(const std::vector<std::string> &all, const std::string &location)
{
    for (const std::string &l : all) if (l == location) return true;
    return false;
}

DrawerState DrawerSensor::drawer(const std::string &location)
{
    if (!known(hw_.locations(), location)) return DrawerState::UNKNOWN;
    Reading r = hw_.drawerClosed(location);
    if (!usable(r)) return DrawerState::UNKNOWN;
    return r.value ? DrawerState::CLOSED : DrawerState::OPEN;
}

LockState DrawerSensor::lock(const std::string &location)
{
    if (!known(hw_.locations(), location)) return LockState::UNKNOWN;
    Reading r = hw_.lockEngaged(location);
    if (!usable(r)) return LockState::UNKNOWN;
    return r.value ? LockState::LOCKED : LockState::UNLOCKED;
}

bool DrawerSensor::busHealthy()
{
    Reading r = hw_.sensorBus();
    return usable(r) && r.value;
}

std::vector<std::string> DrawerSensor::locations() const { return hw_.locations(); }

std::string toString(DrawerState s)
{
    return s == DrawerState::CLOSED ? "closed" : s == DrawerState::OPEN ? "OPEN" : "unknown";
}

std::string toString(LockState s)
{
    return s == LockState::LOCKED ? "locked" : s == LockState::UNLOCKED ? "UNLOCKED" : "unknown";
}
