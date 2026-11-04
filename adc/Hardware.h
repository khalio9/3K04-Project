// Hardware.h - Hardware port: what the software can read from and command on the cabinet
//
// Responsibility: the boundary between the software and the cabinet devices
//                 (4-variable model: IN maps monitored variables to these Readings,
//                 OUT maps commandLock() to the physical lock).
// Secret: the electrical/protocol interface of the locks, drawer switches and sensor bus.
//         SimHardware (demo) and a real driver both implement this, so swapping the
//         hardware changes only the class plugged in at main.cpp.
// Owner: Rawaad
#pragma once
#include <string>
#include <vector>

// One sensor sample. valid = false means the sensor gave no usable value.
struct Reading {
    bool      valid;
    bool      value;        // drawer: true = closed;  lock: true = engaged;  bus: true = healthy
    long long sampledAt;    // ms, same clock as Types.h Clock
};

class Hardware {
public:
    virtual ~Hardware() {}

    virtual std::vector<std::string> locations() const = 0;     // fixed, e.g. A1 A2 A3 B1

    virtual Reading drawerClosed(const std::string &location) = 0;
    virtual Reading lockEngaged(const std::string &location) = 0;
    virtual Reading sensorBus() = 0;                             // shared sensor (Table 1)

    // Ask the actuator to lock (true) or release (false).
    // Returns false if the actuator rejected the command (stalled / jammed).
    // true only means "accepted" - the lock feedback is what confirms it moved.
    virtual bool commandLock(const std::string &location, bool locked) = 0;
};
