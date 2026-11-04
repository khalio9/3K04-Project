// TestHardwareModes.cpp - tests named after the P2 spec rows (faults, modes, locks, sensors)
// Owner: Rawaad
#include <cstdio>
#include <iostream>
#include <vector>
#include "../adc/CommManager.h"
#include "../adc/DrawerSensor.h"
#include "../adc/EventLog.h"
#include "../adc/FaultMonitor.h"
#include "../adc/LockDriver.h"
#include "../adc/ModeManager.h"
#include "../adc/MsSim.h"
#include "../adc/SimHardware.h"

static long long t = 0;
static Clock clock_ = [] { return t; };
static int failures = 0;
static void check(const std::string &name, bool ok)
{
    std::cout << (ok ? "PASS " : "FAIL ") << name << '\n';
    if (!ok) failures++;
}

// Runs first: restarts the clock and deletes the last rig's files before the new one opens them.
struct FreshFiles {
    FreshFiles() { t = 0; std::remove("test_hw_log.txt"); std::remove("test_hw_outbox.txt"); }
};

// One cabinet with 4 locations, wired like main.cpp (no Areeb modules here).
struct Rig {
    FreshFiles   files;
    EventLog     log;
    MsSim        ms;
    CommManager  comm;
    SimHardware  hw;
    DrawerSensor sensor;
    LockDriver   locks;
    ModeManager  modes;
    FaultMonitor faults;
    int          closedCalls = 0;
    std::vector<std::string> sent;              // everything put on the network

    Rig()
        : log("ADC-19", "test_hw_log.txt", clock_), ms("ADC-19", clock_),
          comm("ADC-19", "test_hw_outbox.txt", clock_, [this](const std::string &b) { sent.push_back(b); ms.transport(b); }, log),
          hw({"A1", "A2", "A3", "B1"}, clock_), sensor(hw, clock_), locks(hw, sensor, clock_),
          modes(locks, log), faults(sensor, locks, modes, log, comm, clock_)
    {
        locks.setClosedHandler([this](const std::string &) { closedCalls++; });
        tickAll();                                  // link comes up
        linkUp = comm.linkStatus() == LinkStatus::CONNECTED;
    }
    bool linkUp = false;
    void tickAll() { comm.tick(); ms.pump(comm); faults.tick(); }
    void wait(long long ms_) { for (long long i = 0; i < ms_ / 1000; i++) { t += 1000; tickAll(); } }
    bool has(const std::string &code, const std::string &loc = "")
    {
        for (const Fault &f : faults.active()) if (f.code == code && f.location == loc) return true;
        return false;
    }
    int logged(EventType type, const std::string &text = "")
    {
        int n = 0;
        for (const LogEntry &e : log.entries())
            if (e.type == type && e.details.find(text) != std::string::npos) n++;
        return n;
    }
    bool allLocked()
    {
        for (const std::string &l : hw.locations()) if (sensor.lock(l) != LockState::LOCKED) return false;
        return true;
    }
};

