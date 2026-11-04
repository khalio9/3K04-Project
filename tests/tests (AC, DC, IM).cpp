// One test per rule. Build and run:  make test
#include <cstdio>
#include "fakes.h"

static int checks, failures, testsRun;
#define CHECK(c) do { checks++; if (!(c)) { failures++; std::printf("    FAIL line %d: %s\n", __LINE__, #c); } } while (0)
#define RUN(fn) do { testsRun++; fn(); } while (0)

static void login(System &s, const char *badge, const char *pin)
{
    s.access.logout();
    s.access.authenticate(badge, pin);
}

static bool allLocked(System &s)
{
    return s.fake.isLocked("A1") && s.fake.isLocked("A2") && s.fake.isLocked("A3") && s.fake.isLocked("B1");
}

// ---- AccessControl ----
static void test_unknown_badge()
{
    System s;
    CHECK(s.access.authenticate("NOPE", "0").reason == Reason::InvalidCredential);
    CHECK(s.access.currentUser() == "");
}

static void test_revoked_badge()
{
    System s;
    s.access.revoke("BADGE-N2");
    CHECK(s.access.authenticate("BADGE-N2", "2222").reason == Reason::InvalidCredential);
}

static void test_wrong_pin_then_success_resets_count()
{
    System s;
    CHECK(s.access.authenticate("BADGE-N1", "bad").reason == Reason::WrongPin);
    CHECK(s.access.authenticate("BADGE-N1", "bad").reason == Reason::WrongPin);
    CHECK(s.access.authenticate("BADGE-N1", "1111").ok);
    s.access.logout();
    s.access.authenticate("BADGE-N1", "bad");
    s.access.authenticate("BADGE-N1", "bad");
    CHECK(s.access.authenticate("BADGE-N1", "1111").ok);            // count was reset
}

static void test_lock_after_three_failures()
{
    System s;
    s.access.authenticate("BADGE-N1", "bad");
    s.access.authenticate("BADGE-N1", "bad");
    CHECK(s.access.authenticate("BADGE-N1", "bad").reason == Reason::Locked);
    CHECK(s.access.authenticate("BADGE-N1", "1111").reason == Reason::Locked);   // correct PIN too
    CHECK(s.access.currentUser() == "");
    CHECK(s.fake.countEvent("CREDENTIAL_LOCKOUT") == 1);
}

static void test_new_credential_clears_lock()
{
    System s;
    for (int i = 0; i < 3; i++) s.access.authenticate("BADGE-N1", "bad");
    s.access.addCredential("BADGE-N1", "nurse.amy", Role::Nurse, "1111");
    CHECK(s.access.authenticate("BADGE-N1", "1111").ok);
}

static void test_login_and_logout()
{
    System s;
    login(s, "BADGE-N1", "1111");
    CHECK(s.access.currentUser() == "nurse.amy");
    s.access.logout();
    CHECK(s.access.currentUser() == "");
    CHECK(s.access.isPermitted(Action::Dispense).reason == Reason::NotAuthenticated);
}

static void test_role_permissions()
{
    System s;
    static const bool allowed[3][5] = {
        {true, true, false, false, false},      // nurse
        {true, true, true, false, false},       // pharmacist
        {false, false, true, true, true}        // technician
    };
    const char *badge[3] = {"BADGE-N1", "BADGE-P1", "BADGE-T1"};
    const char *pin[3] = {"1111", "3333", "4444"};
    for (int r = 0; r < 3; r++) {
        login(s, badge[r], pin[r]);
        for (int a = 0; a < 5; a++)
            CHECK(s.access.isPermitted(static_cast<Action>(a)).ok == allowed[r][a]);
    }
}

static void test_revoking_logged_in_user_logs_out()
{
    System s;
    login(s, "BADGE-N1", "1111");
    s.access.revoke("BADGE-N1");
    CHECK(s.access.currentUser() == "");
}

static void test_attempts_logged_and_pin_never_logged()
{
    System s;
    s.access.authenticate("BADGE-N1", "9999");
    s.access.authenticate("BADGE-N1", "1111");
    CHECK(s.fake.countEvent("AUTH_ATTEMPT") == 2);
    CHECK(!s.fake.logContains("9999"));
    CHECK(!s.fake.logContains("1111"));
}

// ---- InventoryManager ----
static void test_find_rejections_and_success()
{
    System s;
    std::string loc;
    CHECK(s.inventory.find("nothing", 1, loc).reason == Reason::UnknownMedication);
    CHECK(s.inventory.find("heparin-5000u", 1, loc).reason == Reason::Expired);
    CHECK(s.inventory.find("morphine-10mg", 21, loc).reason == Reason::InsufficientStock);
    CHECK(s.inventory.find("morphine-10mg", 20, loc).ok && loc == "A1");
}

