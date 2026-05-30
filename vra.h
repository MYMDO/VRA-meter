#ifndef VRA_H
#define VRA_H

#include <Arduino.h>
#include "config.h"

class ADS1115;  // forward declaration — avoids circular include

// --- Error Codes ---
enum VRA_Error : uint8_t {
    VRA_ERR_NONE           = 0,
    VRA_ERR_VOLTAGE_RANGE  = 1,  // battery voltage out of safe range
    VRA_ERR_ADC_SATURATED  = 2,  // ADC current channel saturated (>2.5A)
    VRA_ERR_I2C_FAULT      = 3,  // I2C bus error (NACK from ADS1115)
};

// --- SOH Grades ---
enum SOH_Grade : uint8_t {
    SOH_POOR       = 0,
    SOH_GOOD       = 1,
    SOH_EXCELLENT  = 2,
};

// --- Measurement Result ---
struct VRA_Result {
    float R_ohm;          // Ohmic resistance (Ω) — from instantaneous voltage jump
    float R_pol;          // Polarization resistance (Ω) — from relaxation depth
    float R_squared;      // R² of logarithmic fit — curve quality
    float V_before;       // Voltage under load (V)
    float V_after;        // Voltage just after load removed (V)
    float V_final;        // Final relaxed voltage (V)
    float I_load;         // Current during pulse (A)
    float V_relaxation;   // Total relaxation voltage depth (V)
    SOH_Grade soh_grade;  // EXCELLENT / GOOD / POOR
    VRA_Error error;      // Error code (VRA_ERR_NONE = success)
};

class VRA_Analyzer {
public:
    void begin(ADS1115 &adc);

    // Run a complete VRA measurement cycle.
    // On success: fills result with R_ohm, R_pol, R², SOH grade.
    // On failure: sets result.error and returns false.
    bool measure(VRA_Result &result);

    // Human-readable SOH grade string
    static const char* getGradeString(SOH_Grade grade);

    // Human-readable assessment text
    static void getAssessment(const VRA_Result &result, char *buf, uint8_t bufsize);

    // Access relaxation voltage sample by index (for data export)
    float getVoltageSample(uint8_t index) const;

    // Get regression coefficients (slope a, intercept b) from last measurement
    void getRegression(float &a, float &b) const;

private:
    ADS1115 *adc_;        // reference to ADC (set in begin())
    float voltage_[RELAX_SAMPLES];
    float reg_a_, reg_b_; // regression coefficients from last measurement

    // MOSFET control helpers (active-low: LOW=ON, HIGH=OFF)
    void setLoad(bool on);
    void killLoad();

    // --- Measurement Phases ---
    bool checkBattery(VRA_Result &result);
    bool acquireData(VRA_Result &result);
    void calculateParams(VRA_Result &result);
    void gradeResult(VRA_Result &result);

    // R² on centered data: delta_v[i] = voltage_[i] - voltage_[0]
    // Avoids catastrophic cancellation on 8-bit AVR float (7-digit precision)
    // x_progmem: if true, read x values via pgm_read_float (PROGMEM)
    float calculateR2Centered(const float *x, const float *y, int n,
                              float &a, float &b, bool x_progmem = false);
};

#endif
