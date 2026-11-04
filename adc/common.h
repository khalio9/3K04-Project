// common.h - shared types used by AccessControl, InventoryManager, DispenseController
// and the hardware/mode modules (Mode, Role, Action, Result).
//
// NOTE: rebuilt by Rawaad from how Areeb's code and tests use it, because the original
// file was not uploaded. If Areeb uploads his own copy, keep the three Reasons marked
// "(Rawaad)" at the end of the Reason list - FaultMonitor/ModeManager return them.
#ifndef COMMON_H
#define COMMON_H

// Operating modes (FR-MOD-01..09). Owned by ModeManager.
enum class Mode { Startup, Normal, Maintenance, Fault };

enum class Role { Nurse, Pharmacist, Technician };

// Order matters: tests index the role/permission table by this order.
enum class Action { Dispense, EmergencyOverride, Replenish, EnterMaintenance, ClearFault };

enum class Reason {
    None,
    NotAuthenticated, NotPermitted, InvalidCredential, WrongPin, Locked,
    WrongMode, Busy, InvalidRequest,
    UnknownMedication, Expired, InsufficientStock, ExceedsMaxDose, InvalidQuantity,
    NoTransaction, HardwareFault,
    FaultActive,        // (Rawaad) a Critical fault is still present
    LocationBlocked,    // (Rawaad) a Warning blocks this location
    SelfTestFailed      // (Rawaad) the self-test did not pass
};

struct Result {
    bool ok;
    Reason reason;
};

inline Result Ok() { return Result{true, Reason::None}; }
inline Result Fail(Reason r) { return Result{false, r}; }

// One word per reason, so it can go straight into a log entry.
inline const char *reasonText(Reason r)
{
    static const char *names[] = {
        "NONE",
        "NOT_AUTHENTICATED", "NOT_PERMITTED", "INVALID_CREDENTIAL", "WRONG_PIN", "LOCKED",
        "WRONG_MODE", "BUSY", "INVALID_REQUEST",
        "UNKNOWN_MEDICATION", "EXPIRED", "INSUFFICIENT_STOCK", "EXCEEDS_MAX_DOSE", "INVALID_QUANTITY",
        "NO_TRANSACTION", "HARDWARE_FAULT",
        "FAULT_ACTIVE", "LOCATION_BLOCKED", "SELF_TEST_FAILED"
    };
    return names[static_cast<int>(r)];
}

inline const char *modeText(Mode m)
{
    static const char *names[] = {"STARTUP", "NORMAL", "MAINTENANCE", "FAULT"};
    return names[static_cast<int>(m)];
}

#endif
