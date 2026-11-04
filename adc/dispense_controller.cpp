#include "dispense_controller.h"

int DispenseController::maxDose(const std::string &medication) const
{
    for (const auto &l : limits_)
        if (l.medication == medication)
            return l.maxDose; // Returns the configured maximum dose limit for this drug
    return 0;                                       // Returns 0 if no limit is configured (meaning it will be rejected)
}

Result DispenseController::request(const std::string &medication, int quantity, const std::string &patient, bool emergency)
{
    Result perm = access_.isPermitted(emergency ? Action::EmergencyOverride : Action::Dispense);
    if (!perm.ok)               // Check 1: Validates user role permissions (normal vs emergency)
        return perm;
    if (p_.mode() != Mode::Normal)          // Check 2: Machine must be in Normal mode
        return Fail(Reason::WrongMode);
    if (state_ != TxState::Idle)
        return Fail(Reason::Busy);          // Check 3: Only one transaction at a time
    if (medication.empty() || patient.empty() || quantity < 1)
        return Fail(Reason::InvalidRequest);

    std::string location;
    Result found = inventory_.find(medication, quantity, location);
    if (!found.ok) // Check 5: Ensures enough stock exists and isn't expired
        return found;
    if (quantity > maxDose(medication)) // Check 6: Enforces safety dosage limits
        return Fail(Reason::ExceedsMaxDose); 
    if (!p_.unlock(location)) {              // Hardware action: physically unlocks the drawer/bin
        p_.reportFault("LOCK_FAILURE", location);
        return Fail(Reason::HardwareFault);
    }

    state_ = TxState::Unlocked; // Updates transaction state machine
    location_ = location; // Saves transaction context data
    medication_ = medication;
    patient_ = patient;
    emergency_ = emergency;
    if (emergency) { //logging and override
        p_.log("EMERGENCY_OVERRIDE", access_.currentUser(),
               "GRANTED medication=" + medication + " quantity=" + std::to_string(quantity) +
               " patient=" + patient);
        p_.raiseAlarm("EMERGENCY_OVERRIDE");
    }
    return Ok();
}

void DispenseController::drawerClosed()
{
    if (state_ != TxState::Unlocked) // Guard: only valid if a drawer is currently unlocked
        return;
    if (!p_.lock(location_)) // Re-secures the physical lock
        p_.reportFault("LOCK_FAILURE", location_);
    state_ = TxState::AwaitingConfirm; // Transitions state to wait for nurse/pharmacist count confirmation
} 

Result DispenseController::confirmQuantity(int quantityRemoved)
{
    if (state_ != TxState::AwaitingConfirm) // Guard: can only confirm if waiting for confirmation
        return Fail(Reason::NoTransaction);
    Result r = inventory_.commitDispense(location_, quantityRemoved, patient_, emergency_);
    if (r.ok)
        state_ = TxState::Idle;  // Resets transaction state back to Idle upon successful commit
    return r;
}

void DispenseController::abort()
{
    if (state_ == TxState::Unlocked && !p_.lock(location_)) // Safety: relocks the hardware if aborted mid-unlock
        p_.reportFault("LOCK_FAILURE", location_);
    state_ = TxState::Idle;              // Resets the state machine back to safe default (Idle)
}
