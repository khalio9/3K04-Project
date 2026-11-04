// main.cpp - creates every module, wires them together, runs the 1-second tick.
//
// All wiring lives ONLY here. Modules are created in uses-hierarchy order:
// a module is created after the modules it uses, and gets them in its constructor.
// TODO lines are where Areeb's and Rawaad's modules are created and ticked.
#include <ctime>
#include "adc/CommManager.h"
#include "adc/EventLog.h"
#include "adc/MsSim.h"
#include "adc/Ui.h"

int main()
{
    const std::string ADC_ID = "ADC-19";

    // Demo clock: seconds since start + whatever "wait" fast-forwards.
    // TODO Rawaad: use the Clock module instead, if he has one.
    std::time_t start = std::time(nullptr);
    long long offsetMs = 0;
    Clock clock = [&]() { return static_cast<long long>(std::time(nullptr) - start) * 1000 + offsetMs; };

    // Level 1: EventLog (uses only the clock)
    EventLog log(ADC_ID, "adc_log.txt", clock);

    // Level 2: CommManager (uses EventLog), with the simulated MS as its network
    MsSim ms(ADC_ID, clock);
    CommManager comm(ADC_ID, "adc_outbox.txt", clock,
                     [&](const std::string &bytes) { ms.transport(bytes); }, log);

    // Incoming MS messages -> the module that owns them
    comm.registerHandler(MessageType::PERMISSION_UPDATE, [](const std::string &body) {
        (void)body;
        return true;   // TODO Areeb: return accessControl.applyPermissionUpdate(body)
    });

    // TODO: create Areeb's and Rawaad's modules here

    // One second of the whole cabinet (at least once per second)
    auto tickAll = [&]() {
        // TODO Areeb:  dispense.tick();
        // TODO Rawaad: faults.tick();   (reads log.isHealthy(), comm.isHealthy())
        comm.tick();
        ms.pump(comm);
    };

    Ui ui(log, comm, ms, tickAll, [&](long long ms_) { offsetMs += ms_; });
    tickAll();          // first heartbeat goes out at once
    ui.run();
    return 0;
}