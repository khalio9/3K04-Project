// CommManager.cpp - heartbeat, outbox, resend and ACKs (the secret).
// The message format itself is in Protocol.cpp (also private to this module).
#include "CommManager.h"
#include "Protocol.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

// Number -> text (std::to_string is missing on some older Windows compilers)
static std::string num(long long v) { std::ostringstream o; o << v; return o.str(); }

CommManager::CommManager(const std::string &adcId, const std::string &outboxPath, Clock clock,
                         Transport transport, EventLog &log,
                         long long heartbeatMs, int missedLimit, long long retryMs)
    : adcId_(adcId), path_(outboxPath), clock_(clock), transport_(transport), log_(log),
      heartbeatMs_(heartbeatMs), retryMs_(retryMs), missedLimit_(missedLimit)
{
    loadOutbox();                               // SPEC-COM-4
    lastHeartbeat_ = now() - heartbeatMs_;      // first tick sends a heartbeat at once
}

long long CommManager::now() const { return clock_ ? clock_() : 0; }

// ---------- outbox storage: write a temp file, then replace the old one ----------
bool CommManager::saveOutbox()
{
    if (diskFails_) return false;
    std::string tmp = path_ + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        out << "H|" << nextSeq_ << '|' << lastRxSeq_ << '|' << lastForwardedLogSeq_ << '\n';
        for (const OutMsg &m : outbox_)
            out << m.seq << '|' << static_cast<int>(m.type) << '|' << m.timeMs << '|' << m.body << '\n';
        if (!out) return false;
    }
    std::remove(path_.c_str());                 // Windows rename() will not overwrite
    return std::rename(tmp.c_str(), path_.c_str()) == 0;
}

void CommManager::loadOutbox()
{
    std::ifstream in(path_, std::ios::binary);
    if (!in) return;                            // nothing saved yet
    std::string line;
    if (!std::getline(in, line) || line.compare(0, 2, "H|") != 0) { healthy_ = false; return; }
    std::istringstream h(line.substr(2));
    char bar;
    if (!(h >> nextSeq_ >> bar >> lastRxSeq_ >> bar >> lastForwardedLogSeq_)) { healthy_ = false; return; }

    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        size_t a = line.find('|'), b = line.find('|', a + 1), c = line.find('|', b + 1);
        if (c == std::string::npos) { healthy_ = false; return; }
        OutMsg m;
        m.seq      = static_cast<unsigned>(std::strtoul(line.substr(0, a).c_str(), nullptr, 10));
        m.type     = static_cast<MessageType>(std::atoi(line.substr(a + 1, b - a - 1).c_str()));
        m.timeMs   = std::strtoll(line.substr(b + 1, c - b - 1).c_str(), nullptr, 10);
        m.body     = line.substr(c + 1);
        m.lastSent = -1;                        // resend everything after a restart
        outbox_.push_back(m);
    }
}

// ---------- sending ----------
void CommManager::transmit(MessageType type, unsigned seq, long long timeMs, const std::string &body)
{
    Protocol::Message m{type, adcId_, seq, timeMs, body};
    if (transport_) transport_(Protocol::encode(m));
}

// Heartbeats and ACKs are never stored, but still get a seq (FR-COM-02).
void CommManager::transmitUnstored(MessageType type, const std::string &body)
{
    transmit(type, nextSeq_++, now(), body);
}

void CommManager::retransmit(bool onlyDue)
{
    for (OutMsg &m : outbox_) {                 // outbox is in seq order (COM-I3)
        if (onlyDue && m.lastSent >= 0 && now() - m.lastSent < retryMs_) continue;
        transmit(m.type, m.seq, m.timeMs, m.body);
        m.lastSent = now();
    }
}

void CommManager::sendAck(unsigned seq, bool ok)
{
    transmitUnstored(MessageType::ACK,
                     "ack_seq=" + num(seq) + ";result=" + (ok ? "OK" : "REJECTED"));
}

// SPEC-COM-1
unsigned CommManager::send(MessageType type, const std::string &body)
{
    // Row S1: only stored outgoing types (heartbeats are sent by tick, not stored)
    if (type < MessageType::STATUS || type > MessageType::EVENT)
        throw std::invalid_argument("not an outgoing message type");

    OutMsg m{nextSeq_, type, now(), body, -1};
    outbox_.push_back(m);
    nextSeq_++;
    if (!saveOutbox()) {                        // Row S2: undo, nothing queued
        outbox_.pop_back();
        nextSeq_--;
        healthy_ = false;
        throw CommStorageError("outbox write failed");
    }
    if (link_ == LinkStatus::CONNECTED) {       // Row S4: send at once
        transmit(m.type, m.seq, m.timeMs, m.body);
        outbox_.back().lastSent = now();
    }                                           // Row S3: DEGRADED -> stays queued
    return m.seq;
}

