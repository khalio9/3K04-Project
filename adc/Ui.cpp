// Ui.cpp - terminal screen + demo controls.
// Swapping this for a touchscreen GUI changes only this file.
// Commands marked TODO call Areeb's / Rawaad's modules: replace each notWired(...)
// with one call to their public function.
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
       std::function<void()> tickAll, std::function<void(long long)> advanceClock)
    : log_(log), comm_(comm), ms_(ms), tickAll_(tickAll), advanceClock_(advanceClock)
{
#ifdef _WIN32   // turn on colour codes in the Windows terminal
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (GetConsoleMode(h, &mode)) SetConsoleMode(h, mode | 0x0004);
#endif
}

void Ui::showResult(const std::string &text) { result_ = text; }

// Severity is never shown by colour alone: colour + text + sound (NFR-USE-02)
void Ui::alarm(const char *colour, const std::string &severity, const std::string &what)
{
    std::cout << colour << "  !! " << severity << ": " << what << RESET << '\a' << '\n';
}

void Ui::refresh()
{
    bool connected = comm_.linkStatus() == LinkStatus::CONNECTED;
    std::cout << "\n================ ADC-19 =================\n"
              << " Mode:   " << "NORMAL" << '\n'                        // TODO Rawaad: ModeManager
              << " Link:   " << (connected ? GREEN : YELLOW)
              << (connected ? "CONNECTED" : "DEGRADED") << RESET
              << "   (unsent messages: " << comm_.pendingCount() << ")\n"
              << " User:   " << "(none)" << '\n'                        // TODO Areeb: current user
              << " MS has received " << ms_.receivedCount() << " messages; last: "
              << ms_.lastReceived() << '\n';

    // Alarms from this part. Rawaad's FaultMonitor alarms go here too.
    if (!connected)
        alarm(YELLOW, "WARNING", "Communication with Monitoring System lost - cabinet still works locally");
    if (!log_.isHealthy() || !comm_.isHealthy())
        alarm(RED, "CRITICAL", "Storage error - cabinet must enter Fault mode");

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
        " Hardware:  open <loc> | close <loc> | jamlock <loc> | sensorfail <loc>\n"
        " Storage:   failstorage | fixstorage\n"
        " Other:     wait <seconds> | log [n] | status | help | quit\n";
}

void Ui::notWired(const std::string &who) { showResult("Not wired yet (" + who + "'s module)."); }

// One user command -> exactly one call into another module.
bool Ui::handle(const std::string &line)
{
    std::istringstream in(line);
    std::string cmd, a, b, c;
    in >> cmd >> a >> b >> c;
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

    // ---- Areeb's modules (TODO: replace each notWired with one call) ----
    else if (cmd == "login")    notWired("Areeb");   // AccessControl authenticate(a, b)
    else if (cmd == "logout")   notWired("Areeb");   // AccessControl logout()
    else if (cmd == "dispense") notWired("Areeb");   // DispenseController requestDispense(..., false)
    else if (cmd == "override") notWired("Areeb");   // DispenseController requestDispense(..., true)
    else if (cmd == "confirm")  notWired("Areeb");   // DispenseController confirmQuantity(atoi(a))
    else if (cmd == "restock")  notWired("Areeb");   // InventoryManager replenish(...)

    // ---- Rawaad's modules (TODO) ----
    else if (cmd == "maint")      notWired("Rawaad"); // ModeManager: enter Maintenance
    else if (cmd == "normal")     notWired("Rawaad"); // ModeManager: back to Normal
    else if (cmd == "clearfault") notWired("Rawaad"); // FaultMonitor: clear
    else if (cmd == "open")       notWired("Rawaad"); // simulated drawer: open
    else if (cmd == "close")      notWired("Rawaad"); // simulated drawer: close
    else if (cmd == "jamlock")    notWired("Rawaad"); // simulated lock: stuck
    else if (cmd == "sensorfail") notWired("Rawaad"); // simulated sensor: no reading

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
        refresh();
    }
}