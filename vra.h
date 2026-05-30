#ifndef VRA_H
#define VRA_H

#include <Arduino.h>
#include "config.h"

class ADS1115;  // forward declaration

// Pre-computed ln(t) for t = 10, 20, 30, ... 300 ms
// Stored in PROGMEM to save SRAM and skip runtime log() calls
extern const float LOG_TIME[RELAX_SAMPLES] PROGMEM;

// Results from a single VRA measurement cycle
struct VRA_Result {
    float R_ohm;          // Ohmic resistance (Ω) — from instantaneous voltage jump
    float R_pol;          // Polarization resistance (Ω) — from relaxation depth
    float R_squared;      // R² of logarithmic fit — curve quality
    float V_before;       // Voltage under load (V)
    float V_after;        // Voltage just after load removed (V)
    float V_final;        // Final relaxed voltage (V)
    float I_load;         // Current during pulse (A)
    float V_relaxation;   // Total relaxation voltage depth (V)
    uint8_t soh_grade;    // 0=Poor, 1=Good, 2=Excellent
};

class VRA_Analyzer {
public:
    void begin(ADS1115 &adc);

    // Run a complete VRA measurement cycle
    // Returns false if battery voltage is out of safe range
    bool measure(VRA_Result &result);

    // Get human-readable SOH grade string
    static const char* getGradeString(uint8_t grade);

    // Get human-readable assessment
    static void getAssessment(const VRA_Result &result, char *buf, uint8_t bufsize);

    // Get a relaxation voltage sample by index (for data export)
    float getVoltageSample(uint8_t index) const;

private:
    ADS1115 *adc_;        // reference to ADC (set in begin())
    float voltage_[RELAX_SAMPLES];

    // MOSFET control helpers
    void setLoad(bool on);
    void killLoad();

    // R² on centered data: delta_v[i] = voltage_[i] - voltage_[0]
    // Avoids catastrophic cancellation on 8-bit AVR float
    // x_progmem: if true, read x values via pgm_read_float (PROGMEM)
    float calculateR2Centered(const float *x, const float *y, int n, float &a, float &b, bool x_progmem = false);
};

#endif