// SPEC-COM-2
void CommManager::tick()
{
    // Row K1: heartbeat
    if (now() - lastHeartbeat_ >= heartbeatMs_) {
        if (awaitingReply_) missed_++;
        transmitUnstored(MessageType::HEARTBEAT, "pending=" + num(outbox_.size()));
        awaitingReply_ = true;
        lastHeartbeat_ = now();
    }

    // Row K2: link loss
    if (link_ == LinkStatus::CONNECTED && missed_ >= missedLimit_) {
        link_ = LinkStatus::DEGRADED;
        try { log_.append(EventType::COMM_LOST, Outcome::SUCCESS, "", "missed_heartbeats=3"); }
        catch (const std::exception &) { /* storage fault is reported by FaultMonitor */ }
    }

    // Row K3: forward new log entries as EVENT messages
    for (const LogEntry &e : log_.entries(lastForwardedLogSeq_ + 1)) {
        std::string body = "log_seq=" + num(e.seq) + ";event=" + toString(e.type) +
                           ";user=" + e.userId + ";outcome=" + toString(e.outcome) + ";" + e.details;
        try {
            lastForwardedLogSeq_ = e.seq;
            send(MessageType::EVENT, body);
        } catch (const CommStorageError &) {
            lastForwardedLogSeq_ = e.seq - 1;   // try again next tick
            break;
        }
    }

    // Row K4: resend unACKed messages that are due
    if (link_ == LinkStatus::CONNECTED) retransmit(true);
}

// SPEC-COM-3
void CommManager::receive(const std::string &raw)
{
    Protocol::Message m;
    // Row X1: malformed, fails authentication, or not for this cabinet
    if (!Protocol::decode(raw, m) || m.adcId != adcId_) return;

    // Row X2: duplicate -> ACK again, do not apply again
    if (m.type != MessageType::HEARTBEAT_REPLY && m.type != MessageType::ACK && m.seq <= lastRxSeq_) {
        sendAck(m.seq, true);
        return;
    }

    switch (m.type) {
    case MessageType::HEARTBEAT_REPLY:          // Row X3
        missed_ = 0;
        awaitingReply_ = false;
        if (link_ == LinkStatus::DEGRADED) {
            link_ = LinkStatus::CONNECTED;
            try { log_.append(EventType::COMM_RESTORED, Outcome::SUCCESS); }
            catch (const std::exception &) {}
            retransmit(false);                  // whole outbox, oldest first
        }
        break;

    case MessageType::ACK: {                    // Row X4
        unsigned ack = static_cast<unsigned>(std::strtoul(Protocol::bodyGet(m.body, "ack_seq").c_str(), nullptr, 10));
        for (size_t i = 0; i < outbox_.size(); i++) {
            if (outbox_[i].seq != ack) continue;
            OutMsg removed = outbox_[i];
            outbox_.erase(outbox_.begin() + i);
            if (!saveOutbox()) {                // keep it until the removal is saved
                outbox_.insert(outbox_.begin() + i, removed);
                healthy_ = false;
            }
            break;
        }
        break;                                  // unknown ack_seq: ignored
    }

    case MessageType::PERMISSION_UPDATE:        // Row X5
    case MessageType::SCHEDULE:
    case MessageType::CONFIG_CHANGE: {
        bool ok = handlers_.count(m.type) && handlers_[m.type](m.body);
        lastRxSeq_ = m.seq;
        if (!saveOutbox()) healthy_ = false;
        sendAck(m.seq, ok);
        break;
    }

    default:
        break;                                  // an ADC->MS type sent to us: ignored
    }
}

void CommManager::registerHandler(MessageType type, Handler handler)
{
    bool incoming = type == MessageType::PERMISSION_UPDATE || type == MessageType::SCHEDULE ||
                    type == MessageType::CONFIG_CHANGE;
    if (!incoming || handlers_.count(type))
        throw std::invalid_argument("not an incoming type, or it already has a handler");
    handlers_[type] = handler;
}

LinkStatus CommManager::linkStatus() const   { return link_; }
size_t     CommManager::pendingCount() const { return outbox_.size(); }
bool       CommManager::isHealthy() const    { return healthy_; }