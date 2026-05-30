# AGENTS.md

## Project type

Arduino sketch (ATmega328P). No build system, no package manager, no test framework. Compile with Arduino IDE or `arduino-cli`.

## Compile command

```bash
arduino-cli compile --fqbn arduino:avr:uno VRA-meter.ino
```

Or open `VRA-meter.ino` in Arduino IDE, select board, upload. The `.ino` filename **must** match the directory name.

## Zero external dependencies

All code uses only `<Arduino.h>` and `<math.h>`. Do not add library imports. I2C is bit-banged (not Wire library) — uses direct port manipulation on ATmega328P (PORTC, A4/A5) in `ads1115.cpp`.

## Key cross-file dependency

`VRA_Analyzer::begin(ADS1115 &adc)` in `vra.cpp:17` takes an ADC reference. The global `adc` object is declared in the `.ino` file and passed at startup. If you move or rename the `ADS1115 adc;` declaration, update the `vra.begin(adc)` call in `.ino`.

## MOSFET logic is active-low

`D7 LOW` = load ON, `D7 HIGH` = load OFF. Confusing if you expect standard logic. See `config.h`.

MOSFET is controlled via `VRA_Analyzer::setLoad()` and `killLoad()` helpers in `vra.cpp`. Do NOT use raw `digitalWrite(MOSFET_PIN, ...)` outside these helpers.

## Pull-up resistor on MOSFET gate (HARDWARE REQUIRED)

A **10kΩ resistor from gate to 5V is mandatory**. During Arduino boot, all pins are high-impedance INPUT for ~50ms. With active-low logic (LOW=ON, HIGH=OFF), the gate must be pulled HIGH to keep the load OFF. A pull-down to GND would turn the load ON during boot — dangerous.

## Config lives in config.h

Every tunable parameter (pins, PGA gains, timing, thresholds, safety limits) is in `config.h`. Do not hardcode values in `.cpp` files. Channel assignments (`ADS1115_CH_CURRENT`/`CH_VOLTAGE`) are in `ads1115.h` — they are driver-specific, not user-tunable.

Key constants:
- `VERSION` ("1.0") — firmware version for banner and future EEPROM storage
- `SAFETY_TIMEOUT_MS` (2000) — MOSFET kill-switch timeout
- `MIN_RELAX_MV` (4.0) — minimum relaxation amplitude for valid R²
- `V_AFTER_SETTLE_MS` (3) — wait for first conversion after MOSFET off
- `ADC_START_LEAD_MS` (2) — start conversion before target time

## PROGMEM log table

`vra.h:11` exports `LOG_TIME[30]` — pre-computed `ln(10)..ln(300)` stored in flash via `PROGMEM`. Read with `pgm_read_float(&LOG_TIME[i])`. Do not replace with runtime `log()` calls — ATmega328P has no FPU, each `log()` costs ~500µs.

## Centered regression (float precision fix)

R² is computed on **centered** voltage data: `ΔV[i] = V[i] - V[0]`. This prevents catastrophic cancellation in the variance formula on 32-bit float (7 significant digits). Never compute R² on raw ~3.85V values — the result will be garbage.

## V_instant: no delay after MOSFET off

`vra.cpp` reads V_instant immediately after `setLoad(false)`. Do NOT add a delay here. The ADS1115 at 860 SPS integrates over ~1.16ms, naturally filtering inductive ringing. Any delay lets chemical relaxation start, corrupting R_ohm.

## I2C: direct port manipulation (not digitalWrite)

`ads1115.cpp` uses direct PORTC manipulation for I2C — `sdaHigh()`, `sclLow()`, etc. This achieves ~200kHz clock vs ~20kHz with `digitalWrite`. Do NOT replace with `digitalWrite()` — it's 10x slower and will break the 10ms sample timing.

## ADC timing: start-before-wait pattern

Relaxation samples use non-overlapping conversion: start conversion `ADC_START_LEAD_MS` before target time, then read at target. The sequence is:
1. Wait until `target - ADC_START_LEAD_MS`
2. Call `adc_->startConversion()` (I2C write + conversion starts)
3. Wait until `target` (conversion completes in parallel)
4. Call `adc_->readResult()` (fast I2C read)

Do NOT call `adc_->readVoltage()` in the relaxation loop — it starts conversion AFTER the wait, offsetting all samples by ~4ms.

## No automated tests

Verification is manual: upload to board, open Serial Monitor at 115200 baud, connect battery, send any character to trigger measurement. R² output confirms the algorithm works.

## File roles

| File | Purpose |
|------|---------|
| `VRA-meter.ino` | Entry point, serial UI, measurement loop |
| `config.h` | All tunable parameters (pins, timing, thresholds) |
| `ads1115.h/.cpp` | Bit-banged I2C driver (direct port, ~200kHz) for ADS1115 ADC |
| `vra.h/.cpp` | VRA analysis: R², logarithmic regression, SOH grading |

## Common mistakes to avoid

- Changing `SHUNT_RESISTANCE` in config.h without updating the physical resistor
- Using Wire library instead of the bit-banged I2C in ads1115.cpp
- Forgetting `F()` macro on string literals (AVR RAM is 2KB)
- Moving MOSFET pin without updating both `config.h` and the pin init in `.ino`
- Adding delay between MOSFET off and V_instant read
- Computing R² on raw voltage instead of centered ΔV
- Using runtime `log()` instead of PROGMEM `LOG_TIME` array
- Reading `LOG_TIME[i]` directly — use `pgm_read_float(&LOG_TIME[i])` (PROGMEM, not RAM)
- Using `digitalWrite()` for I2C — use direct port manipulation (see ads1115.cpp)
- Calling `adc_->readVoltage()` in relaxation loop — use startConversion/readResult pattern
- Using raw `digitalWrite(MOSFET_PIN, ...)` — use setLoad()/killLoad() helpers
