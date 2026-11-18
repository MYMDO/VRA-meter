# Measurement audit

## Intended use

VRA Meter characterizes how a single cell responds to a short discharge pulse. A practical use is to compare voltage sag and recovery across cells of the same model under controlled conditions. The immediate product target is a repeatable, low-cost bench instrument with a documented uncertainty. Capacity estimates and suitability decisions require additional evidence.

| Question | Verdict for this instrument |
|---|---|
| How much does terminal voltage sag at the applied test current? | Directly observable with Kelvin sensing and adequate calibration. |
| What is the cell's near-end pulse resistance? | Estimate it as the pre-pulse to late-load voltage difference divided by the corrected late current. Report the measurement window. |
| How much voltage returns after release? | Observable within the captured window. Early and later voltage changes can be expressed relative to load current. |
| Can the step measurement separate the pure ohmic component? | The ADS1115 integrates over a finite interval after switching. The step also includes fast relaxation and any fixture dynamics. |
| Does a high log-fit R² establish health? | R² is unchanged if the same curve shape is offset or scaled, including sign reversal. Different recovery amplitudes can have the same R². |
| Can this pulse measure remaining Ah or certify safe use? | These require independent reference tests and validation. A component defect or failure mechanism may escape this pulse entirely. |

## Concept check

A simple model with an ohmic resistance R₀, one polarization branch R₁ and a time constant τ gives an apparent constant-current pulse resistance of R₀ + R₁ × (1 − exp(−t/τ)). With R₀ = 20 mΩ, R₁ = 30 mΩ and τ = 100 ms, apparent resistance is 22.85 mΩ at 10 ms, 45.73 mΩ at 195 ms and 50.00 mΩ at 1 s. The exact device uses a resistor load, so current can change slightly during the pulse. The formula illustrates why tests must use comparable windows. It also gives a counterexample to treating a pulse reading as a unique capacity measurement. Two models with equal local open-circuit voltage and equal circuit parameters can have different capacities and indistinguishable short-pulse responses when open-circuit voltage is effectively constant over the pulse. At 0.4 A, a 200 ms pulse removes only about 0.022 mAh.

Published measurements show the result depends on the time scale and protocol. Published capacity estimation from relaxation uses controlled histories, capacity labels and model validation. Those studies leave the accuracy of this firmware and its 300 ms post-discharge window an open empirical question. [Barai et al., Scientific Reports (2018)](https://doi.org/10.1038/s41598-017-18424-5) · [Zhu et al., Nature Communications (2022)](https://doi.org/10.1038/s41467-022-29837-w)

## Implementation review

| Area | What v1.3 measures or checks | Remaining limitation |
|---|---|---|
| Voltage path | Separate terminal sense leads and differential readings. Open, loaded and recovery voltages screened against configured bounds. | Requires correct polarity and powered ADC. Wiring errors, contact motion and supply/reference conditions can affect data. |
| Current path | Read the unloaded shunt first. Reject a large offset, subtract the accepted baseline and verify return to baseline after the first recovery voltage. | Correcting the offset leaves gain error, shunt tolerance, heating and wiring errors. Use the measured shunt value where available. |
| Acquisition | Early and late voltage/current guard checks. Log separate late ADC trigger times and the commanded pulse width. | Voltage and current are sequential. Short transients between checks can escape software detection. MCU timing faults, a failed switch, reverse connection and loss of ADC power require independent protection. |
| Recovery time axis | Store the end of each ADC start transaction relative to the software release timestamp. Fit the curve on those times. | These are timestamp proxies. ADC startup, integration and I²C transmission affect the effective sampling time. Verify on real hardware. |
| Resistance estimates | Report Pulse R, Step R and Recovery R separately, with sign preserved. | Relative estimates across a matched protocol are the initial goal. Report calibration and repeatability before quoting accuracy. |
| ADC resolution | With ±6.144 V range, one voltage code is 187.5 µV, equivalent to about 0.47 mΩ at 0.4 A or 3.75 mΩ at 0.05 A. | This is code size expressed in ohms, not instrument accuracy. Quantization of two readings, noise, fixture error, ADC gain and test current uncertainty all contribute. |
| Fit visibility | Center the data and fit voltage versus log of time after acquisition. Publish R² only when the curve span reaches 4 mV. | 4 mV is a firmware policy for suppressing very small-signal fits. Revisit it using measured noise. |
| Serial and export | Explicit commands, CSV for the latest valid attempt and auto-stop on error. | Automatic repeats can change the baseline. Use controlled rests for experimental comparisons. |

[ADS1115 datasheet, electrical characteristics and single-shot mode](https://www.ti.com/lit/ds/symlink/ads1115.pdf)

## Bench acceptance before experimental release

These are initial acceptance targets, not achieved specifications. Record board, firmware commit, supply, shunt, fixture, test current, temperature and oscilloscope or meter settings.

1. With a current-limited supply and no cell, verify shutdown on reset and programming, D7 pulse width at **200 ± 1 ms** with the default configuration, and a low gate between pulses. Compare ADC transaction timing with the serial timestamps. Confirm that the trigger proxy for the first recovery conversion is within 1 ms of software release and the additional shunt read fits before the first scheduled curve trigger (nominally 8 ms). Compare trigger timing against the gate edge on the scope.
2. Measure a stable reference around 3.8–4.0 V and compare both ADC channels with a DMM under load. Place a known low-inductance resistor inside the Kelvin voltage-sense boundary. Use values near **25, 50 and 100 mΩ** where available and record their measured values. Measure at least **10 manual pulses** per reference under fixed conditions and compare the shift in median Pulse R against the added resistance. Initial targets at 0.35–0.42 A are median error within **±5 mΩ** and 10-run spread at most **5 mΩ** for each reference. Establish a zero-reference run and subtract its median for this fixture check.
3. Simulate a current below range and a supply voltage below range under load using the current-limited bench source. Verify the reported fault, gate off, invalid curve and auto-stop. Check disconnected ADC and blocked I²C with power and wiring arranged for a controlled test. Scoping observes the turn-off delay. On a current-limited supply, also emulate a nonzero shunt signal after release and a terminal sense lead losing contact during recovery. Verify that neither attempt produces an exportable curve. A post-release sample checks one instant, and a stuck switch requires physical disconnection. Avoid fault injection with a real cell.
4. Repeat on known cells of one model, with matching temperature, charge state, fixture and rest. Confirm that within-cell spread is smaller than differences worth reporting. Publish conditions and results alongside any accuracy claim.

## If capacity or SOH becomes a product goal

Collect independently measured capacity under each cell model's specified discharge procedure and pair those results with controlled pulse and recovery traces. Include temperature, charge state, rest, cell model and multiple physical cells at different aging states. Establish a naive capacity baseline, keep all traces from one cell in the same data split, and report error and bias on previously unseen physical cells. Measure whether short-pulse features add value over the baseline. Until such evidence exists, retain the pulse-response instrument and raw CSV as the usable output.
