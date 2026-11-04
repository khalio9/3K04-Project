#include "inventory_manager.h"

StockRecord *InventoryManager::get(const std::string &location)
{
    for (auto &r : stock_) // Iterates through all items in the inventory stock container
        if (r.location == location) // Checks if the record's location matches the requested location string
            return &r; // Returns a pointer to the matching stock record
    return nullptr;  // Returns nullptr if no matching location is found
}


Result InventoryManager::find(const std::string &medication, int quantity, std::string &location)
{
    for (auto &r : stock_) {
        if (r.medication != medication)
            continue;
        if (r.quantity > 0 && p_.today() > r.expiry) // Check 1: Expiration check against current platform date
            return Fail(Reason::Expired);
        if (r.quantity < quantity) // Check 2: Stock sufficiency check
            return Fail(Reason::InsufficientStock);
        location = r.location;          // Assigns the valid location output variable
        return Ok();                    // Returns success if all checks pass
    }
    return Fail(Reason::UnknownMedication); // Returns error if medication was never found in stock
}

Result InventoryManager::commitDispense(const std::string &location, int quantityRemoved, const std::string &patient, bool emergency)
{
    StockRecord *r = get(location);
    if (r == nullptr)
        return Fail(Reason::InvalidRequest);
    if (quantityRemoved < 0 || quantityRemoved > r->quantity) // Validates physical limits of removal
        return Fail(Reason::InvalidQuantity);
    // Log the dispense transaction audit trail    
    p_.log("DISPENSE", access_.currentUser(), 
           "COMPLETE location=" + location + " medication=" + r->medication +
           " quantity=" + std::to_string(quantityRemoved) + " patient=" + patient +
           " emergency=" + std::to_string(emergency ? 1 : 0)); 
    r->quantity -= quantityRemoved; // Deducts the removed quantity from inventory
    return Ok();
}

Result InventoryManager::replenish(const std::string &location, const std::string &medication, const std::string &lot, int expiry, int quantity)
{
    Result perm = access_.isPermitted(Action::Replenish);  // Check user permissions first
    if (!perm.ok)
        return perm;
    StockRecord *r = get(location);
    if (r == nullptr || medication.empty() || lot.empty() || quantity < 1)
        return Fail(Reason::InvalidRequest); // Validates parameters and stock location
    // Log the restocking action
    p_.log("REPLENISH", access_.currentUser(),
           "COMPLETE location=" + location + " medication=" + medication + " lot=" + lot +
           " expiry=" + std::to_string(expiry) + " quantity=" + std::to_string(quantity));
    r->medication = medication; // Update slot details with new batch info
    r->lot = lot;
    r->expiry = expiry;
    r->quantity += quantity; // Increase inventory count
    return Ok();
}
