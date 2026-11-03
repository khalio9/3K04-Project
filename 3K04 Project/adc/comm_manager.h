/* comm_manager.h - CommManager (M3.2): messages to and from the Monitoring System
 *
 * Responsibility: exchange messages with the MS and keep them while the link is down
 *                 (FR-COM-01 to -09).
 * Secret: message format (comm_protocol.c), network transport, heartbeat and retry
 *         policy, and how the outbox is stored.
 * Spec: SPEC-COM-1 (send), SPEC-COM-2 (tick), SPEC-COM-3 (receive), SPEC-COM-4 (load).
 * Owner: Omer
 */
#ifndef ADC_COMM_MANAGER_H
#define ADC_COMM_MANAGER_H

#include <stdbool.h>
#include "types.h"

typedef enum {
    /* ADC -> MS */
    MSG_HEARTBEAT = 0, MSG_STATUS, MSG_INVENTORY, MSG_ALARM,
    MSG_MAINTENANCE, MSG_HEALTH, MSG_EVENT,
    /* MS -> ADC */
    MSG_HEARTBEAT_REPLY, MSG_PERMISSION_UPDATE, MSG_SCHEDULE, MSG_CONFIG_CHANGE,
    /* both directions */
    MSG_ACK,
    MSG_COUNT
} MessageType;

typedef enum { LINK_DEGRADED = 0, LINK_CONNECTED = 1 } LinkStatus;

typedef enum {
    COMM_ERR_INVALID = -1,   /* row S1: not an outgoing type   (ValueError) */
    COMM_ERR_STORAGE = -2    /* row S2: outbox write failed    (CommStorageError) */
} CommError;

/* The network link. Hands bytes to the network; returns 0 if handed over.
 * Delivery is not guaranteed (OUT-5): only an ACK proves delivery.
 * The real link or the simulated MS (ms_sim.c) is plugged in here. */
typedef int (*TransportSendFn)(const char *bytes, int len);

/* Applies an incoming PERMISSION_UPDATE, SCHEDULE or CONFIG_CHANGE body.
 * Returns 0 if applied, non-zero if rejected (nothing applied). */
typedef int (*CommHandler)(const char *body);

/* Constructor. Restores the outbox as in SPEC-COM-4. The link starts DEGRADED
 * until the first heartbeat reply (NFR-SAF-02). Pass 0 for the default
 * heartbeat_ms / missed_limit / retry_ms. Returns 0. */
int comm_init(const char *adc_id, const char *storage_path, NowMsFn now_ms,
              TransportSendFn transport, long long heartbeat_ms,
              int missed_limit, long long retry_ms);

/* SPEC-COM-1. Stores the message (with ADC ID and next seq) in the outbox and,
 * if CONNECTED, transmits it at once. It stays in the outbox until ACKed.
 * body is "key=value;key=value" (may be NULL).
 * Returns the seq (> 0), or COMM_ERR_INVALID / COMM_ERR_STORAGE (nothing queued). */
int comm_send(MessageType type, const char *body);

/* SPEC-COM-2. Call at least once per second (N2). Heartbeats, loss detection,
 * forwarding new log entries as EVENT messages, retransmission. */
void comm_tick(void);

/* SPEC-COM-3. Call when bytes arrive from the transport. Never fails:
 * bad or duplicate messages are discarded or re-ACKed. */
void comm_receive(const char *raw);

/* Startup wiring in main.c. Returns 0, or -1 if the type is not an incoming
 * type or already has a handler. */
int comm_register_handler(MessageType type, CommHandler handler);

/* Read only. Shown on screen at all times (FR-MOD-04). */
LinkStatus comm_link_status(void);

/* Read only. Number of unACKed messages in the outbox. */
int comm_pending_count(void);

/* False if the outbox storage failed to load or write. Read by FaultMonitor. */
bool comm_is_healthy(void);

void comm_close(void);

/* No operation changes the operating mode, the lock state or the allowed actions (COM-I4). */

#endif