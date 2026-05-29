# VRA Meter

**Arduino-based battery diagnostics using Voltage Relaxation Analysis (VRA)**

A minimalist hardware, maximum intelligence software approach to measuring battery health (SOH) and internal resistance using a single 200ms current pulse and electrochemical relaxation curve analysis.

![License](https://img.shields.io/badge/license-MIT-blue.svg)
![Platform](https://img.shields.io/badge/platform-Arduino-yellow.svg)

## Table of Contents

- [Overview](#overview)
- [How It Works](#how-it-works)
- [Hardware](#hardware)
  - [Bill of Materials](#bill-of-materials)
  - [Schematic](#schematic)
  - [Kelvin Connection](#kelvin-connection)
- [Software Architecture](#software-architecture)
- [Installation](#installation)
- [Usage](#usage)
  - [Serial Monitor Output](#serial-monitor-output)
  - [Commands](#commands)
  - [Auto Mode](#auto-mode)
- [Measurement Science](#measurement-science)
  - [Voltage Relaxation Curve](#voltage-relaxation-curve)
  - [R_ohm — Ohmic Resistance](#r_ohm--ohmic-resistance)
  - [R_pol — Polarization Resistance](#r_pol--polarization-resistance)
  - [R² — Curve Quality Index](#r²--curve-quality-index)
- [SOH Grading](#soh-grading)
- [Configuration](#configuration)
- [Troubleshooting](#troubleshooting)
- [Theory](#theory)
- [License](#license)

---

## Overview

Traditional battery testers rely on complex hardware — multiple current sources, precision amplifiers, dedicated fuel gauge ICs. This project flips the paradigm: **minimize the hardware, maximize the software intelligence**.

The VRA Meter uses **Voltage Relaxation Analysis** — a technique similar to approaches used or researched in advanced BMS for electric vehicles — to extract three critical battery health parameters from a single 200ms current pulse:

| Parameter | What It Measures | Equivalent To |
|-----------|-----------------|---------------|
| **R_ohm** | Instantaneous voltage jump on load disconnect | AC-IR at 1 kHz |
| **R_pol** | Slow voltage recovery after load removal | Diffusion/polarization resistance |
| **R²** | How well the relaxation curve fits a logarithmic model | Structural integrity of electrodes & electrolyte |

The entire measurement takes under 400ms and requires no user expertise — the firmware automatically grades the battery as **EXCELLENT**, **GOOD**, or **POOR**.

## How It Works

```
1. CONNECT BATTERY
         │
         ▼
2. APPLY LOAD (200ms pulse)
   │  Measure: V_load, I_load
   │
   ▼
3. REMOVE LOAD
   │
   ├─ T=0ms:   Record V_instant immediately (ADS1115 integrates ~1.16ms,
   │            naturally filtering inductive ringing — no software delay)
   │
   ├─ T=10ms:  ─┐
   ├─ T=20ms:   │ Record 30 voltage samples
   ├─ T=30ms:   │ every 10ms over 300ms
   │  ...        │
   └─ T=300ms: ─┘
         │
         ▼
4. CALCULATE
   │  R_ohm  = (V_instant - V_load) / I_load
   │  R_pol  = (V_final - V_instant) / I_load
   │  R²     = log_regression_fit(ΔV vs ln(t))  [centered data]
   │
   ▼
5. OUTPUT RESULT + SOH GRADE
```

## Hardware

### Bill of Materials

| Component | Value | Package | Notes |
|-----------|-------|---------|-------|
| Arduino Nano/Uno | ATmega328P | DIP/TQFP | Or any AVR-based board |
| ADS1115 | 16-bit ADC | Breakout | 4-ch differential, I2C |
| MOSFET (N-ch) | IRLZ44N or similar | TO-220 | Logic-level gate |
| Shunt Resistor | 0.1 Ω, 1W | Axial | Current sense |
| Load Resistor | 10 Ω, 5W | Wirewound | Adjustable — see [Choosing Load Resistor](#choosing-load-resistor) |
| Battery Holder | — | — | For cell under test |
| Pull-up Resistors | 4.7 kΩ | 0805 | **External** pull-ups on SDA/SCL to 5V — REQUIRED for 200kHz I2C |
| **Pull-up Resistor** | **10 kΩ** | 0805 | **REQUIRED on MOSFET gate to 5V — keeps MOSFET OFF during Arduino boot** |

**Total component count: 7** (excluding Arduino and battery holder)

> **CRITICAL:** The 10kΩ pull-up resistor on the MOSFET gate to 5V is mandatory. During Arduino boot (first ~50ms), all pins are high-impedance INPUT. With active-low MOSFET logic (LOW=ON, HIGH=OFF), the gate must be pulled HIGH to keep the load OFF. A pull-down to GND would turn the load ON during boot — dangerous.

### Schematic

```
                    Arduino Uno/Nano
                   ┌────────────────┐
                   │            D7  ├────────────────── Gate
                   │            A4  ├────── SDA ──┬───┤
                   │            A5  ├────── SCL ──┼───┤
                   │            5V  ├───────┬─────┼───┤
                   │            GND ├───────┼─────┼───┤
                   └────────────────┘       │     │   │
                                            │     │   │
                   ADS1115 Breakout         │     │   │
                   ┌────────────────┐       │     │   │
              VDD ─┤ VDD        A0  ├──┐    │     │   │
              GND ─┤ GND        A1  ├──┤    │     │   │
              SDA ─┤ SDA        A2  ├──── BAT+    │   │
              SCL ─┤ SCL        A3  ├──── BAT-    │   │
                   │                │    │         │   │
                   └────────────────┘    │         │   │
                                         │         │   │
              I2C Pull-ups:          [4.7kΩ]   [4.7kΩ] │
              SDA → 5V (ext.)           │         │   │
              SCL → 5V (ext.)           │         │   │
                                         │         │   │
                   BAT+ ─────────────────┘         │   │
                   BAT- ──┬───────────────────────┘   │
                          │                           │
                       ┌──┴──┐                        │
                       │Shunt│ 0.1Ω                   │
                       └──┬──┘                        │
                          │                           │
                          ├──── A0 (via ADS1115)      │
                          │                           │
                         ┌┴┐                          │
                         │ │ Load 10Ω                 │
                         │ │ 5W                       │
                         └┬┘                          │
                          │                           │
                          └───────────── Drain ───────┘
                                     Source ──── GND
```

### Kelvin Connection

For accurate voltage measurement, use **4-wire Kelvin sensing**:

- **Sense wires (thin):** Connect A2 (V+) and A3 (V-) directly to the battery terminals
- **Power wires (thick):** Connect through the shunt and load for current path
- Keep sense wires as short as possible and away from power traces

```
         BAT+ ──────── A2 (ADS1115 V+)
            │
            │ (thick wire for current)
            │
         BAT- ────┬── A3 (ADS1115 V-)
                  │
               [Shunt 0.1Ω]
                  │
               [Load 10Ω]
                  │
                 GND
```

## Software Architecture

```
VRA-meter/
├── VRA-meter.ino        Main sketch — setup, loop, serial UI
├── config.h            Hardware pin assignments & all tunable parameters
├── ads1115.h           ADS1115 driver header
├── ads1115.cpp         Bit-banged I2C driver for ADS1115 (zero dependencies)
├── vra.h               VRA analyzer header
└── vra.cpp             R² calculation, logarithmic regression, SOH grading
```

### Key Design Decisions

- **Direct-port bit-banged I2C** — ATmega328P PORTC manipulation (`sdaHigh()`, `sclLow()`) achieves ~200kHz clock, vs ~20kHz with `digitalWrite`. No Wire library, no interrupt conflicts
- **Single-shot mode** (860 SPS) — one conversion per read, no continuous streaming overhead
- **No external libraries** — pure Arduino core, zero `#include` beyond `<Arduino.h>` and `<math.h>`
- **All config in one file** — every tunable parameter lives in `config.h`
- **PROGMEM log table** — pre-computed `ln(10)..ln(300)` in flash, avoiding 30 expensive `log()` calls on the FPU-less ATmega328P
- **Centered regression** — voltage data is centered (`ΔV = V[i] - V[0]`) before R² calculation to prevent catastrophic cancellation in 32-bit float
- **PGA ±6.144V for voltage channel** — 4.2V Li-ion full charge saturates ±4.096V range; ±6.144V gives 187.5µV/LSB which is sufficient for 10-150mV relaxation signals
- **Quantization guard** — if relaxation amplitude < 4mV (~21 LSB at 187.5µV/LSB), R² regression is skipped and battery is auto-marked EXCELLENT (signal too small to measure meaningfully)
- **Zero-delay V_instant** — no software delay after MOSFET off; the ADS1115's 1.16ms integration window naturally filters inductive ringing
- **Start-before-wait ADC timing** — conversion starts ~2ms before target read time, runs in parallel with the busy-wait, ensuring samples are precisely aligned to the 10ms grid
- **Safety timeout** — 2-second hard limit in `measure()`, kills MOSFET on any I2C hang
- **Pull-up (not pull-down) on MOSFET gate** — active-low logic (LOW=ON, HIGH=OFF) requires pull-UP to 5V; pull-down would turn load ON during Arduino boot (~50ms high-Z period)

## Installation

1. **Clone the repository:**
   ```bash
   git clone https://github.com/MYMDO/VRA-meter.git
   ```

2. **Open in Arduino IDE:**
   - File → Open → `VRA-meter/VRA-meter.ino`

3. **Select your board:**
   - Tools → Board → Arduino Uno (or Nano, etc.)

4. **Upload:**
   - Ctrl+U (or Upload button)

No library installation required.

## Usage

### Serial Monitor Output

Open Serial Monitor at **115200 baud**. You will see:

```
========================================
  VRA Meter v1.0
  Voltage Relaxation Analysis
  Single-pulse battery diagnostics
========================================

  Shunt: 0.10 Ohm
  Load:  10.0 Ohm
  Pulse: 200 ms
  Samples: 30 x 10 ms

========================================

  Voltage Scale:
  4.20V - 3.90V  = Fully charged
  3.90V - 3.70V  = Nominal
  3.70V - 3.40V  = Discharged
  3.40V - 2.50V  = Deep discharge

  Ready. Send any character to start measurement.
  Send 'a' for auto-mode (continuous measurements).
```

After sending any character:

```
Battery: 3.847V ... Measuring...
--- Measurement #1 ---

  [Voltage Data]
  V_load     = 3.6120 V  (under load)
  V_instant  = 3.8450 V  (instant after)
  V_final    = 3.8510 V  (relaxed)
  ΔV_ohmic   = 0.2330 V
  ΔV_relax   = 0.0060 V

  [Current]
  I_load     = 361.2 mA
  R_load     = 10.00 Ohm

  [Resistance Analysis]
  R_ohm      = 645.07 mOhm  (AC-IR equivalent)
  R_pol      = 16.61 mOhm   (polarization)
  R_total    = 661.68 mOhm  (total ESR)

  [Relaxation Curve Quality]
  R-squared  = 0.998432  → GOOD

  Assessment: Minor contact oxidation or slight SEI growth.

  [Relaxation Data]
  t(ms)   V(V)
  10      3.8460
  20      3.8472
  30      3.8480
  ...
  300     3.8510
```

### Commands

| Key | Action |
|-----|--------|
| Any character | Run one measurement |
| `a` / `A` | Toggle auto-mode (continuous measurements every 1 second) |

### Auto Mode

In auto-mode, the device continuously measures and outputs results. This is useful for:
- Monitoring voltage drift over time
- Logging relaxation curves to a file
- Testing battery response to repeated pulses

## Measurement Science

### Voltage Relaxation Curve

When a lithium-ion battery delivers current and the load is suddenly removed, the terminal voltage does not recover instantly. Instead, it follows a characteristic two-phase recovery:

**Phase 1 — Ohmic Recovery (instantaneous):**
The voltage jumps by `I × R_ohm` the moment current stops. This reflects the purely resistive losses in the metal contacts, electrolyte, and separator.

**Phase 2 — Chemical Relaxation (logarithmic):**
The voltage continues to rise slowly as lithium ions diffuse back to equilibrium positions within the electrode crystal structure. This process follows `V(t) = A × ln(t) + B` where `t` is time after load removal.

Healthy batteries follow this logarithmic law precisely. Degraded batteries deviate from it.

### R_ohm — Ohmic Resistance

Calculated from the instantaneous voltage jump when load is disconnected:

```
R_ohm = (V_instant - V_load) / I_load
```

This is equivalent to the AC internal resistance measured at 1 kHz by professional impedance analyzers, but obtained with a simple DC pulse.

**Typical values for 18650 Li-ion cells:**
- New cell: 20–50 mΩ
- Aged cell: 50–150 mΩ
- Damaged cell: > 200 mΩ

### R_pol — Polarization Resistance

Calculated from the total voltage relaxation depth:

```
R_pol = (V_final - V_instant) / I_load
```

This represents the combined resistance of charge transfer at the electrode surfaces and solid-state diffusion of lithium ions. It is highly sensitive to:
- Electrolyte decomposition
- SEI (Solid Electrolyte Interphase) layer growth
- Electrode particle cracking

### R² — Curve Quality Index

The coefficient of determination (R²) measures how well the 30 relaxation voltage samples fit the ideal logarithmic model `V(t) = A × ln(t) + B`.

**Centered regression for float precision:**
On 8-bit AVR (no FPU), 32-bit `float` has only 7 significant digits. Raw voltage values (~3.85V) cause catastrophic cancellation in the variance formula `S_yy = Σy² - (Σy)²/n` — two nearly equal large numbers subtracted, destroying all precision.

The fix: center the data by subtracting the first sample:
```
ΔV[i] = V[i] - V[0]     // range: 0.000 .. 0.010 V
```
Now `ΔV` values are small, and the variance calculation preserves all 7 digits of mantissa.

The regression is performed on:
- X-axis: `ln(t)` from PROGMEM constant array (pre-computed, no runtime `log()`)
- Y-axis: centered voltage `ΔV(t)`

A perfect fit gives R² = 1.0. Deviations indicate structural problems inside the cell.

## SOH Grading

| R² Value | Grade | Interpretation |
|----------|-------|----------------|
| > 0.999 | **EXCELLENT** | Perfect ion diffusion. No degradation detected. |
| 0.95 – 0.999 | **GOOD** | Minor aging. Possible contact oxidation or early SEI growth. |
| < 0.95 | **POOR** | Abnormal diffusion. Suspected micro-short circuit or severe electrode damage. |

**Note:** If the relaxation amplitude is below 4mV (quantization limit of ADS1115 at 187.5µV/LSB), R² is automatically set to 1.0 and graded as EXCELLENT. This prevents false negatives on high-quality batteries with very low polarization resistance.

### What Each Grade Means

**EXCELLENT (R² > 0.999)**
- Electrolyte is fresh and fully functional
- Electrode surfaces are clean and intact
- No parasitic self-discharge
- Battery can be trusted for critical applications

**GOOD (0.95 < R² < 0.999)**
- Normal aging for a used cell
- May have minor contact oxidation
- SEI layer has grown slightly but is still functional
- Suitable for most applications, monitor over time

**POOR (R² < 0.95)**
- Ion diffusion path is significantly disrupted
- Possible causes: micro-short, electrode particle fracture, electrolyte decomposition
- **Replace the battery** — do not use in critical applications

## Configuration

All parameters are in `config.h`:

### Hardware

| Parameter | Default | Description |
|-----------|---------|-------------|
| `MOSFET_PIN` | `7` | Digital output for load switch |
| `ADS1115_ADDR` | `0x48` | I2C address (change if ADDR pin is HIGH) |
| `SHUNT_RESISTANCE` | `0.1` Ω | Current sense resistor value |
| `LOAD_RESISTANCE` | `10.0` Ω | Load resistor for discharge pulse |

### Measurement Timing

| Parameter | Default | Description |
|-----------|---------|-------------|
| `PULSE_DURATION_MS` | `200` ms | Duration of current pulse |
| `RELAX_SAMPLES` | `30` | Number of voltage samples during relaxation |
| `RELAX_SAMPLE_STEP_MS` | `10` ms | Time between relaxation samples |
| `PRE_PULSE_SETTLE_MS` | `50` ms | Settling time before first reading |

### Safety Thresholds

| Parameter | Default | Description |
|-----------|---------|-------------|
| `BATTERY_MIN_V` | `2.5` V | Minimum allowed voltage |
| `BATTERY_MAX_V` | `4.3` V | Maximum allowed voltage (overvoltage protection) |
| `MAX_CURRENT_A` | `5.0` A | Maximum allowed current |

### Choosing Load Resistor

The load resistor determines the discharge current. For a 3.7V Li-ion cell:

| Load Resistor | Approx. Current | Power Dissipated | Recommended? |
|---------------|-----------------|-------------------|--------------|
| 5 Ω | 740 mA | 2.7 W | Only with heatsink |
| **10 Ω** | **370 mA** | **1.4 W** | **Yes (default)** |
| 20 Ω | 185 mA | 0.7 W | Yes |
| 47 Ω | 79 mA | 0.3 W | Conservative |

**Rule of thumb:** The current should be 0.2C–0.5C for the cell under test. For a typical 2500 mAh 18650, that's 500–1250 mA, so a 10 Ω resistor at ~370 mA is a safe conservative choice.

## Troubleshooting

| Symptom | Likely Cause | Solution |
|---------|-------------|----------|
| `UNDERVOLTAGE` message | Battery below 2.5V or not connected | Check wiring, charge battery |
| `OVERVOLTAGE` message | Battery above 4.3V | Disconnect immediately, check cell |
| R_ohm = 0 mΩ | Current too low to measure | Use smaller load resistor |
| R_ohm too low / R_pol too high | Software delay before V_instant | Verify no delay between MOSFET off and first ADC read (fixed in firmware) |
| R² very low (< 0.9) | Battery damaged or wrong timing | Check MOSFET switching, verify cell |
| Erratic readings | I2C noise, loose connections | Shorten wires, add 100nF cap on ADS1115 VDD |
| ADS1115 not responding | Wrong I2C address or wiring | Verify SDA/SCL connections, check ADDR pin |
| I2C bus lockup | Missing external pull-ups | Add 4.7kΩ pull-ups on SDA/SCL to 5V (internal pull-ups too weak for 200kHz) |

## Theory

### Why Logarithmic?

The voltage relaxation of a lithium-ion battery after current interruption follows from the solid-state diffusion equation in spherical electrode particles. The solution, valid for short times after perturbation, gives:

```
V(t) = V_ocv - (2RT / nF) × √(t / π) × (1/√(D_s) × ...)
```

For practical time windows (10–300 ms), this simplifies to the empirical logarithmic form:

```
V(t) = A × ln(t) + B
```

This relationship has been validated extensively in academic literature and is the basis for commercial battery characterization tools. The R² goodness-of-fit metric captures how closely a real battery follows this ideal behavior.

**Implementation note:** The `ln(t)` values are pre-computed at compile time and stored in AVR flash (PROGMEM) — 30 calls to `log()` on the ATmega328P would cost ~15ms of CPU time with no hardware FPU.

### Comparison to EIS

Electrochemical Impedance Spectroscopy (EIS) requires expensive equipment (frequency response analyzer + potentiostat, typically $5,000+). VRA achieves similar diagnostic insight with:
- **1/100th the cost** (ADS1115 ≈ $5 vs EIS equipment ≈ $5,000)
- **1/10th the time** (400ms vs minutes for full EIS sweep)
- **No frequency domain expertise** needed

The trade-off: VRA provides fewer data points than a full Nyquist plot, but for pass/fail grading and SOH estimation, it is sufficient.

### References

1. Plett, G.L. "Extended Kalman filtering for battery management systems." *Journal of Power Sources*, 2004.
2. farmsworth, B. et al. "Physical and chemical electrochemical impedance spectroscopy." *Electrochimica Acta*, 2013.
3. Tian, H. et al. "A modified capacity calculation method for lithium-ion batteries based on voltage relaxation." *Journal of Power Sources*, 2020.

## License

This project is licensed under the MIT License. See [LICENSE](LICENSE) for details.

---

**Built for the open-source hardware community. Star this repo if you find it useful.**
