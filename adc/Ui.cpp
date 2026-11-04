// Ui.cpp - terminal screen + demo controls.
// Swapping this for a touchscreen GUI changes only this file.
// Each cabinet / hardware command is one call to Areeb's or Rawaad's public functions.
#include "Ui.h"

#include <cstdlib>
#include <ctime>
#include <iostream>
#include <sstream>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

static const char *RED    = "\033[1;31m";
static const char *YELLOW = "\033[1;33m";
static const char *GREEN  = "\033[1;32m";
static const char *RESET  = "\033[0m";

Ui::Ui(EventLog &log, CommManager &comm, MsSim &ms,
       AccessControl &access, InventoryManager &inventory, DispenseController &dispense,
       ModeManager &modes, FaultMonitor &faults, LockDriver &locks, SimHardware &hw,
       std::function<void()> tickAll, std::function<void(long long)> advanceClock)
    : log_(log), comm_(comm), ms_(ms), access_(access), inventory_(inventory), dispense_(dispense),
      modes_(modes), faults_(faults), locks_(locks), hw_(hw),
      tickAll_(tickAll), advanceClock_(advanceClock)
{
#ifdef _WIN32   // turn on colour codes in the Windows terminal
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (GetConsoleMode(h, &mode)) SetConsoleMode(h, mode | 0x0004);
#endif
}

void Ui::showResult(const std::string &text) { result_ = text; }

void Ui::showResult(const Result &r, const std::string &okText)
{
    showResult(r.ok ? okText : std::string("Rejected: ") + reasonText(r.reason));
}

void Ui::hardware(bool done, const std::string &okText)
{
    showResult(done ? okText : "Not possible (unknown location, or the drawer is locked).");
}

// Severity is never shown by colour alone: colour + text + sound (NFR-USE-02)
void Ui::alarm(const char *colour, const std::string &severity, const std::string &what)
{
    std::cout << colour << "  !! " << severity << ": " << what << RESET << '\a' << '\n';
}

void Ui::refresh()
{
    bool connected = comm_.linkStatus() == LinkStatus::CONNECTED;
    std::cout << "\n================ ADC-19 =================\n"
              << " Mode:   " << (modes_.mode() == Mode::Normal ? GREEN : modes_.mode() == Mode::Fault ? RED : YELLOW)
              << modeText(modes_.mode()) << RESET << '\n'
              << " Link:   " << (connected ? GREEN : YELLOW)
              << (connected ? "CONNECTED" : "DEGRADED") << RESET
              << "   (unsent messages: " << comm_.pendingCount() << ")\n"
              << " User:   " << (access_.currentUser().empty() ? "(none)" : access_.currentUser()) << '\n'
              << " MS has received " << ms_.receivedCount() << " messages; last: "
              << ms_.lastReceived() << '\n';

    // Each location as the sensors report it; unconfirmed states show "unknown" (NFR-SAF-02)
    std::cout << " Bays:  ";
    for (const std::string &l : locks_.locations()) {
        std::cout << ' ' << l << '=' << toString(locks_.state(l)) << '/'
                  << toString(locks_.drawer(l));
        if (faults_.blocked(l)) std::cout << "(BLOCKED)";
    }
    std::cout << '\n';

    // Alarms: comm status from this part, every active fault from FaultMonitor (FR-FLT-03)
    if (!connected)
        alarm(YELLOW, "WARNING", "Communication with Monitoring System lost - cabinet still works locally");
    for (const Fault &f : faults_.active()) {
        if (f.code == "COMM_LOSS") continue;              // already shown on the line above
        alarm(f.severity == Severity::CRITICAL ? RED : YELLOW, toString(f.severity),
              "#" + std::to_string(f.id) + " " + f.code + (f.location.empty() ? "" : " at " + f.location) +
              (f.acknowledged ? " (acknowledged, still present)" : ""));
    }

    std::cout << " Last action: " << result_ << '\n'
              << "==========================================\n";
}

void Ui::showLog(int n)
{
    unsigned last = log_.lastSeq();
    unsigned from = last > static_cast<unsigned>(n) ? last - n + 1 : 1;
    std::cout << " --- last log entries ---\n";
    for (const LogEntry &e : log_.entries(from))
        std::cout << " #" << e.seq << "  t=" << e.timeMs / 1000 << "s  " << toString(e.type)
                  << "  " << toString(e.outcome) << "  user=" << (e.userId.empty() ? "-" : e.userId)
                  << "  " << e.details << '\n';
}

void Ui::help()
{
    std::cout <<
        " Cabinet:   login <badge> <pin> | logout | dispense <med> <qty> <patient>\n"
        "            override <med> <qty> <patient> | confirm <qty> | restock <loc> <med> <lot> <expiry> <qty>\n"
        "            maint | normal | clearfault\n"
        " Network:   cut | restore | perm <badge>,<role>\n"
        " Hardware:  open <loc> | close <loc> | jamlock <loc> | unjam <loc>\n"
        "            sensorfail <loc|bus> | fixsensor <loc|bus>      (locations: A1 A2 A3 B1)\n"
        " Storage:   failstorage | fixstorage\n"
        " Other:     wait <seconds> | log [n] | status | help | quit\n";
}