static void test_commit_dispense()
{
    System s;
    login(s, "BADGE-N1", "1111");
    CHECK(s.inventory.commitDispense("A1", 21, "P", false).reason == Reason::InvalidQuantity);
    CHECK(s.inventory.commitDispense("A1", -1, "P", false).reason == Reason::InvalidQuantity);
    CHECK(s.inventory.get("A1")->quantity == 20 && s.fake.countEvent("DISPENSE") == 0);
    CHECK(s.inventory.commitDispense("A1", 2, "P", false).ok);
    CHECK(s.inventory.get("A1")->quantity == 18 && s.fake.countEvent("DISPENSE") == 1);
}

static void test_replenish_rules()
{
    System s;
    CHECK(s.inventory.replenish("B1", "saline", "L9", FAKE_TODAY + 100, 10).reason == Reason::NotAuthenticated);
    login(s, "BADGE-N1", "1111");
    CHECK(s.inventory.replenish("B1", "saline", "L9", FAKE_TODAY + 100, 10).reason == Reason::NotPermitted);
    login(s, "BADGE-P1", "3333");
    CHECK(s.inventory.replenish("ZZ", "saline", "L9", FAKE_TODAY + 100, 10).reason == Reason::InvalidRequest);
    CHECK(s.inventory.replenish("B1", "", "L9", FAKE_TODAY + 100, 10).reason == Reason::InvalidRequest);
    CHECK(s.inventory.replenish("B1", "saline", "L9", FAKE_TODAY + 100, 0).reason == Reason::InvalidRequest);
    CHECK(s.inventory.get("B1")->quantity == 0);
    CHECK(s.inventory.replenish("B1", "saline", "L9", FAKE_TODAY + 100, 10).ok);
    CHECK(s.inventory.get("B1")->quantity == 10 && s.inventory.get("B1")->medication == "saline");
    login(s, "BADGE-T1", "4444");                                   // a technician may replenish
    CHECK(s.inventory.replenish("B1", "saline", "L9", FAKE_TODAY + 100, 5).ok);
    CHECK(s.inventory.get("B1")->quantity == 15 && s.fake.countEvent("REPLENISH") == 2);
}

// ---- DispenseController ----
static void test_not_logged_in()
{
    System s;
    CHECK(s.dispense.request("morphine-10mg", 1, "PT-1", false).reason == Reason::NotAuthenticated);
    CHECK(allLocked(s));
}

static void test_technician_cannot_dispense()
{
    System s;
    login(s, "BADGE-T1", "4444");
    CHECK(s.dispense.request("morphine-10mg", 1, "PT-1", false).reason == Reason::NotPermitted);
    CHECK(s.dispense.request("morphine-10mg", 1, "PT-1", true).reason == Reason::NotPermitted);
    CHECK(allLocked(s));
}

static void test_only_in_normal_mode()
{
    System s;
    login(s, "BADGE-N1", "1111");
    s.fake.mode_ = Mode::Maintenance;
    CHECK(s.dispense.request("morphine-10mg", 1, "PT-1", false).reason == Reason::WrongMode);
    s.fake.mode_ = Mode::Fault;
    CHECK(s.dispense.request("morphine-10mg", 1, "PT-1", false).reason == Reason::WrongMode);
    CHECK(allLocked(s));
}

static void test_busy_and_invalid_requests()
{
    System s;
    login(s, "BADGE-N1", "1111");
    CHECK(s.dispense.request("morphine-10mg", 0, "PT-1", false).reason == Reason::InvalidRequest);
    CHECK(s.dispense.request("", 1, "PT-1", false).reason == Reason::InvalidRequest);
    CHECK(s.dispense.request("morphine-10mg", 1, "", false).reason == Reason::InvalidRequest);
    CHECK(s.dispense.request("morphine-10mg", 1, "PT-1", false).ok);
    CHECK(s.dispense.request("morphine-10mg", 1, "PT-1", false).reason == Reason::Busy);
}

static void test_inventory_reasons_pass_through()
{
    System s;
    login(s, "BADGE-N1", "1111");
    CHECK(s.dispense.request("nothing", 1, "PT-1", false).reason == Reason::UnknownMedication);
    CHECK(s.dispense.request("heparin-5000u", 1, "PT-1", false).reason == Reason::Expired);
    CHECK(s.dispense.request("morphine-10mg", 21, "PT-1", false).reason == Reason::InsufficientStock);
    CHECK(allLocked(s));
}

static void test_max_dose_and_missing_limit()
{
    System s;
    login(s, "BADGE-N1", "1111");
    CHECK(s.dispense.request("morphine-10mg", 3, "PT-1", false).reason == Reason::ExceedsMaxDose);
    DispenseController noMorphineLimit(s.fake, s.access, s.inventory, {{"heparin-5000u", 1}});
    CHECK(noMorphineLimit.request("morphine-10mg", 1, "PT-1", false).reason == Reason::ExceedsMaxDose);
    CHECK(allLocked(s));
}

static void test_hardware_fault_stays_locked_and_reports()
{
    System s;
    login(s, "BADGE-N1", "1111");
    s.fake.broken.insert("A1");
    CHECK(s.dispense.request("morphine-10mg", 1, "PT-1", false).reason == Reason::HardwareFault);
    CHECK(s.dispense.state() == TxState::Idle);
    CHECK(s.fake.faults.size() == 1 && s.fake.faults[0] == "LOCK_FAILURE");
    s.fake.broken.clear();
    CHECK(allLocked(s));
}

