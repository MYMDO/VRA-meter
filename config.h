#ifndef CONFIG_H
#define CONFIG_H

#define VERSION "1.0"

// --- Hardware Pin Assignments ---
#define MOSFET_PIN       7        // Digital output: LOW = load ON, HIGH = load OFF
#define ADS1115_ADDR     0x48     // I2C address (ADDR pin to GND)

// --- ADS1115 Channel Configuration ---
// PGA settings, full-scale voltages, channel assignments, and saturation
// threshold are all in ads1115.h (driver-level, not user-tunable).
//
/* PLATFORM WARNING:
 * Pull-ups to 5V (SDA/SCL) are ONLY safe for 5V boards (Uno, Nano ATmega328P).
 * For 3.3V boards (Due, Nano 33 BLE/IoT, Zero, RP2040):
 *   → Use pull-ups to 3.3V, NOT 5V!
 */

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
// R² threshold values for SOH grading (used in vra.cpp)
// Named with _THRESH suffix to avoid conflict with enum SOH_Grade members
#define SOH_EXCELLENT_THRESHOLD  0.999f // R² > 0.999 → Excellent
#define SOH_GOOD_THRESHOLD       0.95f  // R² > 0.95  → Good
// R² < 0.95 → Poor / damaged

// --- Safety ---
#define SAFETY_TIMEOUT_MS  2000  // Hard kill-switch for MOSFET (ms)
#define MIN_RELAX_MV       4.0f  // Minimum relaxation amplitude for valid R² (mV)
                                // Below this, signal is below ADS1115 LSB → auto EXCELLENT

// --- I2C Timing ---
#define I2C_NOP_COUNT  4  // NOPs per half-period (~250ns at 16MHz → ~200kHz)

#endif
