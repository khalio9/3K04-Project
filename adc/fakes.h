// Stand-in for teammates' modules (clock, log, mode, hardware, alarms, faults) so the
// three classes run and can be tested alone, plus a ready-made sample system.
#ifndef FAKES_H
#define FAKES_H
#include <map>
#include <set>
#include <string>
#include <vector>
#include "dispense_controller.h"

const int FAKE_TODAY = 1000;

struct FakePlatform : Platform {
    struct Entry { std::string event, user, text; };

    Mode mode_ = Mode::Normal;
    std::vector<Entry> entries;
    std::vector<std::string> alarms, faults;
    std::map<std::string, bool> locked;             // one entry per location
    std::set<std::string> broken;                   // locations whose lock device fails

    int today() override { return FAKE_TODAY; }
    Mode mode() override { return mode_; }
    void log(const std::string &e, const std::string &u, const std::string &t) override
    {
        entries.push_back(Entry{e, u, t});
    }
    bool unlock(const std::string &l) override
    {
        if (!locked.count(l) || broken.count(l)) return false;
        locked[l] = false;
        return true;
    }
    bool lock(const std::string &l) override
    {
        if (!locked.count(l) || broken.count(l)) return false;
        locked[l] = true;
        return true;
    }
    void raiseAlarm(const std::string &c) override { alarms.push_back(c); }
    void reportFault(const std::string &c, const std::string &) override { faults.push_back(c); }

    // simulation controls and inspection
    bool openDrawer(const std::string &l) { return locked.count(l) && !locked[l]; }  // locked drawers cannot open
    bool isLocked(const std::string &l) { return locked[l]; }
    int countEvent(const std::string &e) const
    {
        int n = 0;
        for (const auto &x : entries) if (x.event == e) n++;
        return n;
    }
    bool logContains(const std::string &s) const
    {
        for (const auto &x : entries)
            if (x.text.find(s) != std::string::npos || x.user.find(s) != std::string::npos) return true;
        return false;
    }
};

// Sample cabinet: 4 locations, 4 users, 3 dose limits.
struct System {
    FakePlatform fake;
    AccessControl access;
    InventoryManager inventory;
    DispenseController dispense;

    System()
        : access(fake), inventory(fake, access),
          dispense(fake, access, inventory,
                   {{"morphine-10mg", 2}, {"ondansetron-4mg", 2}, {"heparin-5000u", 1}})
    {
        for (const char *l : {"A1", "A2", "A3", "B1"}) fake.locked[l] = true;
        access.addCredential("BADGE-N1", "nurse.amy", Role::Nurse, "1111");
        access.addCredential("BADGE-N2", "nurse.raj", Role::Nurse, "2222");
        access.addCredential("BADGE-P1", "pharm.lee", Role::Pharmacist, "3333");
        access.addCredential("BADGE-T1", "tech.sam", Role::Technician, "4444");
        inventory.load({
            {"A1", "morphine-10mg", "LOT-M1", FAKE_TODAY + 200, 20},
            {"A2", "ondansetron-4mg", "LOT-O1", FAKE_TODAY + 90, 15},
            {"A3", "heparin-5000u", "LOT-H0", FAKE_TODAY - 5, 10},   // expired
            {"B1", "", "", 0, 0}                                     // empty
        });
    }
};

#endif
