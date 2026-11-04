// TestIntegration.cpp - the whole Part A cabinet wired like main.cpp, run through the
// Deliverable 1 scenarios: Sc1 dispense, Sc3 hardware fault, Sc4 communication loss.
// Owner: Rawaad (integration testing)
#include <cstdio>
#include <iostream>
#include <vector>
#include "../adc/CabinetPlatform.h"
#include "../adc/CommManager.h"
#include "../adc/DrawerSensor.h"
#include "../adc/EventLog.h"
#include "../adc/FaultMonitor.h"
#include "../adc/LockDriver.h"
#include "../adc/ModeManager.h"
#include "../adc/MsSim.h"
#include "../adc/SimHardware.h"
#include "../adc/access_control.h"
#include "../adc/dispense_controller.h"
#include "../adc/inventory_manager.h"

static long long t = 0;
static Clock clock_ = [] { return t; };
static int failures = 0;
static void check(const std::string &name, bool ok)
{
    std::cout << (ok ? "PASS " : "FAIL ") << name << '\n';
    if (!ok) failures++;
}

static const int DAY = 1000;

struct FreshFiles {
    FreshFiles() { t = 0; std::remove("test_int_log.txt"); std::remove("test_int_outbox.txt"); }
};

struct Cabinet {
    FreshFiles         files;
    EventLog           log;
    MsSim              ms;
    CommManager        comm;
    SimHardware        hw;
    DrawerSensor       sensor;
    LockDriver         locks;
    ModeManager        modes;
    FaultMonitor       faults;
    CabinetPlatform    platform;
    AccessControl      access;
    InventoryManager   inventory;
    DispenseController dispense;
    std::vector<std::string> sent;

    Cabinet()
        : log("ADC-19", "test_int_log.txt", clock_), ms("ADC-19", clock_),
          comm("ADC-19", "test_int_outbox.txt", clock_, [this](const std::string &b) { sent.push_back(b); ms.transport(b); }, log),
          hw({"A1", "A2", "A3", "B1"}, clock_), sensor(hw, clock_), locks(hw, sensor, clock_),
          modes(locks, log), faults(sensor, locks, modes, log, comm, clock_),
          platform(modes, faults, locks, log, comm, clock_, DAY),
          access(platform), inventory(platform, access),
          dispense(platform, access, inventory, {{"morphine-10mg", 2}, {"ondansetron-4mg", 2}, {"heparin-5000u", 1}})
    {
        access.addCredential("BADGE-N1", "nurse.amy", Role::Nurse, "1111");
        access.addCredential("BADGE-P1", "pharm.lee", Role::Pharmacist, "3333");
        access.addCredential("BADGE-T1", "tech.sam", Role::Technician, "4444");
        inventory.load({
            {"A1", "morphine-10mg", "LOT-M1", DAY + 200, 20},
            {"A2", "ondansetron-4mg", "LOT-O1", DAY + 90, 15},
            {"A3", "heparin-5000u", "LOT-H0", DAY - 5, 10},
            {"B1", "", "", 0, 0}
        });
        locks.setClosedHandler([this](const std::string &) { dispense.drawerClosed(); });
        tickAll();
        faults.startupTest();
    }
    void tickAll() { comm.tick(); ms.pump(comm); faults.tick(); }
    void wait(long long ms_) { for (long long i = 0; i < ms_ / 1000; i++) { t += 1000; tickAll(); } }
    void login(const char *badge, const char *pin) { access.logout(); access.authenticate(badge, pin); }
    bool allLocked()
    {
        for (const std::string &l : hw.locations()) if (sensor.lock(l) != LockState::LOCKED) return false;
        return true;
    }
    bool logHas(EventType type, const std::string &text)
    {
        for (const LogEntry &e : log.entries())
            if (e.type == type && e.details.find(text) != std::string::npos) return true;
        return false;
    }
    bool has(const std::string &code)
    {
        for (const Fault &f : faults.active()) if (f.code == code) return true;
        return false;
    }
    int sentCount(const std::string &prefix, const std::string &text)
    {
        int n = 0;
        for (const std::string &m : sent)
            if (m.compare(0, prefix.size(), prefix) == 0 && m.find(text) != std::string::npos) n++;
        return n;
    }
};

