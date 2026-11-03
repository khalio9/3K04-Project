/* ms_sim.c - Simulated Monitoring System (demo and tests only) */
#include "ms_sim.h"
#include "comm_manager.h"
#include "comm_protocol.h"

#include <stdio.h>
#include <string.h>

#define MAX_PENDING 256

static char     s_adc_id[ADC_ID_MAX];
static NowMsFn  s_now;
static bool     s_up = true;
static unsigned s_seq = 1;                 /* MS's own message counter */
static unsigned s_seen[4096];              /* ADC seqs already received (dedupe) */
static int      s_seen_count = 0;
static int      s_received = 0;
static char     s_last[160] = "(nothing yet)";
static char     s_pending[MAX_PENDING][WIRE_MAX];   /* replies waiting to be delivered */
static int      s_pending_count = 0;

static void queue_reply(MessageType type, const char *body)
{
    if (s_pending_count >= MAX_PENDING) return;
    WireMsg w;
    memset(&w, 0, sizeof w);
    w.type = type;
    snprintf(w.adc_id, sizeof w.adc_id, "%s", s_adc_id);
    w.seq = s_seq++;
    w.time_ms = s_now ? s_now() : 0;
    snprintf(w.body, sizeof w.body, "%s", body ? body : "");
    proto_encode(&w, s_pending[s_pending_count++], WIRE_MAX);
}

static bool seen_before(unsigned seq)
{
    for (int i = 0; i < s_seen_count; i++)
        if (s_seen[i] == seq) return true;
    if (s_seen_count < 4096) s_seen[s_seen_count++] = seq;
    return false;
}

void ms_sim_init(const char *adc_id, NowMsFn now_ms)
{
    snprintf(s_adc_id, sizeof s_adc_id, "%s", adc_id);
    s_now = now_ms;
    s_up = true;
    s_seq = 1;
    s_seen_count = s_received = s_pending_count = 0;
    snprintf(s_last, sizeof s_last, "(nothing yet)");
}

int ms_sim_transport(const char *bytes, int len)
{
    (void)len;
    if (!s_up) return 0;                    /* lost on the network (OUT-5) */
    WireMsg m;
    if (!proto_decode(bytes, &m)) return 0;

    if (m.type == MSG_HEARTBEAT) {
        queue_reply(MSG_HEARTBEAT_REPLY, NULL);
        return 0;
    }
    if (m.type == MSG_ACK) return 0;        /* ADC acknowledging one of ours */

    char body[32];
    snprintf(body, sizeof body, "ack_seq=%u", m.seq);
    queue_reply(MSG_ACK, body);             /* ACK everything, duplicates included */
    if (seen_before(m.seq)) return 0;       /* but record it only once */

    s_received++;
    snprintf(s_last, sizeof s_last, "%s seq=%u %.100s", proto_type_name(m.type), m.seq, m.body);
    return 0;
}

void ms_sim_set_link(bool up) { s_up = up; }
bool ms_sim_link_up(void) { return s_up; }

void ms_sim_pump(void)
{
    if (!s_up) { s_pending_count = 0; return; }    /* replies lost on the network */
    int n = s_pending_count;
    static char batch[MAX_PENDING][WIRE_MAX];
    memcpy(batch, s_pending, (size_t)n * WIRE_MAX);
    s_pending_count = 0;
    for (int i = 0; i < n; i++) comm_receive(batch[i]);
}

void ms_sim_send_permission_update(const char *body) { queue_reply(MSG_PERMISSION_UPDATE, body); }

int ms_sim_received_count(void) { return s_received; }
const char *ms_sim_last_received(void) { return s_last; }