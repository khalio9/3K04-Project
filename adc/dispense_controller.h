// DispenseController: runs one dispense from request to completion.
// Hidden: the dose limits and the transaction states. Traces: FR-DSP-01..08.
// The hardware layer calls drawerClosed() when the drawer closes.
#ifndef DISPENSE_CONTROLLER_H
#define DISPENSE_CONTROLLER_H
#include <string>
#include <vector>
#include "inventory_manager.h"

enum class TxState { Idle, Unlocked, AwaitingConfirm };

struct DoseLimit {
    std::string medication;
    int maxDose;                                    // largest quantity per request
};

class DispenseController {
public:
    DispenseController(Platform &p, AccessControl &access, InventoryManager &inventory,
                       const std::vector<DoseLimit> &limits)
        : p_(p), access_(access), inventory_(inventory), limits_(limits) {}

    // The logged-in user must be permitted. emergency = true uses the override
    // permission, logs the override and raises an alarm to the MS.
    Result request(const std::string &medication, int quantity, const std::string &patient,
                   bool emergency);

    void drawerClosed();                            // re-locks and waits for the quantity
    Result confirmQuantity(int quantityRemoved);
    void abort();                                   // ends any transaction safely
    TxState state() const { return state_; }

private:
    int maxDose(const std::string &medication) const;

    Platform &p_;
    AccessControl &access_;
    InventoryManager &inventory_;
    std::vector<DoseLimit> limits_;
    TxState state_ = TxState::Idle;
    std::string location_, medication_, patient_;
    bool emergency_ = false;
};

#endif
