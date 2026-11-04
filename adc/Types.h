// Types.h - shared enums and default parameters (M3.3 Types)
// Secret: where the defaults are set.
// Owner: Omer
#pragma once
#include <functional>
#include <string>

// Event types that must be logged (FR-LOG-01)
enum class EventType {
    AUTH_ATTEMPT, CREDENTIAL_LOCKOUT, DISPENSE, REPLENISH, EMERGENCY_OVERRIDE,
    ALARM, FAULT_RAISED, FAULT_CLEARED, MODE_CHANGE, COMM_LOST, COMM_RESTORED
};

// Outcome of a logged event (FR-LOG-02)
enum class Outcome {
    SUCCESS, FAILED, REJECTED, GRANTED, COMPLETE, COMPLETE_MISMATCH, ABANDONED
};

// The time source, in milliseconds, never decreasing (NAT-2). Supplied by the Clock module.
using Clock = std::function<long long()>;

// Default parameters (A11: placeholders, not clinically validated)
namespace Defaults {
    const long long HEARTBEAT_MS = 30000;   // FR-COM-03
    const int       MISSED_LIMIT = 3;       // Table 1: comm loss
    const long long RETRY_MS     = 5000;    // resend unACKed messages
}

inline std::string toString(EventType t)
{
    static const char *names[] = {
        "AUTH_ATTEMPT", "CREDENTIAL_LOCKOUT", "DISPENSE", "REPLENISH", "EMERGENCY_OVERRIDE",
        "ALARM", "FAULT_RAISED", "FAULT_CLEARED", "MODE_CHANGE", "COMM_LOST", "COMM_RESTORED"
    };
    return names[static_cast<int>(t)];
}

inline std::string toString(Outcome o)
{
    static const char *names[] = {
        "SUCCESS", "FAILED", "REJECTED", "GRANTED", "COMPLETE", "COMPLETE_MISMATCH", "ABANDONED"
    };
    return names[static_cast<int>(o)];
}