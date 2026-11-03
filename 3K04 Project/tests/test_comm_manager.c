/* test_comm_manager.c - tests named after the SPEC-COM rows */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../adc/comm_manager.h"
#include "../adc/event_log.h"
#include "../adc/ms_sim.h"
#include "../adc/comm_protocol.h"   /* tests only: to build a replayed message */

static long long t_ms = 0;
static long long now_ms(void) { return t_ms; }
static int failures = 0;
#define CHECK(name, cond) do { if (cond) printf("PASS %s\n", name); \
                               else { printf("FAIL %s\n", name); failures++; } } while (0)

/* Transport that records what was sent, then passes it to the simulated MS */
static char sent[512][1024];
static int  sent_count = 0;
static int capture(const char *bytes, int len)
{
    if (sent_count < 512) snprintf(sent[sent_count++], 1024, "%s", bytes);
    return ms_sim_transport(bytes, len);
}

static const char *LOG = "test_comm_log.txt";
static const char *OUT = "test_outbox.txt";

/* one second of the main loop */
static void step(void) { t_ms += 1000; comm_tick(); ms_sim_pump(); }

static int handler_calls = 0;
static int perm_handler(const char *body) { handler_calls++; return strstr(body, "role=BAD") ? 1 : 0; }

static void start(bool fresh)
{
    if (fresh) { remove(LOG); remove(OUT); }
    event_log_init("ADC-19", LOG, now_ms);
    ms_sim_init("ADC-19", now_ms);
    comm_init("ADC-19", OUT, now_ms, capture, 30000, 3, 5000);
    comm_register_handler(MSG_PERMISSION_UPDATE, perm_handler);
    sent_count = 0;
}

static int count_sent(const char *prefix)
{
    int n = 0;
    for (int i = 0; i < sent_count; i++) if (strncmp(sent[i], prefix, strlen(prefix)) == 0) n++;
    return n;
}

int main(void)
{
    start(true);

    /* SPEC-COM-4: link starts DEGRADED until a reply */
    CHECK("COM4_starts_degraded", comm_link_status() == LINK_DEGRADED);

    /* K1 + X3: first tick sends a heartbeat, the reply connects */
    step();
    CHECK("K1_heartbeat_sent", count_sent("HEARTBEAT|") == 1);
    CHECK("X3_reply_connects", comm_link_status() == LINK_CONNECTED);

    /* S1: invalid type */
    CHECK("S1_invalid_type_rejected", comm_send(MSG_ACK, "x=1") == COMM_ERR_INVALID);

    /* S4: connected -> sent at once, then ACKed (X4) and removed */
    step();                                      /* flush the COMM_RESTORED event */
    int before = comm_pending_count();
    int seq = comm_send(MSG_ALARM, "type=LOCK_FAILURE;severity=CRITICAL");
    CHECK("S4_connected_sends_at_once", seq > 0 && count_sent("ALARM|") == 1);
    ms_sim_pump();
    CHECK("X4_ack_removes_from_outbox", comm_pending_count() == before);

    /* K1 + K2: cut the link, 3 missed replies -> DEGRADED and COMM_LOST logged */
    ms_sim_set_link(false);
    for (int i = 0; i < 120; i++) step();
    CHECK("K2_three_missed_heartbeats_degraded", comm_link_status() == LINK_DEGRADED);
    LogEntry e;
    event_log_entries(event_log_last_seq(), 0, &e, 1);
    CHECK("K2_comm_lost_logged", e.event_type == EVT_COMM_LOST);

    /* S3: offline -> queued, not sent */
    int sent_before = count_sent("INVENTORY|");
    int s1 = comm_send(MSG_INVENTORY, "loc=A1;qty=9");
    int s2 = comm_send(MSG_INVENTORY, "loc=A1;qty=8");
    CHECK("S3_offline_queued_not_sent", s1 > 0 && s2 > s1 && count_sent("INVENTORY|") == sent_before);
    int queued = comm_pending_count();

    /* COM-I5: outbox survives a restart */
    comm_close();
    comm_init("ADC-19", OUT, now_ms, capture, 30000, 3, 5000);
    comm_register_handler(MSG_PERMISSION_UPDATE, perm_handler);
    CHECK("COM_I5_outbox_survives_restart", comm_pending_count() == queued);

    /* X3: restore -> CONNECTED, whole outbox resent in seq order, then all ACKed */
    ms_sim_set_link(true);
    sent_count = 0;
    for (int i = 0; i < 40; i++) step();
    int i1 = -1, i2 = -1;
    char p1[32], p2[32];
    snprintf(p1, sizeof p1, "INVENTORY|ADC-19|%d|", s1);
    snprintf(p2, sizeof p2, "INVENTORY|ADC-19|%d|", s2);
    for (int i = 0; i < sent_count; i++) {
        if (i1 < 0 && strncmp(sent[i], p1, strlen(p1)) == 0) i1 = i;
        if (i2 < 0 && strncmp(sent[i], p2, strlen(p2)) == 0) i2 = i;
    }
    CHECK("X3_restored_connected", comm_link_status() == LINK_CONNECTED);
    CHECK("X3_resent_in_seq_order", i1 >= 0 && i2 > i1);
    CHECK("COM_I2_emptied_only_by_acks", comm_pending_count() == 0);

    /* K3: log entries are forwarded as EVENT messages */
    event_log_append(EVT_DISPENSE, OUT_COMPLETE, "nurse1", "med=M1;qty=1;patient_id=P7", NULL);
    step(); step();
    CHECK("K3_log_entry_forwarded", count_sent("EVENT|") > 0 &&
          strstr(ms_sim_last_received(), "DISPENSE") != NULL);

    /* X5: permission update applied, and an invalid one rejected */
    handler_calls = 0;
    ms_sim_send_permission_update("badge=B9;role=NURSE");
    ms_sim_pump();
    CHECK("X5_handler_applied", handler_calls == 1 && count_sent("ACK|") >= 1);
    ms_sim_send_permission_update("badge=B9;role=BAD");
    sent_count = 0;
    ms_sim_pump();
    CHECK("X5_rejected_update_acked_REJECTED", strstr(sent[sent_count - 1], "result=REJECTED") != NULL);

    /* X2: replay an MS message that was already applied -> re-ACKed, not applied again */
    WireMsg w;
    memset(&w, 0, sizeof w);
    w.type = MSG_PERMISSION_UPDATE;
    snprintf(w.adc_id, sizeof w.adc_id, "ADC-19");
    w.seq = 1;
    snprintf(w.body, sizeof w.body, "badge=B9;role=NURSE");
    char raw[WIRE_MAX];
    proto_encode(&w, raw, sizeof raw);
    handler_calls = 0;
    sent_count = 0;
    comm_receive(raw);
    CHECK("X2_duplicate_reacked_not_applied", handler_calls == 0 && count_sent("ACK|") == 1);

    /* X1: wrong authentication code -> discarded */
    comm_receive("PERMISSION_UPDATE|ADC-19|999|0|badge=B9;role=NURSE|0000000000000000");
    CHECK("X1_bad_mac_discarded", handler_calls == 0);

    /* X1: malformed message is discarded */
    int pend = comm_pending_count();
    comm_receive("garbage");
    CHECK("X1_malformed_discarded", comm_pending_count() == pend);

    comm_close();
    event_log_close();
    remove(LOG); remove(OUT);
    printf(failures ? "\n%d FAILED\n" : "\nALL PASSED\n", failures);
    return failures != 0;
}