static void test_unlocks_only_requested_location()
{
    System s;
    login(s, "BADGE-N1", "1111");
    CHECK(s.dispense.request("morphine-10mg", 1, "PT-1", false).ok);
    CHECK(!s.fake.isLocked("A1"));
    CHECK(s.fake.isLocked("A2") && s.fake.isLocked("A3") && s.fake.isLocked("B1"));
}

static void test_authenticate_dispense_log()
{
    System s;
    login(s, "BADGE-N1", "1111");
    CHECK(s.dispense.request("morphine-10mg", 2, "PT-1", false).ok);
    CHECK(s.dispense.state() == TxState::Unlocked);
    CHECK(s.fake.openDrawer("A1"));
    s.dispense.drawerClosed();
    CHECK(s.dispense.state() == TxState::AwaitingConfirm);
    CHECK(s.fake.isLocked("A1"));                                   // re-locked when the drawer closed
    CHECK(s.inventory.get("A1")->quantity == 20);                   // not decreased yet
    CHECK(s.dispense.confirmQuantity(2).ok);
    CHECK(s.inventory.get("A1")->quantity == 18 && s.dispense.state() == TxState::Idle);
    CHECK(s.fake.countEvent("DISPENSE") == 1);
    CHECK(s.fake.logContains("patient=PT-1"));
    CHECK(allLocked(s));
}

static void test_locked_drawer_cannot_open()
{
    System s;
    CHECK(!s.fake.openDrawer("A1"));
}

static void test_confirm_rules()
{
    System s;
    login(s, "BADGE-N1", "1111");
    CHECK(s.dispense.confirmQuantity(1).reason == Reason::NoTransaction);
    s.dispense.request("morphine-10mg", 1, "PT-1", false);
    CHECK(s.dispense.confirmQuantity(1).reason == Reason::NoTransaction);    // drawer not closed yet
    s.dispense.drawerClosed();
    CHECK(s.dispense.confirmQuantity(99).reason == Reason::InvalidQuantity);
    CHECK(s.dispense.state() == TxState::AwaitingConfirm);
    CHECK(s.dispense.confirmQuantity(1).ok);
}

static void test_emergency_override()
{
    System s;
    login(s, "BADGE-N1", "1111");
    CHECK(s.dispense.request("morphine-10mg", 1, "PT-1", true).ok);
    CHECK(s.fake.alarms.size() == 1 && s.fake.alarms[0] == "EMERGENCY_OVERRIDE");
    CHECK(s.fake.countEvent("EMERGENCY_OVERRIDE") == 1);
    s.dispense.abort();
    CHECK(s.dispense.request("morphine-10mg", 3, "PT-1", true).reason == Reason::ExceedsMaxDose);
    CHECK(s.dispense.request("heparin-5000u", 1, "PT-1", true).reason == Reason::Expired);
    s.fake.mode_ = Mode::Fault;
    CHECK(s.dispense.request("morphine-10mg", 1, "PT-1", true).reason == Reason::WrongMode);
    CHECK(s.fake.alarms.size() == 1);                               // only the granted override alarmed
}

static void test_abort()
{
    System s;
    login(s, "BADGE-N1", "1111");
    s.dispense.abort();                                             // harmless when idle
    s.dispense.request("morphine-10mg", 1, "PT-1", false);
    s.dispense.abort();
    CHECK(s.dispense.state() == TxState::Idle);
    CHECK(allLocked(s));
    CHECK(s.inventory.get("A1")->quantity == 20);
}

int main()
{
    RUN(test_unknown_badge);
    RUN(test_revoked_badge);
    RUN(test_wrong_pin_then_success_resets_count);
    RUN(test_lock_after_three_failures);
    RUN(test_new_credential_clears_lock);
    RUN(test_login_and_logout);
    RUN(test_role_permissions);
    RUN(test_revoking_logged_in_user_logs_out);
    RUN(test_attempts_logged_and_pin_never_logged);
    RUN(test_find_rejections_and_success);
    RUN(test_commit_dispense);
    RUN(test_replenish_rules);
    RUN(test_not_logged_in);
    RUN(test_technician_cannot_dispense);
    RUN(test_only_in_normal_mode);
    RUN(test_busy_and_invalid_requests);
    RUN(test_inventory_reasons_pass_through);
    RUN(test_max_dose_and_missing_limit);
    RUN(test_hardware_fault_stays_locked_and_reports);
    RUN(test_unlocks_only_requested_location);
    RUN(test_authenticate_dispense_log);
    RUN(test_locked_drawer_cannot_open);
    RUN(test_confirm_rules);
    RUN(test_emergency_override);
    RUN(test_abort);
    std::printf("%d tests, %d checks, %d failed\n", testsRun, checks, failures);
    return failures ? 1 : 0;
}
