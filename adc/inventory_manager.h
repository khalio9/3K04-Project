// InventoryManager: what medication is in which location.
// Hidden: the data structure. Traces: FR-INV-01..05.
// Assumes one location per medication and that staff enter replenishment details
// correctly (A9). Stock is kept in memory only.
#ifndef INVENTORY_MANAGER_H
#define INVENTORY_MANAGER_H
#include <string>
#include <vector>
#include "access_control.h"

struct StockRecord {
    std::string location, medication, lot;
    int expiry;                                     // day number
    int quantity;
};

class InventoryManager {
public:
    InventoryManager(Platform &p, AccessControl &access) : p_(p), access_(access) {}

    void load(const std::vector<StockRecord> &records) { stock_ = records; }

    // Finds the location holding enough unexpired stock.
    // Rejects: unknown medication, expired, insufficient stock.
    Result find(const std::string &medication, int quantity, std::string &location);

    // Decreases stock by the quantity actually removed and logs the dispense.
    Result commitDispense(const std::string &location, int quantityRemoved,
                          const std::string &patient, bool emergency);

    // Pharmacist or technician only. Called after the user has confirmed.
    Result replenish(const std::string &location, const std::string &medication,
                     const std::string &lot, int expiry, int quantity);

    StockRecord *get(const std::string &location);  // nullptr if unknown

private:
    Platform &p_;
    AccessControl &access_;
    std::vector<StockRecord> stock_;
};

#endif
