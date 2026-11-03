/* ui.h - UI (M1.3): the cabinet screen and the demo harness
 *
 * Responsibility: show mode, comm status, prompts, verdicts and alarms; pass each
 *                 user action to exactly one exported operation of another module.
 * Secret: screen layout, input handling, and how alarms and severity are presented.
 * No other module calls the UI (top of the uses hierarchy).
 * Owner: Omer
 */
#ifndef ADC_UI_H
#define ADC_UI_H

/* tick_fn: advances every module by one second (supplied by main.c). */
void ui_init(void (*tick_fn)(void), void (*advance_clock_ms)(long long ms));

/* Runs the screen until the user types "quit". Called once by main.c. */
void ui_run(void);

/* Redraws the status panel from the modules' exported read operations (REQ-UI-1).
 * Never changes the state of another module. */
void ui_refresh(void);

/* Shows the verdict of the last user action, e.g. "Rejected: EXPIRED". */
void ui_show_result(const char *text);

#endif