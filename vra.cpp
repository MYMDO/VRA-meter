#include "vra.h"
#include "ads1115.h"
#include "config.h"
#include <math.h>

extern ADS1115 adc;

// Pre-computed ln(t) for t = 10, 20, 30, ... 300 ms
// Avoids expensive runtime log() on ATmega328P (no FPU)
const float LOG_TIME[RELAX_SAMPLES] PROGMEM = {
    2.302585, 2.995732, 3.401197, 3.688879, 3.912023,
    4.094345, 4.248495, 4.382027, 4.499810, 4.605170,
    4.700480, 4.787492, 4.867534, 4.941642, 5.010635,
    5.075174, 5.135798, 5.192957, 5.247024, 5.298317,
    5.347108, 5.393628, 5.438079, 5.480639, 5.521461,
    5.560682, 5.598422, 5.634790, 5.669881, 5.703782
};

void VRA_Analyzer::begin() {
    // No runtime init needed — LOG_TIME is compile-time constant
}

// R² on centered data: works with delta_v[i] = V[i] - V[0]
// Range ~0.000..0.010 V — fits entirely within float precision (7 digits)
// Original formula S_yy = Σy² - (Σy)²/n would lose all precision on raw ~3.85V values
float VRA_Analyzer::calculateR2Centered(const float *x, const float *y, int n, float &a, float &b) {
    if (n < 2) return 0.0f;
    
    float sum_x = 0, sum_y = 0, sum_xy = 0, sum_x2 = 0;
    
    for (int i = 0; i < n; i++) {
        sum_x  += x[i];
        sum_y  += y[i];
        sum_xy += x[i] * y[i];
        sum_x2 += x[i] * x[i];
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
    // For centered data, SS_tot ≈ Σ(y_i)² directly (mean ≈ 0)
    float ss_tot = 0, ss_res = 0;
    
    for (int i = 0; i < n; i++) {
        float y_pred = a * x[i] + b;
        ss_res += (y[i] - y_pred) * (y[i] - y_pred);
        ss_tot += y[i] * y[i];
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
    
    // Safety timeout — if anything hangs, kill MOSFET within 2 seconds
    // Normal measurement takes ~350ms (50ms settle + 3ms V_after + 300ms relaxation)
    unsigned long safety_start = millis();
    #define SAFETY_TIMEOUT_MS 2000
    
    // --- Phase 1: Measure steady-state voltage under load ---
    digitalWrite(MOSFET_PIN, LOW);  // MOSFET ON → load connected
    delay(PRE_PULSE_SETTLE_MS);     // Let voltage settle under load
    
    result.V_before = adc.readVoltage();
    result.I_load   = adc.readCurrent();
    
    // --- Phase 2: Turn off load, capture relaxation curve ---
    unsigned long t_start = millis();
    digitalWrite(MOSFET_PIN, HIGH); // MOSFET OFF → load disconnected
    
    // Start first conversion immediately — ADS1115 integrates ~1.16ms,
    // naturally filtering inductive ringing from MOSFET switching.
    adc.startConversion(ADS1115_CH_VOLTAGE, VOLTAGE_PGA);
    delay(3); // Wait for conversion to complete
    result.V_after = adc.readResult(FS_6144V);
    
    // Collect relaxation samples with precise timing
    // Strategy: start conversion ~2ms before target, read at target time
    // This ensures the ADS1115 integration window is centered on the target
    for (int i = 0; i < RELAX_SAMPLES; i++) {
        unsigned long target = (unsigned long)((i + 1) * RELAX_SAMPLE_STEP_MS);
        
        // Start conversion early — I2C write takes ~1ms, integration takes ~1.16ms
        // Total: ~2.2ms, so start at target - 2ms
        unsigned long start_at = (target > 2) ? target - 2 : 0;
        while ((millis() - t_start) < start_at) {
            if (millis() - safety_start > SAFETY_TIMEOUT_MS) {
                digitalWrite(MOSFET_PIN, HIGH); // FAILSAFE: kill load
                return false;
            }
        }
        adc.startConversion(ADS1115_CH_VOLTAGE, VOLTAGE_PGA);
        
        // Wait until target time for read
        while ((millis() - t_start) < target) {
            if (millis() - safety_start > SAFETY_TIMEOUT_MS) {
                digitalWrite(MOSFET_PIN, HIGH); // FAILSAFE: kill load
                return false;
            }
        }
        
        // Read the completed result (fast I2C read, ~1ms)
        voltage_[i] = adc.readResult(FS_6144V);
    }
    
    // Final relaxed voltage (last sample)
    result.V_final = voltage_[RELAX_SAMPLES - 1];
    
    // --- Phase 3: Calculate parameters ---
    
    // R_ohm: ohmic resistance from instantaneous voltage jump
    float delta_v = result.V_after - result.V_before;
    if (result.I_load > 0.001f) {
        result.R_ohm = delta_v / result.I_load;
    } else {
        result.R_ohm = 0.0f;
    }
    
    // R_pol: polarization resistance from total relaxation depth
    result.V_relaxation = result.V_final - result.V_after;
    if (result.I_load > 0.001f) {
        result.R_pol = result.V_relaxation / result.I_load;
    } else {
        result.R_pol = 0.0f;
    }
    
    // R²: logarithmic fit of CENTERED relaxation curve
    // Center data: delta_v[i] = voltage_[i] - voltage_[0]
    // This prevents catastrophic cancellation in float arithmetic
    float centered_v[RELAX_SAMPLES];
    for (int i = 0; i < RELAX_SAMPLES; i++) {
        centered_v[i] = voltage_[i] - voltage_[0];
    }
    
    // QUANTIZATION GUARD: If relaxation amplitude is too small,
    // the signal is below ADC resolution → R² is meaningless.
    // At PGA_6144V, 1 LSB = 187.5µV. If ΔV_pol < 4mV (~21 LSB),
    // the battery is physically excellent — skip regression.
    #define MIN_RELAX_MV  4.0f  // minimum relaxation amplitude for valid R²
    float abs_relax = fabs(result.V_relaxation);
    if (abs_relax < (MIN_RELAX_MV / 1000.0f)) {
        result.R_squared = 1.0f;  // too small to measure → perfect
        result.soh_grade = 2;     // EXCELLENT
    } else {
        float a_coeff, b_coeff;
        result.R_squared = calculateR2Centered(LOG_TIME, centered_v, RELAX_SAMPLES, a_coeff, b_coeff);
        
        // SOH grade based on R²
        if (result.R_squared > SOH_EXCELLENT) {
            result.soh_grade = 2; // Excellent
        } else if (result.R_squared > SOH_GOOD) {
            result.soh_grade = 1; // Good
        } else {
            result.soh_grade = 0; // Poor
        }
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
