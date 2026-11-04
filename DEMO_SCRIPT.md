# Deliverable 2 demo script (Part A - ADC)

Build once before the lab: `make` (demo) and `make test` (all tests must say ALL PASSED).
Start fresh: delete `adc_log.txt` and `adc_outbox.txt`, then run `./adc_demo`.

Demo users: `BADGE-N1 1111` nurse, `BADGE-P1 3333` pharmacist, `BADGE-T1 4444` technician.
Locations: A1 morphine, A2 ondansetron, A3 heparin (expired), B1 empty.

## 1. Startup -> Normal (FR-MOD-01, -02)
The screen shows `Mode: NORMAL`, every bay `locked/closed`. `log 5` shows STARTUP -> NORMAL and the self-test PASS.

## 2. Sc1: authenticate -> dispense -> log
```
login BADGE-N1 1111
dispense morphine-10mg 2 PT-1     # only A1 shows UNLOCKED (FR-DSP-04)
open A1
close A1                          # A1 re-locks by itself (FR-DSP-05)
confirm 2                         # stock 20 -> 18 only now (FR-DSP-06, FR-INV-02)
log 5                             # DISPENSE ... patient_id=PT-1
```
Rejections to show if asked: `dispense morphine-10mg 3 PT-1` (max dose), `dispense heparin-5000u 1 PT-1` (expired).

## 3. Emergency override (FR-DSP-07, -08)
`override morphine-10mg 1 PT-2` -> the MS line shows the ALARM. Then `open A1`, `close A1`, `confirm 1`.

## 4. Sc3: hardware fault -> Fault mode -> recovery
```
dispense morphine-10mg 1 PT-1
open A1
jamlock A1
close A1          # CRITICAL LOCK_FAILURE at A1, Mode: FAULT, MS gets the alarm
confirm 1         # the record still completes so stock stays correct
dispense ondansetron-4mg 1 PT-1    # Rejected: WRONG_MODE
login BADGE-T1 4444
clearfault        # Rejected: FAULT_ACTIVE (acknowledged, still present)
unjam A1
clearfault        # cleared
normal            # self-test passes -> NORMAL (FR-MOD-08)
```

## 5. Warnings block one location only
`sensorfail A3` -> A3 shows `unknown/unknown(BLOCKED)`, mode stays NORMAL. `fixsensor A3` clears it by itself.
Drawer left open: `login BADGE-N1 1111`, `dispense ondansetron-4mg 1 PT-1`, `open A2`, `wait 10` -> WARNING DRAWER_OPEN_TOO_LONG; `close A2` clears it.

## 6. Sc4: communication loss (FR-COM-05..09)
`cut`, `wait 120` -> Link DEGRADED, mode still NORMAL, dispensing still works, unsent messages grow.
`restore`, `wait 35` -> CONNECTED, unsent messages back to 0.

## 7. Maintenance (FR-MOD-05, -06)
`login BADGE-T1 4444`, `maint` -> MAINTENANCE, all locked. `normal` -> STARTUP, self-test, NORMAL.
A nurse typing `maint` is rejected (NOT_PERMITTED).

## 8. Storage error
`failstorage` -> CRITICAL STORAGE_ERROR, FAULT. `fixstorage`, then as technician `clearfault`, `normal`.

## On-the-spot change questions (where it lands)
| Change | Only this changes |
|---|---|
| New lock or sensor hardware | a new class implementing `Hardware` (replaces SimHardware in main.cpp) |
| Different stale-sensor time | `Defaults::SENSOR_STALE_MS` (DrawerSensor) |
| Drawer timeout 10 s -> 20 s | `Defaults::DRAWER_TIMEOUT_MS` (LockDriver) |
| New fault type or severity | `FaultMonitor::detect()` |
| New mode or transition | ModeManager |
| MS message format | Protocol.cpp (Omer) |
