# AGENTS.md

## Project type

Arduino sketch (ATmega328P). No build system, no package manager, no test framework. Compile with Arduino IDE or `arduino-cli`.

## Compile command

```bash
arduino-cli compile --fqbn arduino:avr:uno ESR-VRA-meter.ino
```

Or open `ESR-VRA-meter.ino` in Arduino IDE, select board, upload. The `.ino` filename **must** match the directory name.

## Zero external dependencies

All code uses only `<Arduino.h>` and `<math.h>`. Do not add library imports. I2C is bit-banged (not Wire library) — uses direct port manipulation on ATmega328P (PORTC, A4/A5) in `ads1115.cpp:11-20`.

## Key cross-file dependency

`vra.cpp:10` declares `extern ADS1115 adc;` — it references the global `adc` object from the `.ino` file. If you move or rename the `ADS1115 adc;` declaration, update the extern.

## MOSFET logic is active-low

`D7 LOW` = load ON, `D7 HIGH` = load OFF. Confusing if you expect standard logic. See `config.h:5`.

## Pull-up resistor on MOSFET gate (HARDWARE REQUIRED)

A **10kΩ resistor from gate to 5V is mandatory**. During Arduino boot, all pins are high-impedance INPUT for ~50ms. With active-low logic (LOW=ON, HIGH=OFF), the gate must be pulled HIGH to keep the load OFF. A pull-down to GND would turn the load ON during boot — dangerous.

## Config lives in config.h

Every tunable parameter (pins, PGA gains, timing, thresholds) is in `config.h`. Do not hardcode values in `.cpp` files.

## PROGMEM log table

`vra.h:8` exports `LOG_TIME[30]` — pre-computed `ln(10)..ln(300)` stored in flash via `PROGMEM`. Read with `pgm_read_float(&LOG_TIME[i])`. Do not replace with runtime `log()` calls — ATmega328P has no FPU, each `log()` costs ~500µs.

## Centered regression (float precision fix)

R² is computed on **centered** voltage data: `ΔV[i] = V[i] - V[0]`. This prevents catastrophic cancellation in the variance formula on 32-bit float (7 significant digits). Never compute R² on raw ~3.85V values — the result will be garbage.

## V_instant: no delay after MOSFET off

`vra.cpp:83` reads V_instant immediately after `digitalWrite(MOSFET_PIN, HIGH)`. Do NOT add a delay here. The ADS1115 at 860 SPS integrates over ~1.16ms, naturally filtering inductive ringing. Any delay lets chemical relaxation start, corrupting R_ohm.

## I2C: direct port manipulation (not digitalWrite)

`ads1115.cpp` uses direct PORTC manipulation for I2C — `sdaHigh()`, `sclLow()`, etc. (lines 11-20). This achieves ~200kHz clock vs ~20kHz with `digitalWrite`. Do NOT replace with `digitalWrite()` — it's 10x slower and will break the 10ms sample timing.

## ADC timing: start-before-wait pattern

Relaxation samples use non-overlapping conversion: start conversion ~2ms before target time, then read at target. See `vra.cpp:91-103`. The sequence is:
1. Wait until `target - 2ms`
2. Call `adc.startConversion()` (I2C write + conversion starts)
3. Wait until `target` (conversion completes in parallel)
4. Call `adc.readResult()` (fast I2C read)

Do NOT call `adc.readVoltage()` in the relaxation loop — it starts conversion AFTER the wait, offsetting all samples by ~4ms.

## No automated tests

Verification is manual: upload to board, open Serial Monitor at 115200 baud, connect battery, send any character to trigger measurement. R² output confirms the algorithm works.

## File roles

| File | Purpose |
|------|---------|
| `ESR-VRA-meter.ino` | Entry point, serial UI, measurement loop |
| `config.h` | All hardware/tuning constants |
| `ads1115.h/.cpp` | Bit-banged I2C driver (direct port, ~200kHz) for ADS1115 ADC |
| `vra.h/.cpp` | VRA analysis: R², logarithmic regression, SOH grading |

## Common mistakes to avoid

- Changing `SHUNT_RESISTANCE` in config.h without updating the physical resistor
- Using Wire library instead of the bit-banged I2C in ads1115.cpp
- Forgetting `F()` macro on string literals (AVR RAM is 2KB)
- Moving MOSFET pin without updating both `config.h` and the pin init in `.ino:99-100`
- Adding delay between MOSFET off and V_instant read
- Computing R² on raw voltage instead of centered ΔV
- Using runtime `log()` instead of PROGMEM `LOG_TIME` array
- Using `digitalWrite()` for I2C — use direct port manipulation (see ads1115.cpp)
- Calling `adc.readVoltage()` in relaxation loop — use startConversion/readResult pattern
