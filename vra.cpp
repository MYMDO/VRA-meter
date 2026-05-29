#include "vra.h"
#include "ads1115.h"
#include "config.h"
#include <math.h>

extern ADS1115 adc;

void VRA_Analyzer::begin() {
    time_initialized_ = false;
    initTimeArray();
}

void VRA_Analyzer::initTimeArray() {
    for (int i = 0; i < RELAX_SAMPLES; i++) {
        float t_ms = (float)((i + 1) * RELAX_SAMPLE_STEP_MS);
        log_time_[i] = log(t_ms);
    }
    time_initialized_ = true;
}

// Linear regression on (x, y) data: y = a*x + b
// Returns R² and the coefficients a, b
float VRA_Analyzer::calculateR2(const float *x, const float *y, int n, float &a, float &b) {
    if (n < 2) return 0.0f;
    
    float sum_x = 0, sum_y = 0, sum_xy = 0, sum_x2 = 0, sum_y2 = 0;
    
    for (int i = 0; i < n; i++) {
        sum_x  += x[i];
        sum_y  += y[i];
        sum_xy += x[i] * y[i];
        sum_x2 += x[i] * x[i];
        sum_y2 += y[i] * y[i];
    }
    
    float denom = (float)n * sum_x2 - sum_x * sum_x;
    if (fabs(denom) < 1e-12f) {
        a = 0;
        b = 0;
        return 0.0f;
    }
    
    a = ((float)n * sum_xy - sum_x * sum_y) / denom;
    b = (sum_y - a * sum_x) / (float)n;
    
    // R² = 1 - SS_res / SS_tot
    float y_mean = sum_y / (float)n;
    float ss_tot = 0, ss_res = 0;
    
    for (int i = 0; i < n; i++) {
        float y_pred = a * x[i] + b;
        ss_res += (y[i] - y_pred) * (y[i] - y_pred);
        ss_tot += (y[i] - y_mean) * (y[i] - y_mean);
    }
    
    if (ss_tot < 1e-12f) return 1.0f;
    
    return 1.0f - (ss_res / ss_tot);
}

bool VRA_Analyzer::measure(VRA_Result &result) {
    // Safety: check battery voltage first
    float v_check = adc.readVoltage();
    if (v_check < BATTERY_MIN_V || v_check > BATTERY_MAX_V) {
        return false;
    }
    
    // --- Phase 1: Measure steady-state voltage under load ---
    digitalWrite(MOSFET_PIN, LOW);  // MOSFET ON → load connected
    delay(PRE_PULSE_SETTLE_MS);     // Let voltage settle under load
    
    result.V_before = adc.readVoltage();
    result.I_load   = adc.readCurrent();
    
    // --- Phase 2: Turn off load, capture relaxation curve ---
    unsigned long t_start = millis();
    digitalWrite(MOSFET_PIN, HIGH); // MOSFET OFF → load disconnected
    
    // Small delay for the first "instantaneous" reading
    delay(POST_PULSE_DELAY_MS);
    
    // First reading: the "instant after" voltage (for R_ohm calculation)
    result.V_after = adc.readVoltage();
    
    // Collect relaxation samples
    unsigned long elapsed;
    for (int i = 0; i < RELAX_SAMPLES; i++) {
        unsigned long target = (unsigned long)((i + 1) * RELAX_SAMPLE_STEP_MS) + POST_PULSE_DELAY_MS;
        while ((elapsed = millis() - t_start) < target) {
            // busy-wait for precise timing
        }
        voltage_[i] = adc.readVoltage();
    }
    
    // Final relaxed voltage (last sample)
    result.V_final = voltage_[RELAX_SAMPLES - 1];
    
    // --- Phase 3: Calculate parameters ---
    
    // R_ohm: ohmic resistance from instantaneous voltage jump
    // When load is removed, voltage jumps by I * R_ohm
    // V_before = V_emf - I * R_ohm  (under load)
    // V_after  = V_emf - I * R_ohm + I * R_ohm = V_emf (instant after)
    // Actually: V_before is under load, V_after is just after disconnect
    // The jump is: ΔV = V_after - V_before = I * R_ohm
    float delta_v = result.V_after - result.V_before;
    if (result.I_load > 0.001f) {
        result.R_ohm = delta_v / result.I_load;
    } else {
        result.R_ohm = 0.0f;
    }
    
    // R_pol: polarization resistance from total relaxation depth
    // Total relaxation = V_final - V_after
    result.V_relaxation = result.V_final - result.V_after;
    if (result.I_load > 0.001f) {
        result.R_pol = result.V_relaxation / result.I_load;
    } else {
        result.R_pol = 0.0f;
    }
    
    // R²: logarithmic fit of relaxation curve
    float a_coeff, b_coeff;
    result.R_squared = calculateR2(log_time_, voltage_, RELAX_SAMPLES, a_coeff, b_coeff);
    
    // SOH grade based on R²
    if (result.R_squared > SOH_EXCELLENT) {
        result.soh_grade = 2; // Excellent
    } else if (result.R_squared > SOH_GOOD) {
        result.soh_grade = 1; // Good
    } else {
        result.soh_grade = 0; // Poor
    }
    
    return true;
}

const char* VRA_Analyzer::getGradeString(uint8_t grade) {
    switch (grade) {
        case 2: return "EXCELLENT";
        case 1: return "GOOD";
        default: return "POOR";
    }
}

float VRA_Analyzer::getVoltageSample(uint8_t index) const {
    if (index < RELAX_SAMPLES) return voltage_[index];
    return 0.0f;
}

void VRA_Analyzer::getAssessment(const VRA_Result &result, char *buf, uint8_t bufsize) {
    if (result.R_squared > 0.999f) {
        snprintf(buf, bufsize, "Battery in perfect condition. No degradation detected.");
    } else if (result.R_squared > 0.99f) {
        snprintf(buf, bufsize, "Minor contact oxidation or slight SEI growth.");
    } else if (result.R_squared > 0.95f) {
        snprintf(buf, bufsize, "Moderate aging. Active material degradation.");
    } else {
        snprintf(buf, bufsize, "WARNING: Internal damage suspected (micro-short or electrode failure).");
    }
}
