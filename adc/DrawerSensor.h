// DrawerSensor.h - DrawerSensor: turns raw sensor samples into states the rest of the
//                  software can trust
//
// Responsibility: decide whether a drawer/lock reading is usable and report CLOSED/OPEN,
//                 LOCKED/UNLOCKED or UNKNOWN (NFR-SAF-02: never report what it cannot confirm).
// Secret (the private: section): what makes a reading usable - the validity flag, future
//         timestamps and the 5-second freshness limit (Table 1 "Sensor failure").
// Spec: P2-F03 (one location), P2-F04 (shared sensor).
// Uses: Hardware, Clock.  No side effects: reading never changes modes or locks.
// Owner: Rawaad
#pragma once
#include <string>
#include <vector>
#include "Hardware.h"
#include "Types.h"

enum class DrawerState { CLOSED, OPEN, UNKNOWN };
enum class LockState   { LOCKED, UNLOCKED, UNKNOWN };

namespace Defaults {
    const long long SENSOR_STALE_MS = 5000;    // Table 1: no reading for 5 seconds
}

class DrawerSensor {
public:
    DrawerSensor(Hardware &hw, Clock clock, long long staleMs = Defaults::SENSOR_STALE_MS);

    DrawerState drawer(const std::string &location);   // UNKNOWN if invalid, stale or unknown location
    LockState   lock(const std::string &location);
    bool        busHealthy();                          // false if the shared sensor is bad or stale
    std::vector<std::string> locations() const;

private:
    Hardware &hw_;
    Clock     clock_;
    long long staleMs_;

    bool usable(const Reading &r) const;
};

std::string toString(DrawerState s);
std::string toString(LockState s);
