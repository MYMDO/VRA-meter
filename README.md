# VRA Meter

VRA Meter is an experimental 5 V Arduino instrument for comparing the short-pulse response of single Li-ion cells under matched conditions. Its primary result is near-end pulse resistance (DCIR estimate). It also reports the voltage recovery and exports the time-stamped curve as CSV. See the [measurement audit](docs/measurement-audit.md) for the method, limits, and proposed bench acceptance criteria.

**Before upgrading from v1.0, disconnect the cell and check the load wiring.** Versions 1.1 and later use D7 **HIGH = ON**, with a gate pull-down to GND. The v1.0 instructions specified a gate pull-up and the opposite control polarity. Rewire any earlier build to the circuit below before powering or uploading.

## Hardware

Use a 5 V, 16 MHz Arduino Uno Rev3 or classic Nano with an ATmega328P, an ADS1115 module, a logic-level N-channel MOSFET such as IRLZ44N, a 10 Ω / 5 W load resistor, a 0.1 Ω / 1% shunt, and a cell holder. The ADS1115 and Arduino share a regulated 5 V supply and ground.

**Load current path**

BAT+ → load resistor → MOSFET drain → MOSFET source → shunt → BAT−.

| Connection | Wiring |
|---|---|
| Ground | Join BAT−, Arduino GND, ADS1115 GND and the low end of the shunt. |
| ADC power | ADS1115 VDD → Arduino 5 V. Place 100 nF between VDD and GND close to the ADC if the module lacks local decoupling. |
| I²C | A4 → SDA, A5 → SCL. Use one effective pull-up of about 4.7 kΩ to 5 V on each line, accounting for resistors already on the module. |
| ADC address | ADDR → GND for the default 0x48. |
| MOSFET gate | D7 → 330 Ω → gate. Place **10 kΩ from gate to GND** to keep the switch off while the MCU pin is high-impedance. |
| A0 and A1 | A0 → shunt high side (MOSFET source). A1 → shunt low side. Sense at the resistor terminals. |
| A2 and A3 | A2 → BAT+, A3 → BAT− using separate voltage-sense wires at the cell terminals. Add 100 kΩ from A2 and another 100 kΩ from A3 to circuit ground to define the open-input voltage. |

Check the MOSFET package pinout. Keep load current out of the sense wires. At 4.25 V, the default 10 Ω resistor and 0.1 Ω shunt draw roughly 0.42 A before losses. The cell must be rated for the configured voltage and current limits and have known condition and polarity. Follow the cell manufacturer's limits if they are stricter.

Keep each ADS1115 analog input within its supply rails during operation. The ±6.144 V PGA setting controls ADC scaling. Wire and power the tester before connecting the cell, and disconnect the cell before turning off or unplugging the tester. This circuit requires correct polarity, one cell and a powered tester. Software cutoffs take effect after the MOSFET starts and at the times the firmware samples. For use outside supervised bench testing, independent fault protection and validation remain necessary.

Hardware references · [ADS1115 datasheet](https://www.ti.com/lit/ds/symlink/ads1115.pdf) · [Arduino Uno pinout](https://content.arduino.cc/assets/Pinout-UNOrev3_latest.pdf) · [IRLZ44N](https://www.infineon.com/part/IRLZ44N)

## Build and use

1. Clone the repository and open `VRA-meter/VRA-meter.ino` in Arduino IDE.
2. Select Arduino Uno or classic Arduino Nano (ATmega328P) and upload with the cell disconnected. For Nano, select the processor/bootloader corresponding to your board when uploading.
3. Open Serial Monitor at **115200 baud**. Verify the idle gate state and test first on a current-limited bench supply before connecting a cell.

| Serial command | Action |
|---|---|
| `m` | Run one measurement. |
| `a` | Toggle automatic measurements. An error turns automatic mode off. |
| `c` | Export the curve from the latest successful attempt. Every new attempt replaces or invalidates the prior curve. |
| `?` | Show commands. |

Line endings are optional. Automatic and manual attempts share a one-second cooldown measured from the end of each attempt. During a measurement the firmware performs acquisition without processing commands. An `a` command received during that interval is processed after the result.

For Arduino CLI, run from the repository directory:

```sh
arduino-cli core install arduino:avr@1.8.6
arduino-cli compile --fqbn arduino:avr:uno --warnings all .
arduino-cli compile --fqbn arduino:avr:nano:cpu=atmega328 --warnings all .
```

## Interpret the measurements

| Output | Meaning |
|---|---|
| Pulse R | `(V_open − V_load) / I_load`. Near-end pulse resistance, the primary comparison metric. |
| `V_open`, `V_load`, `V_first`, `V_final` | Pre-pulse terminal voltage, loaded voltage near pulse end, first conversion after release, and last recovery sample. |
| Load current and timing | The late shunt reading has its pre-pulse zero subtracted. The screen shows the commanded pulse width and ADC trigger times for late current, late voltage and first recovery voltage. Current and voltage use successive conversions. |
| Step R | `(V_first − V_load) / I_load`. The first post-release conversion integrates over a finite window. |
| Recovery R | `(V_final − V_first) / I_load`. The additional voltage recovery in the recorded window, expressed in ohms. |
| Log fit R² | Descriptive agreement with a logarithmic fit. A recovery-curve span below 4 mV produces `N/A`. Capacity, state of health, and safety are outside the measurement. |
| Curve CSV | Columns are `nominal_time_ms,voltage_V,trigger_us_after_release`. The new third column records the end of the ADC start transaction and is used for the fit. This is a proxy for conversion start. The integration window comes before the nominal read. |

Use manual measurements for cell comparisons. Match the cell model, charge state, temperature, fixture, pulse protocol and rest period. The one-second automatic cooldown only limits how often the instrument pulses. For precision work, record stability and use a separate capacity test or AC impedance measurement if those quantities are needed. If a resistance estimate is negative, check contacts and baseline drift before interpretation.

## Verification and readiness

The firmware has no external Arduino libraries. GitHub Actions compiles Uno and classic Nano and runs host regression tests. Run the host tests locally with `bash tests/run.sh`.

| Bench check | Expected outcome |
|---|---|
| Reset and programming, first with a current-limited supply | D7 and the MOSFET gate stay off until a measurement is requested. |
| Normal pulse on a current-limited supply | Scope shows approximately 200 ms on D7 and the expected current for the load. ADS1115 transactions are acknowledged and sample timing is stable. |
| ADC or load faults | Disconnect the ADC or interrupt the load path in a controlled bench setup. Expect a specific error, D7 off, no exported curve, and auto mode stopped. |
| Voltage outside the configured range | No pulse before initial validation. If voltage crosses the boundary under test, the next loaded voltage check aborts. |
| Known DC source and reference shunt | Compare both ADC channels against a DMM. Insert a known series resistor between the source and the test fixture, inside the voltage-sense taps. Compare the change in Pulse R with the reference value. |

Bench validation is pending. Validate the wiring, time windows, repeatability, and fault response before using measurements to make decisions.

## License

[MIT](LICENSE)
