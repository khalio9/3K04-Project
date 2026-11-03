/* comm_protocol.h - PRIVATE part of CommManager: the wire format.
 *
 * This is CommManager's secret (message format + message authentication).
 * Only comm_manager.c and ms_sim.c (which stands in for Part B's side of the
 * agreed protocol) include this file. No other module may include it.
 * If Part B changes the format, only comm_protocol.c changes.
 */
#ifndef ADC_COMM_PROTOCOL_H
#define ADC_COMM_PROTOCOL_H

#include <stdbool.h>
#include "comm_manager.h"

#define WIRE_MAX   1024
#define BODY_MAX   512

typedef struct {
    MessageType type;
    char        adc_id[ADC_ID_MAX];
    unsigned    seq;
    long long   time_ms;
    char        body[BODY_MAX];
} WireMsg;

/* Builds one message as text. Returns its length. */
int  proto_encode(const WireMsg *m, char *out, int cap);

/* Parses and authenticates one message. Returns false if it is malformed
 * or fails authentication (SPEC-COM-3 row X1). */
bool proto_decode(const char *raw, WireMsg *m);

const char *proto_type_name(MessageType t);

/* Reads "key=value" out of a body like "ack_seq=12;result=OK". Returns false if absent. */
bool proto_body_get(const char *body, const char *key, char *out, int cap);

#endif