# AGENTS.md

## Project type

Arduino sketch (ATmega328P). No build system, no package manager, no test framework. Compile with Arduino IDE or `arduino-cli`.

## Compile command

```bash
arduino-cli compile --fqbn arduino:avr:uno ESR-VRA-meter.ino
```

Or open `ESR-VRA-meter.ino` in Arduino IDE, select board, upload. The `.ino` filename **must** match the directory name.

## Zero external dependencies

All code uses only `<Arduino.h>` and `<math.h>`. Do not add library imports. I2C is bit-banged (not Wire library) — hardcoded to A4/A5 in `ads1115.cpp:5-6`.

## Key cross-file dependency

`vra.cpp:6` declares `extern ADS1115 adc;` — it references the global `adc` object from the `.ino` file. If you move or rename the `ADS1115 adc;` declaration, update the extern.

## MOSFET logic is active-low

`D7 LOW` = load ON, `D7 HIGH` = load OFF. Confusing if you expect standard logic. See `config.h:5`.

## Config lives in config.h

Every tunable parameter (pins, PGA gains, timing, thresholds) is in `config.h`. Do not hardcode values in `.cpp` files.

## No automated tests

Verification is manual: upload to board, open Serial Monitor at 115200 baud, connect battery, send any character to trigger measurement. R² output confirms the algorithm works.

## File roles

| File | Purpose |
|------|---------|
| `ESR-VRA-meter.ino` | Entry point, serial UI, measurement loop |
| `config.h` | All hardware/tuning constants |
| `ads1115.h/.cpp` | Bit-banged I2C driver for ADS1115 ADC |
| `vra.h/.cpp` | VRA analysis: R², logarithmic regression, SOH grading |

## Common mistakes to avoid

- Changing `SHUNT_RESISTANCE` in config.h without updating the physical resistor
- Using Wire library instead of the bit-banged I2C in ads1115.cpp
- Forgetting `F()` macro on string literals ( AVR RAM is 2KB)
- Moving MOSFET pin without updating both `config.h` and the pin init in `.ino:99-100`
