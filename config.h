#ifndef CONFIG_H
#define CONFIG_H

#define VERSION "1.0"

// --- Hardware Pin Assignments ---
#define MOSFET_PIN       7        // Digital output: LOW = load ON, HIGH = load OFF
#define ADS1115_ADDR     0x48     // I2C address (ADDR pin to GND)

// --- ADS1115 Configuration ---
// PGA gain settings (bits [11:9] in config register)
#define PGA_6144V   0x0000
#define PGA_4096V   0x0200
#define PGA_2048V   0x0400  // default
#define PGA_1024V   0x0600
#define PGA_0512V   0x0800
#define PGA_0256V   0x0A00

// For current channel: ±256 mV range (max precision on shunt)
#define CURRENT_PGA  PGA_0256V
// For voltage channel: ±6.144 V range (covers up to 4.25V Li-ion full charge)
// PGA_4096V would saturate at 4.096V — unsafe for fully charged 18650
#define VOLTAGE_PGA  PGA_6144V

// ADS1115 full-scale voltages for each PGA setting
#define FS_0256V  0.256f
#define FS_6144V  6.144f

// --- Shunt Resistor ---
#define SHUNT_RESISTANCE  0.1f   // Ohms

// --- Load Resistor ---
#define LOAD_RESISTANCE   10.0f  // Ohms (adjust to your load)

// --- Measurement Timing ---
#define PULSE_DURATION_MS    200   // Load pulse duration (ms)
#define RELAX_SAMPLES        30    // Number of voltage samples during relaxation
#define RELAX_SAMPLE_STEP_MS 10    // Interval between relaxation samples (ms)
#define PRE_PULSE_SETTLE_MS  50    // Wait before pulse for ADC settling
#define V_AFTER_SETTLE_MS    3     // Wait for first conversion after MOSFET off (ms)
#define ADC_START_LEAD_MS    2     // Start conversion this many ms before target (ms)

// --- Battery Thresholds ---
#define BATTERY_MIN_V   2.5f   // Minimum voltage to allow test (V)
#define BATTERY_MAX_V   4.3f   // Maximum voltage (overvoltage protection)
#define MAX_CURRENT_A   2.5f   // Maximum allowed current (A) — limited by shunt (0.1Ω) and PGA ±256mV

// --- SOH Thresholds ---
#define SOH_EXCELLENT   0.999f // R² > 0.999 → Excellent
#define SOH_GOOD        0.95f  // R² > 0.95  → Good
// R² < 0.95 → Poor / damaged

// --- Safety ---
#define SAFETY_TIMEOUT_MS  2000  // Hard kill-switch for MOSFET (ms)
#define MIN_RELAX_MV       4.0f  // Minimum relaxation amplitude for valid R² (mV)
                                // Below this, signal is below ADS1115 LSB → auto EXCELLENT

// --- I2C Timing ---
#define I2C_NOP_COUNT  4  // NOPs per half-period (~250ns at 16MHz → ~200kHz)

#endif