int main()
{
    // ---------- Sc1: authenticate -> dispense -> log ----------
    {
        Cabinet c;
        check("INT_startup_reaches_normal", c.modes.mode() == Mode::Normal && c.allLocked());
        c.login("BADGE-N1", "1111");
        check("INT_dispense_releases_only_A1", c.dispense.request("morphine-10mg", 2, "PT-1", false).ok &&
              c.sensor.lock("A1") == LockState::UNLOCKED && c.sensor.lock("A2") == LockState::LOCKED);
        check("INT_cannot_confirm_before_close", c.dispense.confirmQuantity(2).reason == Reason::NoTransaction);
        c.hw.openDrawer("A1");
        c.tickAll();
        c.hw.closeDrawer("A1");
        c.tickAll();
        check("INT_close_relocks_and_waits_for_quantity", c.sensor.lock("A1") == LockState::LOCKED &&
              c.dispense.state() == TxState::AwaitingConfirm);
        check("INT_stock_not_changed_before_confirm", c.inventory.get("A1")->quantity == 20);
        check("INT_confirm_completes", c.dispense.confirmQuantity(2).ok && c.inventory.get("A1")->quantity == 18);
        check("INT_dispense_logged_with_patient_id_only",
              c.logHas(EventType::DISPENSE, "location=A1;medication=morphine-10mg;quantity=2;patient_id=PT-1"));
        check("INT_auth_logged", c.logHas(EventType::AUTH_ATTEMPT, "badge=BADGE-N1"));
        check("INT_all_locked_after", c.allLocked() && c.faults.active().empty());
        c.wait(2000);
        check("INT_log_forwarded_to_ms", c.sentCount("EVENT|", "event=DISPENSE") >= 1);
    }

    // ---------- log translation and security ----------
    {
        Cabinet c;
        c.access.authenticate("BADGE-N1", "9999");
        check("INT_wrong_pin_logged_with_reason", c.logHas(EventType::AUTH_ATTEMPT, "reason=WRONG_PIN;badge=BADGE-N1"));
        bool pinLogged = false;
        for (const LogEntry &e : c.log.entries()) if (e.details.find("9999") != std::string::npos) pinLogged = true;
        check("INT_pin_never_logged", !pinLogged);
    }

    // ---------- emergency override -> MS alarm (FR-DSP-08) ----------
    {
        Cabinet c;
        c.login("BADGE-N1", "1111");
        check("INT_override_granted", c.dispense.request("morphine-10mg", 1, "PT-2", true).ok);
        check("INT_override_alarm_sent_to_ms", c.sentCount("ALARM|", "code=EMERGENCY_OVERRIDE") == 1);
        check("INT_override_logged", c.logHas(EventType::EMERGENCY_OVERRIDE, "patient_id=PT-2"));
    }

    // ---------- Table 2: actions by mode ----------
    {
        Cabinet c;
        c.login("BADGE-T1", "4444");
        check("INT_technician_enters_maintenance",
              c.modes.enterMaintenance(c.access.currentUser(), c.access.isPermitted(Action::EnterMaintenance).ok,
                                       c.dispense.state() != TxState::Idle).ok);
        c.login("BADGE-N1", "1111");
        check("INT_no_dispense_in_maintenance", c.dispense.request("morphine-10mg", 1, "PT-1", false).reason == Reason::WrongMode &&
              c.allLocked());
        check("INT_nurse_cannot_end_maintenance",
              c.faults.returnToNormal(c.access.currentUser(), c.access.isPermitted(Action::ClearFault).ok, false).reason == Reason::NotPermitted);
    }
    {
        Cabinet c;
        c.login("BADGE-N1", "1111");
        c.dispense.request("morphine-10mg", 1, "PT-1", false);
        c.login("BADGE-T1", "4444");
        check("INT_no_maintenance_during_dispense",
              c.modes.enterMaintenance("tech.sam", true, c.dispense.state() != TxState::Idle).reason == Reason::Busy);
    }

    // ---------- Sc3: hardware faults ----------
    {
        Cabinet c;                                  // blocked location: refused, no false Critical
        c.hw.failSensor("A1", true);
        c.tickAll();
        c.login("BADGE-N1", "1111");
        Result r = c.dispense.request("morphine-10mg", 1, "PT-1", false);
        check("INT_blocked_location_refused", !r.ok && c.dispense.state() == TxState::Idle &&
              c.faults.blocked("A1"));
        check("INT_policy_refusal_is_not_lock_failure", !c.has("LOCK_FAILURE") && c.modes.mode() == Mode::Normal);
        check("INT_other_locations_still_work", c.dispense.request("ondansetron-4mg", 1, "PT-1", false).ok);
    }
    {
        Cabinet c;                                  // lock jams while the drawer is open
        c.login("BADGE-N1", "1111");
        c.dispense.request("morphine-10mg", 1, "PT-1", false);
        c.hw.openDrawer("A1");
        c.tickAll();
        c.hw.jamLock("A1", true);
        c.hw.closeDrawer("A1");
        c.tickAll();
        check("INT_jam_on_relock_enters_fault", c.has("LOCK_FAILURE") && c.modes.mode() == Mode::Fault);
        check("INT_fault_alarm_sent_to_ms", c.sentCount("ALARM|", "code=LOCK_FAILURE") >= 1);
        check("INT_transaction_still_recordable", c.dispense.confirmQuantity(1).ok &&
              c.inventory.get("A1")->quantity == 19);                   // stock stays correct
        check("INT_no_dispense_in_fault", c.dispense.request("ondansetron-4mg", 1, "PT-1", false).reason == Reason::WrongMode);

        c.login("BADGE-T1", "4444");
        bool tech = c.access.isPermitted(Action::ClearFault).ok;
        check("INT_clear_refused_while_jammed", c.faults.clearFaults("tech.sam", tech).reason == Reason::FaultActive);
        c.hw.jamLock("A1", false);
        check("INT_clear_after_repair", c.faults.clearFaults("tech.sam", tech).ok);
        check("INT_recover_to_normal", c.faults.returnToNormal("tech.sam", tech, false).ok && c.modes.mode() == Mode::Normal);
        c.login("BADGE-N1", "1111");
        check("INT_dispense_works_again", c.dispense.request("ondansetron-4mg", 1, "PT-1", false).ok);
    }

    // ---------- Sc4: communication loss ----------
    {
        Cabinet c;
        c.ms.setLinkUp(false);
        c.wait(130000);
        check("INT_link_degraded_still_normal", c.comm.linkStatus() == LinkStatus::DEGRADED &&
              c.modes.mode() == Mode::Normal && c.has("COMM_LOSS"));
        c.login("BADGE-N1", "1111");
        c.dispense.request("morphine-10mg", 1, "PT-3", false);
        c.hw.openDrawer("A1");
        c.tickAll();
        c.hw.closeDrawer("A1");
        c.tickAll();
        check("INT_dispense_works_offline", c.dispense.confirmQuantity(1).ok);
        check("INT_messages_kept_while_offline", c.comm.pendingCount() > 0);
        c.ms.setLinkUp(true);
        c.wait(35000);
        check("INT_outbox_delivered_after_restore", c.comm.linkStatus() == LinkStatus::CONNECTED &&
              c.comm.pendingCount() == 0 && !c.has("COMM_LOSS"));
    }

    // ---------- storage error ----------
    {
        Cabinet c;
        c.log.simulateDiskFailure(true);
        c.login("BADGE-N1", "1111");                // the AUTH_ATTEMPT write fails
        c.tickAll();
        check("INT_failed_write_enters_fault", c.has("STORAGE_ERROR") && c.modes.mode() == Mode::Fault && c.allLocked());
        c.log.simulateDiskFailure(false);
        check("INT_storage_clear_and_recover", c.faults.clearFaults("tech.sam", true).ok &&
              c.faults.returnToNormal("tech.sam", true, false).ok);
    }

    std::remove("test_int_log.txt");
    std::remove("test_int_outbox.txt");
    std::cout << (failures ? "SOME TESTS FAILED" : "ALL PASSED") << '\n';
    return failures ? 1 : 0;
}
