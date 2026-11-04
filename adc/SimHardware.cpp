// SimHardware.cpp - the simulated cabinet (demo and tests only)
#include "SimHardware.h"

SimHardware::SimHardware(const std::vector<std::string> &locations, Clock clock)
    : order_(locations), clock_(clock)
{
    for (const std::string &l : locations) bays_[l] = Bay();   // closed and locked at power-up
}

long long SimHardware::now() const { return clock_ ? clock_() : 0; }

std::vector<std::string> SimHardware::locations() const { return order_; }

Reading SimHardware::read(const std::string &location, bool drawer)
{
    if (!bays_.count(location)) return Reading{false, false, now()};
    Bay &b = bays_[location];
    bool value = drawer ? b.closed : b.engaged;
    if (b.invalid) return Reading{false, value, now()};
    if (b.silent)  return Reading{true, value, b.silentSince};       // old sample, never refreshed
    return Reading{true, value, now()};
}

Reading SimHardware::drawerClosed(const std::string &location) { return read(location, true); }
Reading SimHardware::lockEngaged(const std::string &location)  { return read(location, false); }
Reading SimHardware::sensorBus() { return Reading{true, !busFailed_, now()}; }

bool SimHardware::commandLock(const std::string &location, bool locked)
{
    if (!bays_.count(location) || bays_[location].jammed) return false;
    bays_[location].engaged = locked;            // a working lock moves at once in the simulation
    return true;
}

bool SimHardware::openDrawer(const std::string &location)
{
    if (!bays_.count(location) || bays_[location].engaged) return false;   // locked drawers cannot open
    bays_[location].closed = false;
    return true;
}

bool SimHardware::closeDrawer(const std::string &location)
{
    if (!bays_.count(location)) return false;
    bays_[location].closed = true;
    return true;
}

bool SimHardware::jamLock(const std::string &location, bool jammed)
{
    if (!bays_.count(location)) return false;
    bays_[location].jammed = jammed;             // stays in whatever position it is in now
    return true;
}

bool SimHardware::failSensor(const std::string &location, bool failed)
{
    if (!bays_.count(location)) return false;
    bays_[location].invalid = failed;
    return true;
}

bool SimHardware::silenceSensor(const std::string &location, bool silent)
{
    if (!bays_.count(location)) return false;
    bays_[location].silent = silent;
    bays_[location].silentSince = now();
    return true;
}

void SimHardware::failBus(bool failed) { busFailed_ = failed; }
