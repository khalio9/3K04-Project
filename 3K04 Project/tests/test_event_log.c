/* test_event_log.c - tests named after the SPEC-LOG rows */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../adc/event_log.h"

static long long fake_now = 1000;
static long long now_ms(void) { return fake_now; }
static int failures = 0;
#define CHECK(name, cond) do { if (cond) printf("PASS %s\n", name); \
                               else { printf("FAIL %s\n", name); failures++; } } while (0)

static const char *PATH = "test_log.txt";

static void test_G1_no_file_starts_empty(void)
{
    remove(PATH);
    event_log_init("ADC-19", PATH, now_ms);
    CHECK("G1_no_file_starts_empty", event_log_last_seq() == 0 && event_log_is_healthy());
}

static void test_L1_invalid_type_rejected(void)
{
    unsigned before = event_log_last_seq();
    LogStatus s = event_log_append((EventType)99, OUT_SUCCESS, "u1", NULL, NULL);
    CHECK("L1_invalid_type_rejected", s == LOG_ERR_INVALID && event_log_last_seq() == before);
}

static void test_L2_pin_and_patient_data_rejected(void)
{
    unsigned before = event_log_last_seq();
    LogStatus a = event_log_append(EVT_AUTH_ATTEMPT, OUT_FAILED, "u1", "badge=B1;pin=1234", NULL);
    LogStatus b = event_log_append(EVT_DISPENSE, OUT_COMPLETE, "u1", "patient_name=Bob", NULL);
    LogStatus c = event_log_append(EVT_DISPENSE, OUT_COMPLETE, "u1", "patient_id=P7;med=M1", NULL);
    CHECK("L2_pin_rejected", a == LOG_ERR_FORBIDDEN);
    CHECK("L2_patient_name_rejected", b == LOG_ERR_FORBIDDEN);
    CHECK("L2_patient_id_allowed", c == LOG_OK && event_log_last_seq() == before + 1);
}

static void test_L4_appends_with_next_seq_and_time(void)
{
    fake_now = 5000;
    LogEntry e;
    unsigned before = event_log_last_seq();
    LogStatus s = event_log_append(EVT_MODE_CHANGE, OUT_SUCCESS, NULL, "to=MAINTENANCE", &e);
    CHECK("L4_appends_with_next_seq_and_time",
          s == LOG_OK && e.seq == before + 1 && e.time_ms == 5000 &&
          strcmp(e.adc_id, "ADC-19") == 0);
}

static void test_G2_restart_restores_everything(void)
{
    unsigned before = event_log_last_seq();
    event_log_close();
    event_log_init("ADC-19", PATH, now_ms);
    LogEntry all[16];
    int n = event_log_entries(1, 0, all, 16);
    int ok = n == (int)before && event_log_is_healthy();
    for (int i = 0; i < n; i++) ok = ok && all[i].seq == (unsigned)(i + 1);   /* LOG-I1 */
    CHECK("G2_restart_restores_everything", ok);
}

static void test_G3_partial_last_line_dropped(void)
{
    unsigned before = event_log_last_seq();
    event_log_close();
    FILE *f = fopen(PATH, "ab");
    fputs("99|123|ADC-19|2|u|4|half-writ", f);        /* power cut mid-write */
    fclose(f);
    event_log_init("ADC-19", PATH, now_ms);
    LogStatus s = event_log_append(EVT_ALARM, OUT_SUCCESS, NULL, "type=TEST", NULL);
    CHECK("G3_partial_last_line_dropped",
          event_log_is_healthy() && s == LOG_OK && event_log_last_seq() == before + 1);
}

static void test_G4_edited_record_detected(void)
{
    event_log_close();
    FILE *f = fopen(PATH, "rb");
    char buf[8192];
    size_t n = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    buf[n] = '\0';
    char *p = strstr(buf, "patient_id=P7");   /* tamper with an old record */
    if (p) p[11] = 'X';
    f = fopen(PATH, "wb");
    fwrite(buf, 1, n, f);
    fclose(f);
    event_log_init("ADC-19", PATH, now_ms);
    CHECK("G4_edited_record_detected", !event_log_is_healthy());
}

int main(void)
{
    test_G1_no_file_starts_empty();
    test_L1_invalid_type_rejected();
    test_L2_pin_and_patient_data_rejected();
    test_L4_appends_with_next_seq_and_time();
    test_G2_restart_restores_everything();
    test_G3_partial_last_line_dropped();
    test_G4_edited_record_detected();
    event_log_close();
    remove(PATH);
    printf(failures ? "\n%d FAILED\n" : "\nALL PASSED\n", failures);
    return failures != 0;
}