/* comm_manager.c - CommManager implementation
 *
 * Everything `static` here is the module's SECRET:
 *   - heartbeat and retry policy (s_heartbeat_ms, s_missed_limit, s_retry_ms)
 *   - the outbox and how it is stored on disk (s_outbox, save_outbox)
 *   - the transport that is plugged in (s_transport)
 * The message format itself is in comm_protocol.c (also private to this module).
 */
#include "comm_manager.h"
#include "comm_protocol.h"
#include "event_log.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    unsigned    seq;
    MessageType type;
    long long   time_ms;
    long long   last_sent;      /* not saved: after a restart everything is resent */
    char        body[BODY_MAX];
} OutMsg;

/* ---------- private state ---------- */
static char            s_adc_id[ADC_ID_MAX];
static char            s_path[512];
static NowMsFn         s_now;
static TransportSendFn s_transport;
static long long       s_heartbeat_ms, s_retry_ms;
static int             s_missed_limit;

static LinkStatus      s_link = LINK_DEGRADED;
static int             s_missed = 0;
static bool            s_awaiting_reply = false;
static long long       s_last_heartbeat = 0;
static unsigned        s_next_seq = 1;
static unsigned        s_last_rx_seq = 0;
static unsigned        s_last_fwd_log_seq = 0;
static bool            s_healthy = true;

static OutMsg         *s_outbox = NULL;
static int             s_out_count = 0, s_out_cap = 0;
static CommHandler     s_handlers[MSG_COUNT];

#ifdef ADC_SIM
static bool s_sim_fail_writes = false;   /* demo hook: pretend the outbox disk failed */
void comm_sim_fail_writes(bool fail) { s_sim_fail_writes = fail; }
#endif

static long long now(void) { return s_now ? s_now() : 0; }

static bool is_outgoing(MessageType t) { return t >= MSG_HEARTBEAT && t <= MSG_EVENT; }
static bool is_handled_incoming(MessageType t)
{
    return t == MSG_PERMISSION_UPDATE || t == MSG_SCHEDULE || t == MSG_CONFIG_CHANGE;
}

/* ---------- outbox storage (temp file, then replace) ---------- */
static bool save_outbox(void)
{
#ifdef ADC_SIM
    if (s_sim_fail_writes) return false;
#endif
    char tmp[600];
    snprintf(tmp, sizeof tmp, "%s.tmp", s_path);
    FILE *f = fopen(tmp, "wb");
    if (!f) return false;
    bool ok = fprintf(f, "H|%u|%u|%u\n", s_next_seq, s_last_rx_seq, s_last_fwd_log_seq) > 0;
    for (int i = 0; ok && i < s_out_count; i++)
        ok = fprintf(f, "%u|%d|%" PRId64 "|%s\n", s_outbox[i].seq, (int)s_outbox[i].type,
                     (int64_t)s_outbox[i].time_ms, s_outbox[i].body) > 0;
    ok = (fclose(f) == 0) && ok;
    if (!ok) { remove(tmp); return false; }
    remove(s_path);                        /* Windows rename() will not overwrite */
    return rename(tmp, s_path) == 0;
}

static bool push_out(const OutMsg *m)
{
    if (s_out_count == s_out_cap) {
        int cap = s_out_cap ? s_out_cap * 2 : 64;
        OutMsg *n = realloc(s_outbox, (size_t)cap * sizeof *n);
        if (!n) return false;
        s_outbox = n;
        s_out_cap = cap;
    }
    s_outbox[s_out_count++] = *m;
    return true;
}

/* SPEC-COM-4: load */
static void load_outbox(void)
{
    FILE *f = fopen(s_path, "rb");
    if (!f) return;                                      /* nothing saved yet */
    char line[BODY_MAX + 128];
    bool header = false;
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (!header) {
            if (sscanf(line, "H|%u|%u|%u", &s_next_seq, &s_last_rx_seq, &s_last_fwd_log_seq) != 3) {
                s_healthy = false;
                break;
            }
            header = true;
            continue;
        }
        OutMsg m;
        memset(&m, 0, sizeof m);
        int type;
        int64_t t;
        char body[BODY_MAX] = "";
        int n = sscanf(line, "%u|%d|%" SCNd64 "|%511[^\n]", &m.seq, &type, &t, body);
        if (n < 3 || type < 0 || type >= MSG_COUNT) { s_healthy = false; break; }
        m.type = (MessageType)type;
        m.time_ms = t;
        snprintf(m.body, sizeof m.body, "%s", body);
        m.last_sent = -1;
        if (!push_out(&m)) { s_healthy = false; break; }
    }
    fclose(f);
}

