#ifndef CONFIG_H
#define CONFIG_H

// --- Hardware Pin Assignments ---
#define MOSFET_PIN       7        // Digital output: LOW = load ON, HIGH = load OFF
#define ADS1115_ADDR     0x48     // I2C address (ADDR pin to GND)

// --- ADS1115 Configuration ---
// Channel 0 (A0-A1): Differential current measurement across shunt
// Channel 1 (A2-A3): Differential voltage measurement across battery (Kelvin)
#define ADS1115_CH_CURRENT  0     // Config register mux bits for A0-A1
#define ADS1115_CH_VOLTAGE  1     // Config register mux bits for A2-A3

// PGA gain settings (bits [11:9] in config register)
#define PGA_6144V   0x0000
#define PGA_4096V   0x0200
#define PGA_2048V   0x0400  // default
#define PGA_1024V   0x0600
#define PGA_0512V   0x0800
#define PGA_0256V   0x0A00

// For current channel: ±256 mV range (max precision on shunt)
#define CURRENT_PGA  PGA_0256V
// For voltage channel: ±4.096 V range (covers up to 4.2V Li-ion)
#define VOLTAGE_PGA  PGA_4096V

// ADS1115 full-scale voltages for each PGA setting
#define FS_0256V  0.256f
#define FS_4096V  4.096f

// --- Shunt Resistor ---
#define SHUNT_RESISTANCE  0.1f   // Ohms

// --- Load Resistor ---
#define LOAD_RESISTANCE   10.0f  // Ohms (adjust to your load)

// --- Measurement Timing ---
#define PULSE_DURATION_MS    200   // Load pulse duration (ms)
#define RELAX_SAMPLES        30    // Number of voltage samples during relaxation
#define RELAX_SAMPLE_STEP_MS 10    // Interval between relaxation samples (ms)
#define PRE_PULSE_SETTLE_MS  50    // Wait before pulse for ADC settling
#define POST_PULSE_DELAY_MS  5     // Small delay after MOSFET off before first sample

// --- I2C Timing ---
#define I2C_DELAY_US  2

// --- Battery Thresholds ---
#define BATTERY_MIN_V   2.5f   // Minimum voltage to allow test (V)
#define BATTERY_MAX_V   4.3f   // Maximum voltage (overvoltage protection)
#define MAX_CURRENT_A   5.0f   // Maximum allowed current (A)

// --- SOH Thresholds ---
#define SOH_EXCELLENT   0.999f // R² > 0.999 → Excellent
#define SOH_GOOD        0.95f  // R² > 0.95  → Good
// R² < 0.95 → Poor / damaged

#endif
