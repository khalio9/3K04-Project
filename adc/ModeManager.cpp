// ModeManager.cpp - transition guards and entry actions (the secret).
//
//   Startup     --pass-->        Normal        P2-M02
//   Startup     --fail-->        Fault         P2-M03
//   Normal      --maint-->       Maintenance   P2-M04 (technician, no transaction)
//   Maintenance --end-->         Startup       P2-M05 (then the self-test runs again)
//   any         --Critical-->    Fault         P2-M06
//   Fault       --recovered-->   Normal        P2-M08 (all Criticals cleared + self-test passed)
//   anything else: rejected, mode unchanged, logged  P2-M09
#include "ModeManager.h"

ModeManager::ModeManager(LockDriver &locks, EventLog &log) : locks_(locks), log_(log)
{
    change(Mode::Startup, "", "power_up");            // P2-M01 / FR-MOD-01
}

// Entry actions, the same for every transition
void ModeManager::change(Mode to, const std::string &user, const std::string &why)
{
    Mode from = mode_;
    mode_ = to;
    if (to != Mode::Normal) locks_.lockAll();         // only Normal may have a released drawer
    try {
        log_.append(EventType::MODE_CHANGE, Outcome::SUCCESS, user,
                    std::string("from=") + modeText(from) + ";to=" + modeText(to) + ";why=" + why);
    } catch (const std::exception &) {
        // A failed write makes log.isHealthy() false; FaultMonitor turns that into STORAGE_ERROR.
    }
}

// P2-M09: a rejected event leaves the mode alone but is still recorded
Result ModeManager::reject(const std::string &event, const std::string &user, Reason reason)
{
    try {
        log_.append(EventType::MODE_CHANGE, Outcome::REJECTED, user,
                    std::string("event=") + event + ";mode=" + modeText(mode_) + ";reason=" + reasonText(reason));
    } catch (const std::exception &) {}
    return Fail(reason);
}

Result ModeManager::startupResult(bool passed)
{
    if (mode_ != Mode::Startup) return reject("startup_result", "", Reason::WrongMode);
    change(passed ? Mode::Normal : Mode::Fault, "", passed ? "self_test_passed" : "self_test_failed");
    return passed ? Ok() : Fail(Reason::SelfTestFailed);
}

Result ModeManager::enterMaintenance(const std::string &user, bool technician, bool transactionActive)
{
    if (!technician)            return reject("enter_maintenance", user, Reason::NotPermitted);
    if (mode_ != Mode::Normal)  return reject("enter_maintenance", user, Reason::WrongMode);
    if (transactionActive)      return reject("enter_maintenance", user, Reason::Busy);   // FR-MOD-05
    change(Mode::Maintenance, user, "technician_request");
    return Ok();
}

Result ModeManager::endMaintenance(const std::string &user, bool technician)
{
    if (!technician)                return reject("end_maintenance", user, Reason::NotPermitted);
    if (mode_ != Mode::Maintenance) return reject("end_maintenance", user, Reason::WrongMode);
    change(Mode::Startup, user, "maintenance_ended");   // FR-MOD-06
    return Ok();
}

void ModeManager::enterFault(const std::string &why)
{
    if (mode_ == Mode::Fault) return;                    // logged once per actual change
    change(Mode::Fault, "", why);
}

Result ModeManager::recovered(const std::string &user)
{
    if (mode_ != Mode::Fault) return reject("recovered", user, Reason::WrongMode);
    change(Mode::Normal, user, "recovery_self_test_passed");
    return Ok();
}