/* ---------- transmit ---------- */
static void transmit(MessageType type, unsigned seq, long long t, const char *body)
{
    WireMsg w;
    memset(&w, 0, sizeof w);
    w.type = type;
    snprintf(w.adc_id, sizeof w.adc_id, "%s", s_adc_id);
    w.seq = seq;
    w.time_ms = t;
    snprintf(w.body, sizeof w.body, "%s", body ? body : "");
    char raw[WIRE_MAX];
    int len = proto_encode(&w, raw, sizeof raw);
    if (s_transport) s_transport(raw, len);
}

/* Messages that are never stored in the outbox (heartbeat, ACK) still get a seq (FR-COM-02). */
static void transmit_unstored(MessageType type, const char *body)
{
    unsigned seq = s_next_seq++;
    transmit(type, seq, now(), body);
}

static void retransmit(bool only_due)
{
    long long t = now();
    for (int i = 0; i < s_out_count; i++) {             /* outbox is in seq order (COM-I3) */
        OutMsg *m = &s_outbox[i];
        if (only_due && m->last_sent >= 0 && t - m->last_sent < s_retry_ms) continue;
        transmit(m->type, m->seq, m->time_ms, m->body);
        m->last_sent = t;
    }
}

static void send_ack(unsigned seq, bool ok)
{
    char body[64];
    snprintf(body, sizeof body, "ack_seq=%u;result=%s", seq, ok ? "OK" : "REJECTED");
    transmit_unstored(MSG_ACK, body);
}

/* ---------- public operations ---------- */
int comm_init(const char *adc_id, const char *storage_path, NowMsFn now_ms,
              TransportSendFn transport, long long heartbeat_ms,
              int missed_limit, long long retry_ms)
{
    comm_close();
    snprintf(s_adc_id, sizeof s_adc_id, "%s", adc_id);
    snprintf(s_path, sizeof s_path, "%s", storage_path);
    s_now = now_ms;
    s_transport = transport;
    s_heartbeat_ms = heartbeat_ms > 0 ? heartbeat_ms : HEARTBEAT_PERIOD_MS;
    s_missed_limit = missed_limit > 0 ? missed_limit : MISSED_HEARTBEAT_LIMIT;
    s_retry_ms     = retry_ms > 0 ? retry_ms : RETRY_INTERVAL_MS;

    s_link = LINK_DEGRADED;                 /* unconfirmed until a reply (NFR-SAF-02) */
    s_missed = 0;
    s_awaiting_reply = false;
    s_next_seq = 1;
    s_last_rx_seq = 0;
    s_last_fwd_log_seq = 0;
    s_healthy = true;
    memset(s_handlers, 0, sizeof s_handlers);
    load_outbox();
    s_last_heartbeat = now() - s_heartbeat_ms;   /* first tick sends a heartbeat at once */
    return 0;
}

int comm_send(MessageType type, const char *body)
{
    if (!is_outgoing(type) || type == MSG_HEARTBEAT)       /* row S1 */
        return COMM_ERR_INVALID;

    OutMsg m;
    memset(&m, 0, sizeof m);
    m.seq = s_next_seq;
    m.type = type;
    m.time_ms = now();
    m.last_sent = -1;
    snprintf(m.body, sizeof m.body, "%s", body ? body : "");

    if (!push_out(&m)) return COMM_ERR_STORAGE;
    s_next_seq++;
    if (!save_outbox()) {                                  /* row S2: undo, nothing queued */
        s_out_count--;
        s_next_seq--;
        s_healthy = false;
        return COMM_ERR_STORAGE;
    }
    if (s_link == LINK_CONNECTED) {                        /* row S4 */
        OutMsg *stored = &s_outbox[s_out_count - 1];
        transmit(stored->type, stored->seq, stored->time_ms, stored->body);
        stored->last_sent = now();
    }
    return (int)m.seq;                                     /* row S3 or S4 */
}

