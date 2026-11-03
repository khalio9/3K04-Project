/* types.c - names for the shared enums (used in log files, messages and the UI) */
#include "types.h"

static const char *EVENT_NAMES[EVT_COUNT] = {
    "AUTH_ATTEMPT", "CREDENTIAL_LOCKOUT", "DISPENSE", "REPLENISH",
    "EMERGENCY_OVERRIDE", "ALARM", "FAULT_RAISED", "FAULT_CLEARED",
    "MODE_CHANGE", "COMM_LOST", "COMM_RESTORED"
};

static const char *OUTCOME_NAMES[OUT_COUNT] = {
    "SUCCESS", "FAILED", "REJECTED", "GRANTED",
    "COMPLETE", "COMPLETE_MISMATCH", "ABANDONED"
};

const char *event_type_name(EventType t)
{
    return (t >= 0 && t < EVT_COUNT) ? EVENT_NAMES[t] : "UNKNOWN";
}

const char *outcome_name(Outcome o)
{
    return (o >= 0 && o < OUT_COUNT) ? OUTCOME_NAMES[o] : "UNKNOWN";
}