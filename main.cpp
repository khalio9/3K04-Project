// main.cpp - creates every module, wires them together, runs the 1-second tick.
//
// All wiring lives ONLY here. Modules are created in uses-hierarchy order:
// a module is created after the modules it uses, and gets them in its constructor.
#include <ctime>
#include "adc/CabinetPlatform.h"
#include "adc/CommManager.h"
#include "adc/DrawerSensor.h"
#include "adc/EventLog.h"
#include "adc/FaultMonitor.h"
#include "adc/LockDriver.h"
#include "adc/ModeManager.h"
#include "adc/MsSim.h"
#include "adc/SimHardware.h"
#include "adc/Ui.h"
#include "adc/access_control.h"
#include "adc/dispense_controller.h"
#include "adc/inventory_manager.h"

int main()
{
    const std::string ADC_ID = "ADC-19";
    const int FIRST_DAY = 1000;          // day number at clock time 0 (expiry dates use day numbers)

    // Demo clock: seconds since start + whatever "wait" fast-forwards.
    // This is the Clock for every module (Types.h); no separate Clock module is needed.
    std::time_t start = std::time(nullptr);
    long long offsetMs = 0;
    Clock clock = [&]() { return static_cast<long long>(std::time(nullptr) - start) * 1000 + offsetMs; };

    // Level 1: EventLog (uses only the clock)
    EventLog log(ADC_ID, "adc_log.txt", clock);

    // Level 2: CommManager (uses EventLog), with the simulated MS as its network
    MsSim ms(ADC_ID, clock);
    CommManager comm(ADC_ID, "adc_outbox.txt", clock,
                     [&](const std::string &bytes) { ms.transport(bytes); }, log);

    // Hardware: the simulated cabinet plugs in where a real driver would go
    SimHardware hw({"A1", "A2", "A3", "B1"}, clock);
    DrawerSensor sensor(hw, clock);
    LockDriver locks(hw, sensor, clock);

    // Modes and faults (power-up = Startup, everything locked: FR-MOD-01)
    ModeManager modes(locks, log);
    FaultMonitor faults(sensor, locks, modes, log, comm, clock);

    // Areeb's modules, on the real platform
    CabinetPlatform platform(modes, faults, locks, log, comm, clock, FIRST_DAY);
    AccessControl access(platform);
    InventoryManager inventory(platform, access);
    DispenseController dispense(platform, access, inventory,
                                {{"morphine-10mg", 2}, {"ondansetron-4mg", 2}, {"heparin-5000u", 1}});

    // Demo data (same sample cabinet as fakes.h)
    access.addCredential("BADGE-N1", "nurse.amy", Role::Nurse, "1111");
    access.addCredential("BADGE-N2", "nurse.raj", Role::Nurse, "2222");
    access.addCredential("BADGE-P1", "pharm.lee", Role::Pharmacist, "3333");
    access.addCredential("BADGE-T1", "tech.sam", Role::Technician, "4444");
    inventory.load({
        {"A1", "morphine-10mg", "LOT-M1", FIRST_DAY + 200, 20},
        {"A2", "ondansetron-4mg", "LOT-O1", FIRST_DAY + 90, 15},
        {"A3", "heparin-5000u", "LOT-H0", FIRST_DAY - 5, 10},     // expired
        {"B1", "", "", 0, 0}                                      // empty
    });

    // Drawer closed after a release -> the dispense waits for the quantity (FR-DSP-05, -06)
    locks.setClosedHandler([&](const std::string &) { dispense.drawerClosed(); });

    // Incoming MS messages -> the module that owns them
    comm.registerHandler(MessageType::PERMISSION_UPDATE, [](const std::string &body) {
        (void)body;
        return true;   // TODO Areeb: return accessControl.applyPermissionUpdate(body)
    });

    // One second of the whole cabinet (at least once per second)
    auto tickAll = [&]() {
        comm.tick();
        ms.pump(comm);
        faults.tick();     // after comm, so it sees this second's link status
    };

    tickAll();                 // first heartbeat goes out at once
    faults.startupTest();      // Startup -> Normal (pass) or Fault (fail)

    Ui ui(log, comm, ms, access, inventory, dispense, modes, faults, locks, hw,
          tickAll, [&](long long ms_) { offsetMs += ms_; });
    ui.run();
    return 0;
}
