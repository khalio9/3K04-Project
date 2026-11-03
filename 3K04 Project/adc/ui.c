/* ui.c - UI implementation (terminal screen + demo controls)
 *
 * Secret: the layout below, the command words, and how severity is shown
 * (colour + text + bell, NFR-USE-02). Swapping this for a touchscreen GUI
 * changes only this file.
 *
 * Commands marked TODO call Areeb's / Rawaad's modules. Replace each
 * "not wired yet" with one call to their exported function.
 */
#include "ui.h"
#include "comm_manager.h"
#include "event_log.h"
#include "ms_sim.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#endif

/* ANSI colours (severity is never shown by colour alone: text + bell too) */
#define RED    "\033[1;31m"
#define YELLOW "\033[1;33m"
#define GREEN  "\033[1;32m"
#define RESET  "\033[0m"

static void (*s_tick)(void);
static void (*s_advance)(long long);
static char s_result[200] = "Ready.";

#ifdef ADC_SIM
void event_log_sim_fail_writes(bool fail);
void comm_sim_fail_writes(bool fail);
#endif

void ui_init(void (*tick_fn)(void), void (*advance_clock_ms)(long long))
{
    s_tick = tick_fn;
    s_advance = advance_clock_ms;
#ifdef _WIN32   /* turn on colour codes in the Windows terminal */
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (GetConsoleMode(h, &mode)) SetConsoleMode(h, mode | 0x0004);
#endif
}

void ui_show_result(const char *text) { snprintf(s_result, sizeof s_result, "%s", text); }

static void alarm_line(const char *colour, const char *severity, const char *what)
{
    printf("%s  !! %s: %s%s\a\n", colour, severity, what, RESET);   /* colour + text + sound */
}

void ui_refresh(void)
{
    bool connected = comm_link_status() == LINK_CONNECTED;
    printf("\n================ ADC-19 =================\n");
    printf(" Mode:   %s\n", "NORMAL");   /* TODO Rawaad: mode name from ModeManager */
    printf(" Link:   %s%s%s   (unsent messages: %d)\n",
           connected ? GREEN : YELLOW, connected ? "CONNECTED" : "DEGRADED", RESET,
           comm_pending_count());
    printf(" User:   %s\n", "(none)");   /* TODO Areeb: current session's user */
    printf(" MS has received %d messages; last: %s\n",
           ms_sim_received_count(), ms_sim_last_received());

    /* Alarms that belong to this part. Rawaad's FaultMonitor alarms go here too. */
    if (!connected)
        alarm_line(YELLOW, "WARNING", "Communication with Monitoring System lost - cabinet still works locally");
    if (!event_log_is_healthy() || !comm_is_healthy())
        alarm_line(RED, "CRITICAL", "Storage error - cabinet must enter Fault mode");

    printf(" Last action: %s\n", s_result);
    printf("==========================================\n");
}

static void show_log(int n)
{
    unsigned last = event_log_last_seq();
    unsigned from = last > (unsigned)n ? last - (unsigned)n + 1 : 1;
    LogEntry e[50];
    int got = event_log_entries(from, 0, e, n > 50 ? 50 : n);
    printf(" --- last %d log entries ---\n", got);
    for (int i = 0; i < got; i++)
        printf(" #%u  t=%ds  %-18s %-17s user=%-8s %s\n", e[i].seq, (int)(e[i].time_ms / 1000),
               event_type_name(e[i].event_type), outcome_name(e[i].outcome),
               e[i].user_id[0] ? e[i].user_id : "-", e[i].details);
}

static void help(void)
{
    printf(
        " Cabinet:   login <badge> <pin> | logout | dispense <med> <qty> <patient>\n"
        "            override <med> <qty> <patient> | confirm <qty> | restock <loc> <med> <lot> <expiry> <qty>\n"
        "            maint | normal | clearfault\n"
        " Network:   cut | restore | perm <badge>,<role>\n"
        " Hardware:  open <loc> | close <loc> | jamlock <loc> | sensorfail <loc>\n"
#ifdef ADC_SIM
        " Storage:   failstorage | fixstorage\n"
#endif
        " Other:     wait <seconds> | log [n] | status | help | quit\n");
}

static void not_wired(const char *who) { char b[120]; snprintf(b, sizeof b, "Not wired yet (%s's module).", who); ui_show_result(b); }

