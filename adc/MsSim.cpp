// MsSim.cpp - Simulated Monitoring System (demo and tests only)
#include "MsSim.h"
#include "Protocol.h"

#include <sstream>

MsSim::MsSim(const std::string &adcId, Clock clock) : adcId_(adcId), clock_(clock) {}

void MsSim::reply(MessageType type, const std::string &body)
{
    Protocol::Message m{type, adcId_, seq_++, clock_ ? clock_() : 0, body};
    replies_.push_back(Protocol::encode(m));
}

void MsSim::transport(const std::string &bytes)
{
    if (!up_) return;                                   // lost on the network (OUT-5)
    Protocol::Message m;
    if (!Protocol::decode(bytes, m)) return;

    if (m.type == MessageType::HEARTBEAT) { reply(MessageType::HEARTBEAT_REPLY, ""); return; }
    if (m.type == MessageType::ACK) return;             // the ADC acknowledging one of ours

    std::ostringstream body;
    body << "ack_seq=" << m.seq;
    reply(MessageType::ACK, body.str());                // ACK everything, duplicates too
    if (!seen_.insert(m.seq).second) return;            // ...but count each message once

    received_++;
    last_ = Protocol::typeName(m.type) + " " + m.body.substr(0, 90);
}

void MsSim::pump(CommManager &comm)
{
    std::vector<std::string> batch;
    batch.swap(replies_);
    if (!up_) return;                                   // replies lost on the network
    for (const std::string &r : batch) comm.receive(r);
}

void MsSim::setLinkUp(bool up) { up_ = up; }
bool MsSim::linkUp() const { return up_; }
void MsSim::sendPermissionUpdate(const std::string &body) { reply(MessageType::PERMISSION_UPDATE, body); }
int MsSim::receivedCount() const { return received_; }
std::string MsSim::lastReceived() const { return last_; }