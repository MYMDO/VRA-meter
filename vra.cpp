#include "vra.h"
#include "ads1115.h"
#include <math.h>

// Pre-computed ln(t) for t = 10, 20, 30, ... 300 ms
// Stored in PROGMEM to save SRAM and skip 30 expensive log() calls on
// ATmega328P (no FPU, each log() ≈ 500µs)
const float LOG_TIME[RELAX_SAMPLES] PROGMEM = {
    2.302585, 2.995732, 3.401197, 3.688879, 3.912023,
    4.094345, 4.248495, 4.382027, 4.499810, 4.605170,
    4.700480, 4.787492, 4.867534, 4.941642, 5.010635,
    5.075174, 5.135798, 5.192957, 5.247024, 5.298317,
    5.347108, 5.393628, 5.438079, 5.480639, 5.521461,
    5.560682, 5.598422, 5.634790, 5.669881, 5.703782
};

// ============================================================================
// Initialization
// ============================================================================

void VRA_Analyzer::begin(ADS1115 &adc) {
    adc_ = &adc;
    reg_a_ = 0;
    reg_b_ = 0;
}

// ============================================================================
// MOSFET Control (active-low: LOW = ON, HIGH = OFF)
// ============================================================================

void VRA_Analyzer::setLoad(bool on) {
    digitalWrite(MOSFET_PIN, on ? LOW : HIGH);
}

void VRA_Analyzer::killLoad() {
    digitalWrite(MOSFET_PIN, HIGH);  // active-low: HIGH = OFF
}

// ============================================================================
// Main Entry Point — orchestrates measurement phases
// ============================================================================

bool VRA_Analyzer::measure(VRA_Result &result) {
    result.error = VRA_ERR_NONE;
    adc_->clearError();

    if (!checkBattery(result))    return false;
    if (!acquireData(result))     return false;

    calculateParams(result);
    gradeResult(result);
    return true;
}

// ============================================================================
// Phase 0: Safety check — battery voltage and I2C bus
// ============================================================================

bool VRA_Analyzer::checkBattery(VRA_Result &result) {
    float v_check = adc_->readDifferential(ADS1115_CH_VOLTAGE, VOLTAGE_PGA, FS_6144V);

    if (adc_->hadI2CError()) {
        result.error = VRA_ERR_I2C_FAULT;
        return false;
    }
    if (v_check < BATTERY_MIN_V || v_check > BATTERY_MAX_V) {
        result.error = VRA_ERR_VOLTAGE_RANGE;
        return false;
    }
    return true;
}

// ============================================================================
// Phase 1: Data acquisition — load pulse + relaxation curve
// ============================================================================

bool VRA_Analyzer::acquireData(VRA_Result &result) {
    unsigned long safety_start = millis();

    // --- Step 1: Apply load, measure steady-state ---
    setLoad(true);
    delay(PRE_PULSE_SETTLE_MS);

    result.V_before = adc_->readDifferential(ADS1115_CH_VOLTAGE, VOLTAGE_PGA, FS_6144V);

    // Read current with saturation detection
    adc_->startConversion(ADS1115_CH_CURRENT, CURRENT_PGA);
    delay(ADC_START_LEAD_MS);
    uint16_t attempts = 0;
    while (!(adc_->readConfigReg() & ADS1115_CFG_OS)) {
        delay(1);
        if (++attempts > 100) break;
    }
    int16_t raw_shunt = adc_->readResultRaw();

    if (raw_shunt >= ADC_SATURATION_THRESHOLD) {
        killLoad();
        result.error = VRA_ERR_ADC_SATURATED;
        return false;
    }
    result.I_load = ((float)raw_shunt / 32767.0f) * FS_0256V / SHUNT_RESISTANCE;

    // --- Step 2: Remove load, capture V_instant ---
    unsigned long t_start = millis();
    setLoad(false);

    // Start conversion immediately — ADS1115 integrates ~1.16ms,
    // naturally filtering inductive ringing from MOSFET switching.
    adc_->startConversion(ADS1115_CH_VOLTAGE, VOLTAGE_PGA);
    delay(V_AFTER_SETTLE_MS);

    // Poll OS-bit to confirm conversion is done (guards against I2C jitter)
    uint16_t os_attempts = 0;
    while (!(adc_->readConfigReg() & ADS1115_CFG_OS) && os_attempts < 10) {
        delay(1);
        os_attempts++;
    }
    result.V_after = adc_->readResult(FS_6144V);

    // --- Step 3: Collect relaxation curve (30 samples × 10ms) ---
    // Start-before-wait pattern: conversion starts ADC_START_LEAD_MS before
    // target time, runs in parallel with busy-wait, read at target.
    for (int i = 0; i < RELAX_SAMPLES; i++) {
        unsigned long target = (unsigned long)((i + 1) * RELAX_SAMPLE_STEP_MS);

        unsigned long start_at = (target > ADC_START_LEAD_MS) ? target - ADC_START_LEAD_MS : 0;
        while ((millis() - t_start) < start_at) {
            if (millis() - safety_start > SAFETY_TIMEOUT_MS) {
                killLoad();
                return false;
            }
        }
        adc_->startConversion(ADS1115_CH_VOLTAGE, VOLTAGE_PGA);

        while ((millis() - t_start) < target) {
            if (millis() - safety_start > SAFETY_TIMEOUT_MS) {
                killLoad();
                return false;
            }
        }

        voltage_[i] = adc_->readResult(FS_6144V);
    }

    result.V_final = voltage_[RELAX_SAMPLES - 1];
    return true;
}