int main()
{
    // ---------- modes: startup ----------
    {
        Rig r;
        check("rig_link_connected", r.linkUp && !r.has("COMM_LOSS"));
        check("M01_power_up_is_startup_all_locked", r.modes.mode() == Mode::Startup && r.allLocked());
        check("M01_mode_change_logged", r.logged(EventType::MODE_CHANGE, "to=STARTUP") == 1);
        check("M02_self_test_pass_goes_normal", r.faults.startupTest().ok && r.modes.mode() == Mode::Normal);
        check("M02_self_test_result_logged", r.logged(EventType::ALARM, "check=self_test;result=PASS") == 1);
        check("M09_startup_test_twice_rejected", r.faults.startupTest().reason == Reason::WrongMode);
    }
    {
        Rig r;
        r.hw.failSensor("A1", true);
        Result res = r.faults.startupTest();
        check("M03_self_test_fail_goes_fault", !res.ok && res.reason == Reason::SelfTestFailed &&
                                               r.modes.mode() == Mode::Fault);
        check("F06_self_test_fault_raised", r.has("SELF_TEST") && r.has("SENSOR_FAILURE", "A1"));
    }

    // ---------- sensors ----------
    {
        Rig r;
        r.faults.startupTest();
        r.hw.silenceSensor("A2", true);             // readings stop updating at t
        r.wait(4000);
        check("F03_reading_4s_old_still_usable", r.sensor.drawer("A2") == DrawerState::CLOSED && !r.has("SENSOR_FAILURE", "A2"));
        r.wait(1000);
        check("F03_reading_exactly_5s_old_is_stale", r.sensor.drawer("A2") == DrawerState::UNKNOWN && r.has("SENSOR_FAILURE", "A2"));
        check("F03_warning_blocks_only_that_location", r.faults.blocked("A2") && !r.faults.blocked("A1") &&
                                                       r.modes.mode() == Mode::Normal);
        check("NFR-SAF-02_unknown_never_reported_as_locked", r.locks.state("A2") == LockState::UNKNOWN);
        check("A01_release_refused_on_unknown_state", !r.locks.release("A2"));
        r.hw.silenceSensor("A2", false);
        r.tickAll();
        check("F03_warning_clears_when_sensor_recovers", !r.has("SENSOR_FAILURE", "A2") && !r.faults.blocked("A2"));
        check("F03_cleared_is_logged", r.logged(EventType::FAULT_CLEARED, "SENSOR_FAILURE") == 1);
    }
    {
        Rig r;
        r.faults.startupTest();
        r.hw.failBus(true);
        r.tickAll();
        check("F04_shared_sensor_is_critical_fault_mode", r.has("SENSOR_BUS_FAILURE") && r.modes.mode() == Mode::Fault && r.allLocked());
    }

    // ---------- locks and the access cycle ----------
    {
        Rig r;
        r.faults.startupTest();
        check("A01_release_only_requested_location", r.locks.release("A1") &&
              r.sensor.lock("A1") == LockState::UNLOCKED && r.sensor.lock("A2") == LockState::LOCKED);
        check("A01_unknown_location_refused", !r.locks.release("Z9"));
        r.hw.openDrawer("A1");
        r.tickAll();
        r.hw.closeDrawer("A1");
        r.tickAll();
        check("DSP05_relock_when_drawer_closes", r.sensor.lock("A1") == LockState::LOCKED && !r.locks.cycleActive("A1"));
        check("DSP05_closed_handler_called_once", r.closedCalls == 1);
        r.tickAll();
        check("DSP05_handler_not_called_again", r.closedCalls == 1);
    }
    {
        Rig r;
        r.faults.startupTest();
        r.locks.release("A1");
        r.wait(9000);
        check("D-P2-3_unopened_drawer_still_released_at_9s", r.sensor.lock("A1") == LockState::UNLOCKED);
        r.wait(1000);
        check("D-P2-3_unopened_drawer_relocked_at_10s", r.sensor.lock("A1") == LockState::LOCKED && r.closedCalls == 1);
    }
    {
        Rig r;
        r.faults.startupTest();
        r.locks.release("A1");
        r.hw.openDrawer("A1");
        r.wait(9000);
        check("F01_open_9s_no_warning", !r.has("DRAWER_OPEN_TOO_LONG", "A1"));
        r.wait(1000);
        check("F01_open_10s_warning_and_blocked", r.has("DRAWER_OPEN_TOO_LONG", "A1") && r.faults.blocked("A1") &&
                                                 r.modes.mode() == Mode::Normal);
        r.hw.closeDrawer("A1");
        r.tickAll();
        check("F01_close_clears_and_relocks", !r.has("DRAWER_OPEN_TOO_LONG", "A1") && r.sensor.lock("A1") == LockState::LOCKED);
    }

    // ---------- lock failure, fault lifecycle, clearing, recovery ----------
    {
        Rig r;
        r.faults.startupTest();
        r.hw.jamLock("A3", true);
        r.locks.lockAll();
        r.tickAll();
        check("F02_jammed_lock_is_critical", r.has("LOCK_FAILURE", "A3"));
        check("MOD07_critical_enters_fault", r.modes.mode() == Mode::Fault);
        r.wait(3000);
        check("E01_raised_once_not_every_tick", r.logged(EventType::FAULT_RAISED, "LOCK_FAILURE") == 1);
        int alarms = 0;
        for (const std::string &m : r.sent)
            if (m.compare(0, 6, "ALARM|") == 0 && m.find("code=LOCK_FAILURE") != std::string::npos &&
                m.find("state=RAISED") != std::string::npos) alarms++;
        check("FLT04_alarm_sent_to_ms_once", alarms == 1);

        check("C01_non_technician_cannot_clear", r.faults.clearFaults("nurse.amy", false).reason == Reason::NotPermitted);
        Result c = r.faults.clearFaults("tech.sam", true);
        check("C01_present_fault_only_acknowledged", c.reason == Reason::FaultActive && r.has("LOCK_FAILURE", "A3"));
        bool acked = false;
        for (const Fault &f : r.faults.active()) if (f.code == "LOCK_FAILURE") acked = f.acknowledged;
        check("C01_acknowledged_flag_set", acked);
        check("M07_recovery_refused_while_critical", r.faults.returnToNormal("tech.sam", true, false).reason == Reason::FaultActive &&
                                                     r.modes.mode() == Mode::Fault);

        r.hw.jamLock("A3", false);
        check("C01_repaired_fault_cleared", r.faults.clearFaults("tech.sam", true).ok && !r.has("LOCK_FAILURE", "A3"));
        check("FLT06_clear_logged_with_technician", r.logged(EventType::FAULT_CLEARED, "LOCK_FAILURE") == 1);
        check("M08_still_fault_until_self_test", r.modes.mode() == Mode::Fault);
        check("M07_non_technician_cannot_recover", r.faults.returnToNormal("nurse.amy", false, false).reason == Reason::NotPermitted);
        check("M07_no_recovery_during_transaction", r.faults.returnToNormal("tech.sam", true, true).reason == Reason::Busy);
        check("M08_self_test_pass_back_to_normal", r.faults.returnToNormal("tech.sam", true, false).ok &&
                                                   r.modes.mode() == Mode::Normal);
    }
    {
        Rig r;                                     // a failed startup is recovered by a new passing test
        r.hw.failSensor("B1", true);
        r.faults.startupTest();
        r.hw.failSensor("B1", false);
        r.tickAll();
        check("M07_self_test_latch_survives_clearfault", r.faults.clearFaults("tech.sam", true).ok && r.has("SELF_TEST"));
        check("M08_new_passing_test_clears_self_test", r.faults.returnToNormal("tech.sam", true, false).ok &&
                                                       !r.has("SELF_TEST") && r.modes.mode() == Mode::Normal);
    }
    {
        Rig r;                                     // F08: drawer open with no release
        r.faults.startupTest();
        r.locks.release("A1");
        r.hw.jamLock("A1", true);                  // stuck open
        r.hw.openDrawer("A1");
        r.tickAll();
        r.hw.closeDrawer("A1");
        r.tickAll();                               // cycle ends, re-lock rejected
        check("F02_relock_rejected_is_critical", r.has("LOCK_FAILURE", "A1") && r.modes.mode() == Mode::Fault);
        r.hw.openDrawer("A1");
        r.tickAll();
        check("F08_unexpected_open_is_critical", r.has("UNEXPECTED_OPEN", "A1"));
    }

    // ---------- storage ----------
    {
        Rig r;
        r.faults.startupTest();
        r.log.simulateDiskFailure(true);
        try { r.log.append(EventType::ALARM, Outcome::FAILED, "", "check=storage"); } catch (...) {}
        r.tickAll();
        check("F05_storage_error_critical", r.has("STORAGE_ERROR") && r.modes.mode() == Mode::Fault);
        check("F05_clear_fails_while_disk_broken", r.faults.clearFaults("tech.sam", true).reason == Reason::FaultActive);
        r.log.simulateDiskFailure(false);
        check("F05_clear_after_probe_write_succeeds", r.faults.clearFaults("tech.sam", true).ok && !r.has("STORAGE_ERROR"));
        check("F05_recover_to_normal", r.faults.returnToNormal("tech.sam", true, false).ok);
    }

    // ---------- communication ----------
    {
        Rig r;
        r.faults.startupTest();
        r.ms.setLinkUp(false);
        r.wait(130000);                            // 3+ missed heartbeats (30 s each)
        check("F07_comm_loss_is_warning", r.has("COMM_LOSS") && !r.faults.hasCritical());
        check("COM05_still_normal_while_offline", r.modes.mode() == Mode::Normal);
        r.ms.setLinkUp(true);
        r.wait(31000);
        check("F07_clears_when_link_back", !r.has("COMM_LOSS"));
    }

    // ---------- maintenance ----------
    {
        Rig r;
        r.faults.startupTest();
        check("M04_nurse_cannot_enter_maintenance", r.modes.enterMaintenance("nurse.amy", false, false).reason == Reason::NotPermitted);
        check("M04_not_during_transaction", r.modes.enterMaintenance("tech.sam", true, true).reason == Reason::Busy);
        check("M09_rejections_logged", r.logged(EventType::MODE_CHANGE, "event=enter_maintenance") == 2);
        r.locks.release("A1");
        check("M04_technician_enters_maintenance_all_locked", r.modes.enterMaintenance("tech.sam", true, false).ok &&
                                                              r.modes.mode() == Mode::Maintenance && r.allLocked());
        check("C01_clear_allowed_in_maintenance", r.faults.clearFaults("tech.sam", true).ok);
        check("M05_end_maintenance_runs_self_test", r.faults.returnToNormal("tech.sam", true, false).ok &&
                                                    r.modes.mode() == Mode::Normal &&
                                                    r.logged(EventType::MODE_CHANGE, "from=MAINTENANCE;to=STARTUP") == 1);
        check("C01_clear_refused_in_normal", r.faults.clearFaults("tech.sam", true).reason == Reason::WrongMode);
    }

    std::remove("test_hw_log.txt");
    std::remove("test_hw_outbox.txt");
    std::cout << (failures ? "SOME TESTS FAILED" : "ALL PASSED") << '\n';
    return failures ? 1 : 0;
}
