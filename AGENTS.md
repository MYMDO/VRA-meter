# VRA Meter development

The root sketch directory must be named `VRA-meter` so Arduino finds `VRA-meter.ino`. The target is the 5 V, 16 MHz ATmega328P used in Uno Rev3 and classic Nano. Keep the firmware free of external Arduino libraries.

## Verify changes

```sh
bash tests/run.sh
arduino-cli compile --fqbn arduino:avr:uno --warnings all .
arduino-cli compile --fqbn arduino:avr:nano:cpu=atmega328 --warnings all .
```

Use Arduino AVR core 1.8.6. Follow the bench checks in [README.md](README.md) after changing timing, bus traffic or the load circuit.

## Code boundaries

- `VRA-meter.ino` owns serial commands, cooldown, and output. The sketch accesses the ADS1115 through the analyzer.
- `vra.cpp` validates the battery, captures the pulse and relaxation data, computes the resistance estimates, and fits the curve.
- `ads1115.cpp` owns I²C on PORTC (A4/SDA, A5/SCL), conversion timing, and signed raw-code scaling.
- `config.h` holds user-tunable electrical limits and timing. Review sample storage and the timing budget when changing the sample count or interval.

## Load and acquisition

- D7 HIGH turns on the documented low-side N-MOSFET. The physical gate pull-down holds it off during reset. Set the output latch LOW before configuring the pin as an output.
- Turn the load off before handling an acquisition error or writing to Serial. Keep transactions bounded while the load is on.
- Start the first post-release conversion immediately. Schedule subsequent conversions ahead of their nominal read times and confirm readiness before reading.
- Fit the centered sample data against recorded trigger-time proxies after acquisition. Report insufficient span with an unavailable R² and treat R² only as a fit metric.