// ============================================================================
// Phase 2: Calculate R_ohm, R_pol
// ============================================================================

void VRA_Analyzer::calculateParams(VRA_Result &result) {
    // R_ohm: ohmic resistance from instantaneous voltage jump
    float delta_v = result.V_after - result.V_before;
    result.R_ohm = (result.I_load > 0.001f) ? (delta_v / result.I_load) : 0.0f;

    // R_pol: polarization resistance from total relaxation depth
    result.V_relaxation = result.V_final - result.V_after;
    result.R_pol = (result.I_load > 0.001f) ? (result.V_relaxation / result.I_load) : 0.0f;
}

// ============================================================================
// Phase 3: R² regression + SOH grading
// ============================================================================

void VRA_Analyzer::gradeResult(VRA_Result &result) {
    // Center data: ΔV[i] = voltage_[i] - voltage_[0]
    // Prevents catastrophic cancellation on 32-bit float (7 significant digits)
    float centered_v[RELAX_SAMPLES];
    for (int i = 0; i < RELAX_SAMPLES; i++) {
        centered_v[i] = voltage_[i] - voltage_[0];
    }

    // QUANTIZATION GUARD: If relaxation amplitude < MIN_RELAX_MV (~21 LSB at
    // 187.5µV/LSB), the signal is below ADC resolution → R² is meaningless.
    // The battery is physically excellent — skip regression.
    float abs_relax = fabs(result.V_relaxation);
    if (abs_relax < (MIN_RELAX_MV / 1000.0f)) {
        result.R_squared = 1.0f;
        result.soh_grade = SOH_EXCELLENT;
        reg_a_ = 0;
        reg_b_ = 0;
        return;
    }

    // Logarithmic regression: ΔV = a × ln(t) + b
    result.R_squared = calculateR2Centered(LOG_TIME, centered_v, RELAX_SAMPLES,
                                           reg_a_, reg_b_, true);

    // SOH grade based on R²
    if (result.R_squared > SOH_EXCELLENT_THRESHOLD) {
        result.soh_grade = SOH_EXCELLENT;
    } else if (result.R_squared > SOH_GOOD_THRESHOLD) {
        result.soh_grade = SOH_GOOD;
    } else {
        result.soh_grade = SOH_POOR;
    }
}

// ============================================================================
// Centered R² Calculation
// ============================================================================
// Works with delta_v[i] = V[i] - V[0] — range ~0.000..0.010 V, fits entirely
// within float precision (7 digits). Raw ~3.85V values would destroy precision.
//
// SS_tot = Σ(y_i - ȳ)² — NOT Σ(y_i²). The mean ȳ is NOT zero for centered
// data (centered from V[0], not from mean). Using Σy² would systematically
// overestimate SS_tot, lowering R² by 0.005–0.025.

float VRA_Analyzer::calculateR2Centered(const float *x, const float *y, int n,
                                         float &a, float &b, bool x_progmem) {
    if (n < 2) return 0.0f;

    float sum_x = 0, sum_y = 0, sum_xy = 0, sum_x2 = 0;

    for (int i = 0; i < n; i++) {
        float xi = x_progmem ? pgm_read_float(&x[i]) : x[i];
        sum_x  += xi;
        sum_y  += y[i];
        sum_xy += xi * y[i];
        sum_x2 += xi * xi;
    }

    float denom = (float)n * sum_x2 - sum_x * sum_x;
    if (fabs(denom) < 1e-12f) {
        a = 0;
        b = 0;
        return 0.0f;
    }

    // Least-squares: a = (n·Σxy − Σx·Σy) / (n·Σx² − (Σx)²)
    a = ((float)n * sum_xy - sum_x * sum_y) / denom;
    b = (sum_y - a * sum_x) / (float)n;

    // R² = 1 − SS_res / SS_tot
    float y_mean = sum_y / (float)n;
    float ss_tot = 0, ss_res = 0;

    for (int i = 0; i < n; i++) {
        float xi = x_progmem ? pgm_read_float(&x[i]) : x[i];
        float y_pred = a * xi + b;
        ss_res += (y[i] - y_pred) * (y[i] - y_pred);
        float y_diff = y[i] - y_mean;
        ss_tot += y_diff * y_diff;
    }

    if (ss_tot < 1e-12f) return 1.0f;

    return 1.0f - (ss_res / ss_tot);
}

// ============================================================================
// Accessors
// ============================================================================

const char* VRA_Analyzer::getGradeString(SOH_Grade grade) {
    switch (grade) {
        case SOH_EXCELLENT: return "EXCELLENT";
        case SOH_GOOD:      return "GOOD";
        default:            return "POOR";
    }
}

float VRA_Analyzer::getVoltageSample(uint8_t index) const {
    if (index < RELAX_SAMPLES) return voltage_[index];
    return 0.0f;
}

void VRA_Analyzer::getRegression(float &a, float &b) const {
    a = reg_a_;
    b = reg_b_;
}

void VRA_Analyzer::getAssessment(const VRA_Result &result, char *buf, uint8_t bufsize) {
    if (result.R_squared > SOH_EXCELLENT_THRESHOLD) {
        snprintf(buf, bufsize, "Battery in perfect condition. No degradation detected.");
    } else if (result.R_squared > SOH_GOOD_THRESHOLD) {
        snprintf(buf, bufsize, "Minor contact oxidation or slight SEI growth.");
    } else if (result.R_squared > 0.75f) {
        snprintf(buf, bufsize, "Moderate aging. Active material degradation.");
    } else {
        snprintf(buf, bufsize, "WARNING: Internal damage suspected (micro-short or electrode failure).");
    }
}
