// Ui.h - UI (M1.3): the cabinet screen and the demo harness
//
// Responsibility: show mode, comm status, prompts, verdicts and alarms; pass each user
//                 action to exactly one public function of another module.
// Secret (the private: section): screen layout, commands, and how alarms and severity
//         are presented (colour + text + sound, NFR-USE-02).
// No other module calls the UI (top of the uses hierarchy).
// Owner: Omer (Areeb's and Rawaad's commands wired by Rawaad)
#pragma once
#include <functional>
#include <string>
#include "CommManager.h"
#include "EventLog.h"
#include "FaultMonitor.h"
#include "LockDriver.h"
#include "ModeManager.h"
#include "MsSim.h"
#include "SimHardware.h"
#include "dispense_controller.h"

class Ui {
public:
    // tickAll advances every module by one second; advanceClock fast-forwards the demo clock.
    Ui(EventLog &log, CommManager &comm, MsSim &ms,
       AccessControl &access, InventoryManager &inventory, DispenseController &dispense,
       ModeManager &modes, FaultMonitor &faults, LockDriver &locks, SimHardware &hw,
       std::function<void()> tickAll, std::function<void(long long)> advanceClock);

    void run();                                   // runs until the user types "quit"
    void refresh();                               // redraws from read-only calls (REQ-UI-1)
    void showResult(const std::string &text);     // verdict of the last action

private:
    EventLog    &log_;
    CommManager &comm_;
    MsSim       &ms_;
    AccessControl      &access_;
    InventoryManager   &inventory_;
    DispenseController &dispense_;
    ModeManager        &modes_;
    FaultMonitor       &faults_;
    LockDriver         &locks_;
    SimHardware        &hw_;
    std::function<void()>          tickAll_;
    std::function<void(long long)> advanceClock_;
    std::string result_ = "Ready.";

    bool handle(const std::string &line);         // one command -> one call into a module
    void showLog(int n);
    void help();
    void alarm(const char *colour, const std::string &severity, const std::string &what);
    void showResult(const Result &r, const std::string &okText);
    void hardware(bool done, const std::string &okText);
};