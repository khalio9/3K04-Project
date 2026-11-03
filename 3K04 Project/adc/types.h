/* types.h - shared enums and default parameters (M3.3 Types)
 * Secret: where the defaults are set.
 * Owner: Omer
 */
#ifndef ADC_TYPES_H
#define ADC_TYPES_H

/* Event types that must be logged (FR-LOG-01) */
typedef enum {
    EVT_AUTH_ATTEMPT = 0,
    EVT_CREDENTIAL_LOCKOUT,
    EVT_DISPENSE,
    EVT_REPLENISH,
    EVT_EMERGENCY_OVERRIDE,
    EVT_ALARM,
    EVT_FAULT_RAISED,
    EVT_FAULT_CLEARED,
    EVT_MODE_CHANGE,
    EVT_COMM_LOST,
    EVT_COMM_RESTORED,
    EVT_COUNT
} EventType;

/* Outcome of a logged event (FR-LOG-02) */
typedef enum {
    OUT_SUCCESS = 0,
    OUT_FAILED,
    OUT_REJECTED,
    OUT_GRANTED,
    OUT_COMPLETE,
    OUT_COMPLETE_MISMATCH,
    OUT_ABANDONED,
    OUT_COUNT
} Outcome;

/* Time source supplied by the Clock module: milliseconds, never decreasing (NAT-2) */
typedef long long (*NowMsFn)(void);

/* Default parameters (A11: placeholders, not clinically validated) */
#define ADC_ID_MAX              16
#define USER_ID_MAX             32
#define DETAILS_MAX             256
#define HEARTBEAT_PERIOD_MS     30000   /* FR-COM-03 */
#define MISSED_HEARTBEAT_LIMIT  3       /* Table 1: comm loss */
#define RETRY_INTERVAL_MS       5000    /* resend unACKed messages */

const char *event_type_name(EventType t);
const char *outcome_name(Outcome o);

#endif