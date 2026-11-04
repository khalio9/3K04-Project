#include "access_control.h"
#include <functional>

static const int MAX_FAILED = 3;                    // FR-AUT-04 (default)

static std::size_t hashPin(const std::string &pin)
{
    return std::hash<std::string>()(pin); //converts plain text PIN into an integer hash value 
}

int AccessControl::find(const std::string &badge) const
{
    for (std::size_t i = 0; i < creds_.size(); i++) 
        if (creds_[i].badge == badge) //matches the badge ID against stored recorrds
            return static_cast<int>(i); //returns the index if found
    return -1; //else returns -1 if badge doesn't exist
}

void AccessControl::addCredential(const std::string &badge, const std::string &user, Role role,
                                  const std::string &pin)
{
    Credential c = {badge, user, role, hashPin(pin), false, 0};  // Initializes a credential struct
    int i = find(badge); 
    if (i < 0)
        creds_.push_back(c);                // Adds a new credential if it doesn't exist
    else
        creds_[i] = c;  // Updates the existing credential if found
}

void AccessControl::revoke(const std::string &badge)
{
    int i = find(badge);
    if (i < 0)
        return;
    creds_[i].revoked = true; // Flags the credential as invalid/revoked
    if (current_ == i)
        current_ = -1;  // Forces an immediate logout if the user is active
}

Result AccessControl::authenticate(const std::string &badge, const std::string &pin)
{
    int i = find(badge);
    Reason reason = Reason::None;

    if (i < 0 || creds_[i].revoked) // Check 1: Valid badge and not revoked
        reason = Reason::InvalidCredential;
    else if (creds_[i].failed >= MAX_FAILED) // Check 2: Account lockout check
        reason = Reason::Locked;
    else if (hashPin(pin) != creds_[i].pinHash) { // Check 3: PIN comparison
        creds_[i].failed++; // Increment consecutive failed attempt
        reason = Reason::WrongPin;
        if (creds_[i].failed >= MAX_FAILED) {
            reason = Reason::Locked; 
            p_.log("CREDENTIAL_LOCKOUT", creds_[i].user, badge); // Log security lockout event
        }
    }

    if (reason != Reason::None) {    // If any check failed, handle rejection
        p_.log("AUTH_ATTEMPT", i >= 0 ? creds_[i].user : "",std::string("FAILED ") + reasonText(reason) + " badge=" + badge);  // Record failure audit trail
        return Fail(reason);
    }
    creds_[i].failed = 0; // Reset failure counter on success
    current_ = i; // Set current active session index
    p_.log("AUTH_ATTEMPT", creds_[i].user, "SUCCESS badge=" + badge); // Record success audit trail
    return Ok();
}

std::string AccessControl::currentUser() const
{
    return current_ >= 0 ? creds_[current_].user : "";  // Returns user ID or empty string if unauthenticated
}

Result AccessControl::isPermitted(Action action) const
{
    if (current_ < 0)
        return Fail(Reason::NotAuthenticated); // Must be logged in to check permissions
    Role role = creds_[current_].role;
    bool allowed;
    if (role == Role::Nurse)
        allowed = action == Action::Dispense || action == Action::EmergencyOverride;
    else if (role == Role::Pharmacist)
        allowed = action == Action::Dispense || action == Action::EmergencyOverride || action == Action::Replenish;
    else
        allowed = action == Action::Replenish || action == Action::EnterMaintenance || action == Action::ClearFault; // Technician permissions
    return allowed ? Ok() : Fail(Reason::NotPermitted);
}
