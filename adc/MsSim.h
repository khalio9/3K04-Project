// MsSim.h - Simulated Monitoring System (demo and tests only)
//
// Plays Part B's side so the TAs can see communication loss and recovery without a
// real network. It plugs into CommManager as its Transport, exactly where the real
// network would go.
// Owner: Omer
#pragma once
#include <set>
#include <string>
#include <vector>
#include "CommManager.h"

class MsSim {
public:
    MsSim(const std::string &adcId, Clock clock);

    void transport(const std::string &bytes);    // given to CommManager; drops everything while cut
    void pump(CommManager &comm);                // delivers replies (heartbeat replies, ACKs); once per tick

    void setLinkUp(bool up);                     // demo control: cut / restore the network
    bool linkUp() const;
    void sendPermissionUpdate(const std::string &body);   // demo control: MS sends an update

    int         receivedCount() const;           // for the demo screen
    std::string lastReceived() const;

private:
    std::string              adcId_;
    Clock                    clock_;
    bool                     up_ = true;
    unsigned                 seq_ = 1;           // the MS's own message counter
    std::set<unsigned>       seen_;              // ADC seqs already received (dedupe)
    int                      received_ = 0;
    std::string              last_ = "(nothing yet)";
    std::vector<std::string> replies_;           // waiting to be delivered

    void reply(MessageType type, const std::string &body);
};