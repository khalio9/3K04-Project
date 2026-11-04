// TestCommManager.cpp - tests named after the SPEC-COM rows
#include <cstdio>
#include <iostream>
#include <sstream>
#include <vector>
#include "../adc/CommManager.h"
#include "../adc/MsSim.h"
#include "../adc/Protocol.h"   // tests only: to build a replayed message

static long long t = 0;
static Clock clock_ = [] { return t; };
static int failures = 0;
static void check(const std::string &name, bool ok)
{
    std::cout << (ok ? "PASS " : "FAIL ") << name << '\n';
    if (!ok) failures++;
}

static std::string num(unsigned v) { std::ostringstream o; o << v; return o.str(); }

static std::vector<std::string> sent;   // everything the ADC put on the network
static int countSent(const std::string &prefix)
{
    int n = 0;
    for (const std::string &s : sent) if (s.compare(0, prefix.size(), prefix) == 0) n++;
    return n;
}

int main()
{
    std::remove("test_comm_log.txt");
    std::remove("test_outbox.txt");
    EventLog log("ADC-19", "test_comm_log.txt", clock_);
    MsSim ms("ADC-19", clock_);
    Transport net = [&](const std::string &b) { sent.push_back(b); ms.transport(b); };
    int handlerCalls = 0;
    Handler perm = [&](const std::string &body) { handlerCalls++; return body.find("role=BAD") == std::string::npos; };

    CommManager *comm = new CommManager("ADC-19", "test_outbox.txt", clock_, net, log, 30000, 3, 5000);
    comm->registerHandler(MessageType::PERMISSION_UPDATE, perm);
    auto step = [&] { t += 1000; comm->tick(); ms.pump(*comm); };

    check("COM4_starts_degraded", comm->linkStatus() == LinkStatus::DEGRADED);
    step();
    check("K1_heartbeat_sent", countSent("HEARTBEAT|") == 1);
    check("X3_reply_connects", comm->linkStatus() == LinkStatus::CONNECTED);

    bool threw = false;
    try { comm->send(MessageType::ACK, "x=1"); } catch (const std::invalid_argument &) { threw = true; }
    check("S1_invalid_type_rejected", threw);

    step();
    size_t before = comm->pendingCount();
    comm->send(MessageType::ALARM, "type=LOCK_FAILURE;severity=CRITICAL");
    check("S4_connected_sends_at_once", countSent("ALARM|") == 1);
    ms.pump(*comm);
    check("X4_ack_removes_from_outbox", comm->pendingCount() == before);

    ms.setLinkUp(false);
    for (int i = 0; i < 120; i++) step();
    check("K2_three_missed_heartbeats_degraded", comm->linkStatus() == LinkStatus::DEGRADED);
    check("K2_comm_lost_logged", log.entries(log.lastSeq())[0].type == EventType::COMM_LOST);

    int inventoryBefore = countSent("INVENTORY|");
    unsigned s1 = comm->send(MessageType::INVENTORY, "loc=A1;qty=9");
    unsigned s2 = comm->send(MessageType::INVENTORY, "loc=A1;qty=8");
    check("S3_offline_queued_not_sent", s2 > s1 && countSent("INVENTORY|") == inventoryBefore);

    size_t queued = comm->pendingCount();
    delete comm;                                                          // restart
    comm = new CommManager("ADC-19", "test_outbox.txt", clock_, net, log, 30000, 3, 5000);
    comm->registerHandler(MessageType::PERMISSION_UPDATE, perm);
    check("COM_I5_outbox_survives_restart", comm->pendingCount() == queued);

    ms.setLinkUp(true);
    sent.clear();
    for (int i = 0; i < 40; i++) step();
    int i1 = -1, i2 = -1;
    for (int i = 0; i < (int)sent.size(); i++) {
        if (i1 < 0 && sent[i].find("INVENTORY|ADC-19|" + num(s1) + "|") == 0) i1 = i;
        if (i2 < 0 && sent[i].find("INVENTORY|ADC-19|" + num(s2) + "|") == 0) i2 = i;
    }
    check("X3_restored_connected", comm->linkStatus() == LinkStatus::CONNECTED);
    check("X3_resent_in_seq_order", i1 >= 0 && i2 > i1);
    check("COM_I2_emptied_only_by_acks", comm->pendingCount() == 0);

    log.append(EventType::DISPENSE, Outcome::COMPLETE, "nurse1", "med=M1;qty=1;patient_id=P7");
    step(); step();
    check("K3_log_entry_forwarded", ms.lastReceived().find("DISPENSE") != std::string::npos);

    handlerCalls = 0;
    ms.sendPermissionUpdate("badge=B9;role=NURSE");
    ms.pump(*comm);
    check("X5_handler_applied", handlerCalls == 1);
    sent.clear();
    ms.sendPermissionUpdate("badge=B9;role=BAD");
    ms.pump(*comm);
    check("X5_rejected_update_acked_REJECTED", !sent.empty() && sent.back().find("result=REJECTED") != std::string::npos);

    Protocol::Message old{MessageType::PERMISSION_UPDATE, "ADC-19", 1, 0, "badge=B9;role=NURSE"};
    handlerCalls = 0;
    sent.clear();
    comm->receive(Protocol::encode(old));
    check("X2_duplicate_reacked_not_applied", handlerCalls == 0 && countSent("ACK|") == 1);

    comm->receive("PERMISSION_UPDATE|ADC-19|999|0|badge=B9;role=NURSE|0000000000000000");
    check("X1_bad_mac_discarded", handlerCalls == 0);
    size_t pend = comm->pendingCount();
    comm->receive("garbage");
    check("X1_malformed_discarded", comm->pendingCount() == pend);

    delete comm;
    std::remove("test_comm_log.txt");
    std::remove("test_outbox.txt");
    std::cout << (failures ? "\nSOME FAILED\n" : "\nALL PASSED\n");
    return failures != 0;
}