void comm_tick(void)
{
    long long t = now();

    /* Row K1: heartbeat */
    if (t - s_last_heartbeat >= s_heartbeat_ms) {
        if (s_awaiting_reply) s_missed++;
        char body[64];
        snprintf(body, sizeof body, "pending=%d", s_out_count);
        transmit_unstored(MSG_HEARTBEAT, body);
        s_awaiting_reply = true;
        s_last_heartbeat = t;
    }

    /* Row K2: link loss */
    if (s_link == LINK_CONNECTED && s_missed >= s_missed_limit) {
        s_link = LINK_DEGRADED;
        event_log_append(EVT_COMM_LOST, OUT_SUCCESS, NULL, "missed_heartbeats=3", NULL);
    }

    /* Row K3: forward new log entries as EVENT messages */
    unsigned last = event_log_last_seq();
    while (s_last_fwd_log_seq < last) {
        LogEntry e;
        if (event_log_entries(s_last_fwd_log_seq + 1, s_last_fwd_log_seq + 1, &e, 1) != 1) break;
        char body[BODY_MAX];
        snprintf(body, sizeof body, "log_seq=%u;event=%s;user=%s;outcome=%s;%s",
                 e.seq, event_type_name(e.event_type), e.user_id,
                 outcome_name(e.outcome), e.details);
        s_last_fwd_log_seq = e.seq;
        if (comm_send(MSG_EVENT, body) < 0) {              /* storage failed: retry next tick */
            s_last_fwd_log_seq = e.seq - 1;
            break;
        }
    }

    /* Row K4: retransmit unACKed messages that are due */
    if (s_link == LINK_CONNECTED) retransmit(true);
}

void comm_receive(const char *raw)
{
    WireMsg m;
    /* Row X1: malformed, fails authentication, or not for this ADC */
    if (!proto_decode(raw, &m) || strcmp(m.adc_id, s_adc_id) != 0) return;

    /* Row X2: duplicate */
    if (m.type != MSG_HEARTBEAT_REPLY && m.type != MSG_ACK && m.seq <= s_last_rx_seq) {
        send_ack(m.seq, true);
        return;
    }

    switch (m.type) {
    case MSG_HEARTBEAT_REPLY:                                /* Row X3 */
        s_missed = 0;
        s_awaiting_reply = false;
        if (s_link == LINK_DEGRADED) {
            s_link = LINK_CONNECTED;
            event_log_append(EVT_COMM_RESTORED, OUT_SUCCESS, NULL, NULL, NULL);
            retransmit(false);                               /* whole outbox, in seq order */
        }
        break;

    case MSG_ACK: {                                          /* Row X4 */
        char v[16];
        if (!proto_body_get(m.body, "ack_seq", v, sizeof v)) break;
        unsigned ack = (unsigned)strtoul(v, NULL, 10);
        for (int i = 0; i < s_out_count; i++) {
            if (s_outbox[i].seq == ack) {
                OutMsg removed = s_outbox[i];
                memmove(&s_outbox[i], &s_outbox[i + 1],
                        (size_t)(s_out_count - i - 1) * sizeof *s_outbox);
                s_out_count--;
                if (!save_outbox()) {                        /* keep it until it is saved */
                    memmove(&s_outbox[i + 1], &s_outbox[i],
                            (size_t)(s_out_count - i) * sizeof *s_outbox);
                    s_outbox[i] = removed;
                    s_out_count++;
                    s_healthy = false;
                }
                break;
            }
        }
        break;                                               /* unknown ack_seq: ignored */
    }

    case MSG_PERMISSION_UPDATE:                              /* Row X5 */
    case MSG_SCHEDULE:
    case MSG_CONFIG_CHANGE: {
        bool ok = s_handlers[m.type] != NULL && s_handlers[m.type](m.body) == 0;
        s_last_rx_seq = m.seq;
        if (!save_outbox()) s_healthy = false;
        send_ack(m.seq, ok);
        break;
    }

    default:
        break;                                               /* an ADC->MS type sent to us: ignored */
    }
}

int comm_register_handler(MessageType type, CommHandler handler)
{
    if (!is_handled_incoming(type) || s_handlers[type] != NULL) return -1;
    s_handlers[type] = handler;
    return 0;
}

LinkStatus comm_link_status(void) { return s_link; }
int        comm_pending_count(void) { return s_out_count; }
bool       comm_is_healthy(void) { return s_healthy; }

void comm_close(void)
{
    free(s_outbox);
    s_outbox = NULL;
    s_out_count = s_out_cap = 0;
}