// One user command -> exactly one call into another module.
bool Ui::handle(const std::string &line)
{
    std::istringstream in(line);
    std::string cmd, a, b, c, d, e;
    in >> cmd >> a >> b >> c >> d >> e;
    if (cmd.empty()) return true;

    // ---- this part (working) ----
    if (cmd == "quit") return false;
    else if (cmd == "help")   { help(); return true; }
    else if (cmd == "status") { /* just redraw */ }
    else if (cmd == "log")    { showLog(a.empty() ? 10 : std::atoi(a.c_str())); return true; }
    else if (cmd == "cut") {
        ms_.setLinkUp(false);
        showResult("Network cut. Link goes DEGRADED after 3 unanswered heartbeats (try: wait 120).");
    }
    else if (cmd == "restore") {
        ms_.setLinkUp(true);
        showResult("Network restored. The next heartbeat reply reconnects and resends the outbox.");
    }
    else if (cmd == "wait") {
        int secs = std::atoi(a.c_str());
        for (int i = 0; i < secs; i++) { advanceClock_(1000); tickAll_(); }
        showResult("Advanced the clock " + a + " s.");
    }
    else if (cmd == "perm") {
        size_t comma = a.find(',');
        std::string body = comma == std::string::npos
            ? "badge=" + a
            : "badge=" + a.substr(0, comma) + ";role=" + a.substr(comma + 1);
        ms_.sendPermissionUpdate(body);
        showResult("Monitoring System sent a permission update.");
    }
    else if (cmd == "failstorage") {
        log_.simulateDiskFailure(true);
        comm_.simulateDiskFailure(true);
        try { log_.append(EventType::ALARM, Outcome::FAILED, "", "check=storage"); }
        catch (const LogStorageError &) { /* expected: row L3 sets isHealthy() = false */ }
        showResult("Disk now fails (demo). The failed write set isHealthy() = false.");
    }
    else if (cmd == "fixstorage") {
        log_.simulateDiskFailure(false);
        comm_.simulateDiskFailure(false);
        showResult("Disk works again; health stays false until a technician clears the fault.");
    }

    // ---- Areeb's modules ----
    else if (cmd == "login") {
        Result r = access_.authenticate(a, b);       // first, so the name below is the new user
        showResult(r, "Logged in as " + access_.currentUser() + ".");
    }
    else if (cmd == "logout")   { access_.logout(); showResult("Logged out."); }
    else if (cmd == "dispense") showResult(dispense_.request(a, std::atoi(b.c_str()), c, false),
                                           "Drawer released. Open it, take the medication, close it, then: confirm <qty>");
    else if (cmd == "override") showResult(dispense_.request(a, std::atoi(b.c_str()), c, true),
                                           "EMERGENCY OVERRIDE granted - alarm sent to the MS. Open, take, close, confirm.");
    else if (cmd == "confirm")  showResult(dispense_.confirmQuantity(std::atoi(a.c_str())), "Dispense complete and logged.");
    else if (cmd == "restock")  showResult(inventory_.replenish(a, b, c, std::atoi(d.c_str()), std::atoi(e.c_str())),
                                           "Restocked " + a + ".");

    // ---- Rawaad's modules: modes and faults (technician = AccessControl's permission) ----
    else if (cmd == "maint")
        showResult(modes_.enterMaintenance(access_.currentUser(), access_.isPermitted(Action::EnterMaintenance).ok,
                                           dispense_.state() != TxState::Idle), "Maintenance mode. All locations locked.");
    else if (cmd == "normal")
        showResult(faults_.returnToNormal(access_.currentUser(), access_.isPermitted(Action::ClearFault).ok,
                                          dispense_.state() != TxState::Idle), "Self-test passed. Back to NORMAL.");
    else if (cmd == "clearfault")
        showResult(faults_.clearFaults(access_.currentUser(), access_.isPermitted(Action::ClearFault).ok),
                   "Faults cleared. Type 'normal' to run the self-test.");

    // ---- Rawaad's simulated hardware (the TAs poke the cabinet with these) ----
    else if (cmd == "open")      hardware(hw_.openDrawer(a), "Drawer " + a + " opened.");
    else if (cmd == "close")     hardware(hw_.closeDrawer(a), "Drawer " + a + " closed.");
    else if (cmd == "jamlock")   hardware(hw_.jamLock(a, true), "Lock " + a + " is now jammed.");
    else if (cmd == "unjam")     hardware(hw_.jamLock(a, false), "Lock " + a + " repaired (a technician clears the fault).");
    else if (cmd == "sensorfail" && a == "bus") { hw_.failBus(true); showResult("Shared sensor failed."); }
    else if (cmd == "fixsensor"  && a == "bus") { hw_.failBus(false); showResult("Shared sensor repaired."); }
    else if (cmd == "sensorfail") hardware(hw_.failSensor(a, true), "Sensors at " + a + " now give invalid readings.");
    else if (cmd == "fixsensor")  hardware(hw_.failSensor(a, false), "Sensors at " + a + " repaired.");

    else showResult("Unknown command. Type: help");
    return true;
}

void Ui::run()
{
    std::time_t last = std::time(nullptr);
    help();
    refresh();
    std::string line;
    while (true) {
        std::cout << "> " << std::flush;
        if (!std::getline(std::cin, line)) break;
        for (std::time_t t = std::time(nullptr); last < t; last++) tickAll_();   // real seconds spent typing
        if (!handle(line)) break;
        tickAll_();          // check the cabinet right after the command, so faults show at once
        refresh();
    }
}