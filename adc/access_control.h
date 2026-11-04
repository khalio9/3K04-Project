// AccessControl: who is logged in and what their role allows.
// Hidden: how credentials are stored and checked, the lockout count, the permission
// table. Traces: FR-AUT-01..05.
#ifndef ACCESS_CONTROL_H
#define ACCESS_CONTROL_H
#include <string>
#include <vector>
#include "platform.h"

class AccessControl {
public:
    explicit AccessControl(Platform &p) : p_(p) {}

    // Adds a credential, or replaces it (which also clears a lock).
    void addCredential(const std::string &badge, const std::string &user, Role role, const std::string &pin);
    void revoke(const std::string &badge);          // also logs the user out if logged in

    Result authenticate(const std::string &badge, const std::string &pin);
    void logout() { current_ = -1; }
    std::string currentUser() const;                // "" if nobody is logged in
    Result isPermitted(Action action) const;        // for the logged-in user

private:
    struct Credential {
        std::string badge, user;
        Role role;
        std::size_t pinHash;                        // the PIN itself is never stored
        bool revoked;
        int failed;                                 // consecutive failed attempts
    };
    int find(const std::string &badge) const;

    Platform &p_;
    std::vector<Credential> creds_;
    int current_ = -1;                              // index of the logged-in credential
};

#endif
