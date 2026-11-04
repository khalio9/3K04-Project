// TestEventLog.cpp - tests named after the SPEC-LOG rows
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include "../adc/EventLog.h"

static long long fakeNow = 1000;
static Clock clock_ = [] { return fakeNow; };
static int failures = 0;
static void check(const std::string &name, bool ok)
{
    std::cout << (ok ? "PASS " : "FAIL ") << name << '\n';
    if (!ok) failures++;
}
template <class Ex, class F> static bool throws(F f) { try { f(); } catch (const Ex &) { return true; } return false; }

static const char *PATH = "test_log.txt";

int main()
{
    std::remove(PATH);
    {
        EventLog log("ADC-19", PATH, clock_);
        check("G1_no_file_starts_empty", log.lastSeq() == 0 && log.isHealthy());

        check("L2_pin_rejected", throws<std::invalid_argument>([&] {
            log.append(EventType::AUTH_ATTEMPT, Outcome::FAILED, "u1", "badge=B1;pin=1234"); }));
        check("L2_patient_name_rejected", throws<std::invalid_argument>([&] {
            log.append(EventType::DISPENSE, Outcome::COMPLETE, "u1", "patient_name=Bob"); }));
        check("L2_nothing_added_on_reject", log.lastSeq() == 0);

        log.append(EventType::DISPENSE, Outcome::COMPLETE, "u1", "patient_id=P7;med=M1");
        fakeNow = 5000;
        LogEntry e = log.append(EventType::MODE_CHANGE, Outcome::SUCCESS, "", "to=MAINTENANCE");
        check("L4_appends_with_next_seq_and_time", e.seq == 2 && e.timeMs == 5000 && e.adcId == "ADC-19");

        log.simulateDiskFailure(true);
        check("L3_disk_failure_throws", throws<LogStorageError>([&] {
            log.append(EventType::ALARM, Outcome::FAILED); }));
        check("L3_unhealthy_and_nothing_added", !log.isHealthy() && log.lastSeq() == 2);
    }
    {
        EventLog log("ADC-19", PATH, clock_);
        auto all = log.entries();
        check("G2_restart_restores_everything",
              log.isHealthy() && all.size() == 2 && all[0].seq == 1 && all[1].seq == 2);
    }
    {
        std::ofstream(PATH, std::ios::app) << "3|123|ADC-19|2|u|4|half-writ";   // power cut mid-write
        EventLog log("ADC-19", PATH, clock_);
        LogEntry e = log.append(EventType::ALARM, Outcome::SUCCESS, "", "type=TEST");
        check("G3_partial_last_line_dropped", log.isHealthy() && e.seq == 3);
    }
    {
        std::ifstream in(PATH);
        std::stringstream ss;
        ss << in.rdbuf();
        std::string text = ss.str();
        text.replace(text.find("patient_id=P7"), 13, "patient_id=X7");    // tamper with an old record
        in.close();
        std::ofstream(PATH, std::ios::trunc) << text;
        EventLog log("ADC-19", PATH, clock_);
        check("G4_edited_record_detected", !log.isHealthy());
    }
    std::remove(PATH);
    std::cout << (failures ? "\nSOME FAILED\n" : "\nALL PASSED\n");
    return failures != 0;
}