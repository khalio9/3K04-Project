// EventLog.h - EventLog (M3.1): the append-only audit trail
//
// Responsibility: keep the append-only audit trail of every FR-LOG-01 event.
// Secret (the private: section): log file format, hash chain, and how durability is achieved.
// Spec: SPEC-LOG-1 (append), SPEC-LOG-2 (reads), SPEC-LOG-3 (load).
// Owner: Omer
#pragma once
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>
#include "Types.h"

// One log record (FR-LOG-02). The hash is NOT here: it is part of the secret.
struct LogEntry {
    unsigned    seq;
    long long   timeMs;
    std::string adcId;
    EventType   type;
    std::string userId;     // "" for system events (LOG-I4)
    Outcome     outcome;
    std::string details;    // "key=value;key=value"
};

// Thrown by append() when the write fails (row L3).
class LogStorageError : public std::runtime_error {
public:
    explicit LogStorageError(const std::string &m) : std::runtime_error(m) {}
};

class EventLog {
public:
    // Constructor = SPEC-LOG-3: loads any existing log (rows G1-G4).
    // A damaged file does not throw; it makes isHealthy() false (row G4).
    EventLog(const std::string &adcId, const std::string &path, Clock clock);
    ~EventLog();

    // SPEC-LOG-1. Adds one durable record with the next seq and the current time.
    // Throws std::invalid_argument (rows L1, L2) or LogStorageError (row L3).
    // When it throws, nothing was added, so the caller must leave its own state unchanged.
    LogEntry append(EventType type, Outcome outcome,
                    const std::string &userId = "", const std::string &details = "");

    // SPEC-LOG-2. Read only; returns copies (toSeq = 0 means "to the end").
    std::vector<LogEntry> entries(unsigned fromSeq = 1, unsigned toSeq = 0) const;
    unsigned lastSeq() const;      // 0 if the log is empty
    bool     isHealthy() const;    // read by FaultMonitor: Table 1 "Storage error"

    // Demo/test hook: pretend the disk has failed.
    void simulateDiskFailure(bool fail) { diskFails_ = fail; }

    // There is deliberately no function to edit or delete an entry (FR-LOG-03).
    EventLog(const EventLog &) = delete;
    EventLog &operator=(const EventLog &) = delete;

private:
    // ---- the secret ----
    std::string           adcId_;
    std::string           path_;
    Clock                 clock_;
    std::vector<LogEntry> entries_;
    uint64_t              lastHash_ = 0;   // head of the hash chain (LOG-I6)
    bool                  healthy_  = true;
    bool                  diskFails_ = false;
    std::FILE            *file_ = nullptr;

    void        load();                                         // SPEC-LOG-3
    static std::string lineFor(const LogEntry &e);              // file format
    static uint64_t    chainHash(uint64_t prev, const std::string &line);
    static bool        forbidden(const std::string &details);   // row L2
};