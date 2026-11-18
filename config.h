#ifndef CONFIG_H
#define CONFIG_H

#define VERSION "1.3"

// 5 V, 16 MHz ATmega328P. D7 HIGH turns on the documented low-side N-MOSFET.
#define MOSFET_PIN              7
#define ADS1115_ADDR            0x48
#define SERIAL_BAUD             115200
#define SHUNT_RESISTANCE        0.1f    // Actual shunt value, in ohms
#define LOAD_RESISTANCE         10.0f   // Nominal load, in ohms

// The cell manufacturer's limits take precedence. Tune only for verified hardware.
#define BATTERY_MIN_V           3.0f
#define BATTERY_MAX_V           4.25f
#define MIN_CURRENT_A           0.05f
#define MAX_CURRENT_A           0.50f
#define MAX_ZERO_SHUNT_MV       1.0f   // Abort if the unloaded shunt drop exceeds this magnitude
#define COOLDOWN_MS             1000UL

#define PULSE_DURATION_US       200000UL
#define EARLY_SAMPLE_US           5000UL
#define FINAL_SAMPLE_LEAD_US      7000UL
#define MAX_TIMING_OVERRUN_US     1000UL
#define RELAX_SAMPLES               30
#define RELAX_SAMPLE_STEP_US     10000UL
#define ADC_START_LEAD_US         2000UL
#define MIN_RELAX_MV                4.0f

#endif
