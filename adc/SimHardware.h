// SimHardware.h - Simulated cabinet hardware (demo and tests only)
//
// Plays the drawers, locks and sensors so the TAs can open/close drawers, jam a lock or
// break a sensor from the UI. It plugs in as the Hardware port, exactly where a real
// device driver would go (IS-A Hardware).
// Physical rule (NAT): a drawer can only be opened if its lock is not engaged.
// Owner: Rawaad
#pragma once
#include <map>
#include <string>
#include <vector>
#include "Hardware.h"
#include "Types.h"

class SimHardware : public Hardware {
public:
    SimHardware(const std::vector<std::string> &locations, Clock clock);

    // ---- Hardware port ----
    std::vector<std::string> locations() const override;
    Reading drawerClosed(const std::string &location) override;
    Reading lockEngaged(const std::string &location) override;
    Reading sensorBus() override;
    bool    commandLock(const std::string &location, bool locked) override;

    // ---- demo controls (false = unknown location, or physically impossible) ----
    bool openDrawer(const std::string &location);           // fails while the lock is engaged
    bool closeDrawer(const std::string &location);
    bool jamLock(const std::string &location, bool jammed); // jammed lock rejects every command
    bool failSensor(const std::string &location, bool failed);  // invalid readings
    bool silenceSensor(const std::string &location, bool silent); // readings stop updating
    void failBus(bool failed);                               // shared sensor fails

private:
    struct Bay {
        bool      closed = true;
        bool      engaged = true;
        bool      jammed = false;
        bool      invalid = false;
        bool      silent = false;
        long long silentSince = 0;
    };
    std::vector<std::string>    order_;
    std::map<std::string, Bay>  bays_;
    Clock                       clock_;
    bool                        busFailed_ = false;

    long long now() const;
    Reading   read(const std::string &location, bool drawer);
};
