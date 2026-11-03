/* main.c - starts every module, wires them together, runs the 1-second tick.
 *
 * Wiring (who gets which function) lives ONLY here, so the modules themselves
 * do not depend on each other more than the uses hierarchy allows.
 * TODO lines are where Areeb's and Rawaad's modules are started and ticked.
 */
#include <time.h>

#include "adc/comm_manager.h"
#include "adc/event_log.h"
#include "adc/ms_sim.h"
#include "adc/ui.h"

#define ADC_ID "ADC-19"

/* ---- demo clock: seconds since start, plus whatever "wait" fast-forwards ----
 * TODO Rawaad: replace with the Clock module's now function if he has one. */
static time_t    s_start;
static long long s_offset_ms = 0;
static long long clock_now_ms(void) { return (long long)(time(NULL) - s_start) * 1000 + s_offset_ms; }
static void      clock_advance_ms(long long ms) { s_offset_ms += ms; }

/* ---- incoming MS messages -> the module that owns them ---- */
static int on_permission_update(const char *body)
{
    (void)body;
    /* TODO Areeb: return his apply_permission_update(...) result (0 = applied). */
    return 0;
}

/* ---- one second of the whole cabinet (N2: at least once per second) ---- */
static void tick_all(void)
{
    /* TODO Areeb:  dispense controller tick */
    /* TODO Rawaad: fault monitor tick (reads event_log_is_healthy(), comm_is_healthy()) */
    comm_tick();
    ms_sim_pump();
}

int main(void)
{
    s_start = time(NULL);

    event_log_init(ADC_ID, "adc_log.txt", clock_now_ms);
    ms_sim_init(ADC_ID, clock_now_ms);
    comm_init(ADC_ID, "adc_outbox.txt", clock_now_ms, ms_sim_transport, 0, 0, 0);
    comm_register_handler(MSG_PERMISSION_UPDATE, on_permission_update);
    /* TODO: init Areeb's and Rawaad's modules here */

    ui_init(tick_all, clock_advance_ms);
    tick_all();                       /* first heartbeat goes out at once */
    ui_run();

    comm_close();
    event_log_close();
    return 0;
}