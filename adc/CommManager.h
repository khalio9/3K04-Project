// CommManager.h - CommManager (M3.2): messages to and from the Monitoring System
//
// Responsibility: exchange messages with the MS and keep them while the link is down
//                 (FR-COM-01 to -09).
// Secret (the private: section + Protocol.cpp): message format, network transport,
//         heartbeat and retry policy, and how the outbox is stored.
// Spec: SPEC-COM-1 (send), SPEC-COM-2 (tick), SPEC-COM-3 (receive), SPEC-COM-4 (load).
// Uses: EventLog (to log COMM_LOST / COMM_RESTORED and to forward log entries).
// Owner: Omer
#pragma once
#include <functional>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>
#include "EventLog.h"
#include "Types.h"

enum class MessageType {
    HEARTBEAT, STATUS, INVENTORY, ALARM, MAINTENANCE, HEALTH, EVENT,   // ADC -> MS
    HEARTBEAT_REPLY, PERMISSION_UPDATE, SCHEDULE, CONFIG_CHANGE,       // MS -> ADC
    ACK                                                                // both
};

enum class LinkStatus { DEGRADED, CONNECTED };

// Thrown by send() when the outbox cannot be saved (row S2).
class CommStorageError : public std::runtime_error {
public:
    explicit CommStorageError(const std::string &m) : std::runtime_error(m) {}
};

// The network link: hands bytes to the network. Delivery is not guaranteed (OUT-5).
// The real network or the simulated MS (MsSim) is plugged in here.
using Transport = std::function<void(const std::string &bytes)>;

// Applies an incoming PERMISSION_UPDATE / SCHEDULE / CONFIG_CHANGE. Returns true if applied.
using Handler = std::function<bool(const std::string &body)>;

class CommManager {
public:
    // Constructor = SPEC-COM-4: restores the outbox. Link starts DEGRADED (NFR-SAF-02).
    CommManager(const std::string &adcId, const std::string &outboxPath, Clock clock,
                Transport transport, EventLog &log,
                long long heartbeatMs = Defaults::HEARTBEAT_MS,
                int missedLimit = Defaults::MISSED_LIMIT,
                long long retryMs = Defaults::RETRY_MS);

    // SPEC-COM-1. Stores the message in the outbox (ADC ID + next seq) and, if CONNECTED,
    // sends it at once. It stays in the outbox until ACKed. Returns its seq.
    // Throws std::invalid_argument (row S1) or CommStorageError (row S2; nothing queued).
    unsigned send(MessageType type, const std::string &body = "");

    // SPEC-COM-2. Call at least once per second: heartbeat, loss detection,
    // forwarding new log entries, retransmission.
    void tick();

    // SPEC-COM-3. Call when bytes arrive. Never throws: bad or duplicate messages are
    // discarded or re-ACKed.
    void receive(const std::string &raw);

    // Startup wiring in main.cpp. Throws std::invalid_argument for a non-incoming type
    // or a type that already has a handler.
    void registerHandler(MessageType type, Handler handler);

    LinkStatus linkStatus() const;      // shown on screen at all times (FR-MOD-04)
    size_t     pendingCount() const;    // unACKed messages in the outbox
    bool       isHealthy() const;       // false if the outbox failed to load or save

    // Demo/test hook: pretend the outbox disk has failed.
    void simulateDiskFailure(bool fail) { diskFails_ = fail; }

    // No operation changes the operating mode, the lock state or the allowed actions (COM-I4).

private:
    // ---- the secret ----
    struct OutMsg {
        unsigned    seq;
        MessageType type;
        long long   timeMs;
        std::string body;
        long long   lastSent;   // -1 = not sent since it was stored / loaded
    };

    std::string adcId_, path_;
    Clock       clock_;
    Transport   transport_;
    EventLog   &log_;
    long long   heartbeatMs_, retryMs_;
    int         missedLimit_;

    LinkStatus          link_ = LinkStatus::DEGRADED;
    int                 missed_ = 0;
    bool                awaitingReply_ = false;
    long long           lastHeartbeat_ = 0;
    unsigned            nextSeq_ = 1;
    unsigned            lastRxSeq_ = 0;
    unsigned            lastForwardedLogSeq_ = 0;
    bool                healthy_ = true;
    bool                diskFails_ = false;
    std::vector<OutMsg> outbox_;
    std::map<MessageType, Handler> handlers_;

    long long now() const;
    void transmit(MessageType type, unsigned seq, long long timeMs, const std::string &body);
    void transmitUnstored(MessageType type, const std::string &body);   // heartbeat, ACK
    void retransmit(bool onlyDue);
    void sendAck(unsigned seq, bool ok);
    bool saveOutbox();
    void loadOutbox();
};