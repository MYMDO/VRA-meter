#ifndef VRA_H
#define VRA_H

#include <Arduino.h>
#include "config.h"

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
    void begin();
    
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
    float log_time_[RELAX_SAMPLES];
    float voltage_[RELAX_SAMPLES];
    bool  time_initialized_;
    
    void initTimeArray();
    float calculateR2(const float *x, const float *y, int n, float &a, float &b);
};

#endif
