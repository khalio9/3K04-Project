/* event_log.h - EventLog (M3.1): the append-only audit trail
 *
 * Responsibility: keep the append-only audit trail of every FR-LOG-01 event.
 * Secret: log file format, hash chain, and how durability is achieved.
 *         (Everything about these lives in event_log.c as static data.)
 * Spec: SPEC-LOG-1 (append), SPEC-LOG-2 (reads), SPEC-LOG-3 (load).
 * Owner: Omer
 */
#ifndef ADC_EVENT_LOG_H
#define ADC_EVENT_LOG_H

#include <stdbool.h>
#include "types.h"

/* One log record (FR-LOG-02). The hash is NOT here: it is part of the secret. */
typedef struct {
    unsigned  seq;
    long long time_ms;
    char      adc_id[ADC_ID_MAX];
    EventType event_type;
    char      user_id[USER_ID_MAX];   /* "" for system events (LOG-I4) */
    Outcome   outcome;
    char      details[DETAILS_MAX];   /* "key=value;key=value" */
} LogEntry;

/* Result codes (C has no exceptions) */
typedef enum {
    LOG_OK            =  0,   /* row L4 */
    LOG_ERR_INVALID   = -1,   /* row L1: unknown event type or outcome      (ValueError) */
    LOG_ERR_FORBIDDEN = -2,   /* row L2: secret or patient data in details  (ValueError) */
    LOG_ERR_STORAGE   = -3    /* row L3: the write failed                   (LogStorageError) */
} LogStatus;

/* Constructor. Loads any existing log as in SPEC-LOG-3 (rows G1-G4).
 * Returns 0, or -1 if out of memory. A damaged file is NOT an error here:
 * it sets is_healthy() to false (row G4). */
int event_log_init(const char *adc_id, const char *storage_path, NowMsFn now_ms);

/* SPEC-LOG-1. Adds one durable record with the next seq and the current time.
 * user_id and details may be NULL. out may be NULL.
 * On any error nothing is added; the caller must leave its own state unchanged. */
LogStatus event_log_append(EventType event_type, Outcome outcome,
                           const char *user_id, const char *details,
                           LogEntry *out);

/* SPEC-LOG-2. Copies entries with from_seq <= seq <= to_seq (to_seq = 0 means "to the end")
 * into out[0..max-1]. Returns how many were copied. Read only. */
int event_log_entries(unsigned from_seq, unsigned to_seq, LogEntry *out, int max);

/* Sequence number of the newest entry; 0 if the log is empty. */
unsigned event_log_last_seq(void);

/* False after a failed write or a failed integrity check at load (row G4).
 * Read by FaultMonitor to detect the Table 1 "Storage error". */
bool event_log_is_healthy(void);

/* Frees memory and closes the file. */
void event_log_close(void);

/* There is deliberately no function to edit or delete an entry (FR-LOG-03). */

#endif