/* One user command -> exactly one call into another module. */
static bool handle(char *line)
{
    char cmd[32] = "", a[64] = "", b[64] = "", c[64] = "";
    sscanf(line, "%31s %63s %63s %63s", cmd, a, b, c);
    if (cmd[0] == '\0') return true;

    /* ---- this part (working) ---- */
    if (!strcmp(cmd, "quit")) return false;
    else if (!strcmp(cmd, "help")) { help(); return true; }
    else if (!strcmp(cmd, "status")) { /* just redraw */ }
    else if (!strcmp(cmd, "log")) { show_log(a[0] ? atoi(a) : 10); return true; }
    else if (!strcmp(cmd, "cut")) { ms_sim_set_link(false); ui_show_result("Network cut. Link goes DEGRADED after 3 unanswered heartbeats (try: wait 120)."); }
    else if (!strcmp(cmd, "restore")) { ms_sim_set_link(true); ui_show_result("Network restored. Next heartbeat reply reconnects and resends the outbox."); }
    else if (!strcmp(cmd, "wait")) {
        int secs = atoi(a);
        for (int i = 0; i < secs; i++) { s_advance(1000); s_tick(); }
        char r[64]; snprintf(r, sizeof r, "Advanced the clock %d s.", secs); ui_show_result(r);
    }
    else if (!strcmp(cmd, "perm")) {
        char body[128];
        char *comma = strchr(a, ',');
        if (comma) { *comma = '\0'; snprintf(body, sizeof body, "badge=%s;role=%s", a, comma + 1); }
        else snprintf(body, sizeof body, "badge=%s", a);
        ms_sim_send_permission_update(body);
        ui_show_result("Monitoring System sent a permission update.");
    }
#ifdef ADC_SIM
    else if (!strcmp(cmd, "failstorage")) { event_log_sim_fail_writes(true); comm_sim_fail_writes(true);
        event_log_append(EVT_ALARM, OUT_FAILED, NULL, "check=storage", NULL);   /* this write fails (row L3) */
        ui_show_result("Disk now fails (demo). The failed write set is_healthy() = false."); }
    else if (!strcmp(cmd, "fixstorage")) { event_log_sim_fail_writes(false); comm_sim_fail_writes(false);
        ui_show_result("Storage writes work again; health stays false until a technician clears the fault."); }
#endif

    /* ---- Areeb's modules (TODO: replace each not_wired with one call) ---- */
    else if (!strcmp(cmd, "login"))    not_wired("Areeb");   /* AccessControl authenticate(a, b) */
    else if (!strcmp(cmd, "logout"))   not_wired("Areeb");   /* AccessControl logout() */
    else if (!strcmp(cmd, "dispense")) not_wired("Areeb");   /* DispenseController request_dispense(..., emergency=false) */
    else if (!strcmp(cmd, "override")) not_wired("Areeb");   /* DispenseController request_dispense(..., emergency=true) */
    else if (!strcmp(cmd, "confirm"))  not_wired("Areeb");   /* DispenseController confirm_quantity(atoi(a)) */
    else if (!strcmp(cmd, "restock"))  not_wired("Areeb");   /* InventoryManager replenish(...) */

    /* ---- Rawaad's modules (TODO) ---- */
    else if (!strcmp(cmd, "maint"))      not_wired("Rawaad"); /* ModeManager request Maintenance */
    else if (!strcmp(cmd, "normal"))     not_wired("Rawaad"); /* ModeManager return to Normal */
    else if (!strcmp(cmd, "clearfault")) not_wired("Rawaad"); /* FaultMonitor clear */
    else if (!strcmp(cmd, "open"))       not_wired("Rawaad"); /* simulated drawer: open */
    else if (!strcmp(cmd, "close"))      not_wired("Rawaad"); /* simulated drawer: close */
    else if (!strcmp(cmd, "jamlock"))    not_wired("Rawaad"); /* simulated lock: stuck */
    else if (!strcmp(cmd, "sensorfail")) not_wired("Rawaad"); /* simulated sensor: no reading */

    else { ui_show_result("Unknown command. Type: help"); }
    return true;
}

void ui_run(void)
{
    char line[256];
    time_t last = time(NULL);
    help();
    ui_refresh();
    for (;;) {
        printf("> ");
        fflush(stdout);
        if (!fgets(line, sizeof line, stdin)) break;
        time_t t = time(NULL);                 /* catch up the real seconds spent typing */
        for (; last < t; last++) s_tick();
        if (!handle(line)) break;
        ui_refresh();
    }
}