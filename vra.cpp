#include "vra.h"
#include "ads1115.h"

#include <math.h>

namespace {
static_assert(RELAX_SAMPLES >= 3 && RELAX_SAMPLES <= 255,
              "Configure 3 to 255 samples for the curve fit and 8-bit sample index.");
static_assert(MAX_ZERO_SHUNT_MV > 0, "Set a positive shunt zero-check limit.");
static_assert(ADC_START_LEAD_US < RELAX_SAMPLE_STEP_US, "ADC lead must fit in one interval.");
static_assert(SHUNT_RESISTANCE > 0 && MIN_CURRENT_A > 0 && MIN_CURRENT_A < MAX_CURRENT_A,
              "Configure a positive shunt and an ordered, positive current range.");
static_assert(BATTERY_MIN_V > 0 && BATTERY_MIN_V < BATTERY_MAX_V,
              "Configure an ordered, positive cell voltage range.");
static_assert(EARLY_SAMPLE_US + FINAL_SAMPLE_LEAD_US < PULSE_DURATION_US,
              "Both load checks must fit inside the pulse.");

bool voltageInRange(float volts) {
    return volts >= BATTERY_MIN_V && volts <= BATTERY_MAX_V;
}

void waitUntil(unsigned long start, unsigned long offset) {
    while (micros() - start < offset) {}
}
}

void VRAAnalyzer::begin() {
    setLoad(false);
    pinMode(MOSFET_PIN, OUTPUT);
    adc_.begin();
}

void VRAAnalyzer::setLoad(bool on) {
    digitalWrite(MOSFET_PIN, on ? HIGH : LOW);
}

bool VRAAnalyzer::measure(VRAResult &result) {
    result = {};
    if (!checkBattery(result) || !acquireData(result)) return false;
    calculateParams(result);
    fitRelaxation(result);
    return true;
}

bool VRAAnalyzer::checkADC(ADCStatus status, VRAResult &result) {
    switch (status) {
        case ADCStatus::Ok: return true;
        case ADCStatus::BusFault: result.error = VRAError::ADCBus; break;
        case ADCStatus::Timeout: result.error = VRAError::ADCTimeout; break;
        case ADCStatus::Saturated: result.error = VRAError::ADCSaturated; break;
    }
    return false;
}

bool VRAAnalyzer::checkBattery(VRAResult &result) {
    if (!checkADC(adc_.read(ADCChannel::Voltage, result.open_voltage), result)) return false;
    if (!voltageInRange(result.open_voltage)) {
        result.error = VRAError::CellVoltage;
        return false;
    }
    if (!checkADC(adc_.read(ADCChannel::Current, result.zero_shunt_voltage), result)) return false;
    if (fabsf(result.zero_shunt_voltage) > MAX_ZERO_SHUNT_MV / 1000.0f) {
        result.error = VRAError::ShuntZero;
        return false;
    }
    return true;
}

bool VRAAnalyzer::readLoadedVoltage(VRAResult &result) {
    if (!checkADC(adc_.read(ADCChannel::Voltage, result.loaded_voltage), result)) return false;
    if (voltageInRange(result.loaded_voltage)) return true;
    result.error = VRAError::LoadedVoltage;
    return false;
}

bool VRAAnalyzer::readLoadCurrent(VRAResult &result) {
    float shunt_voltage;
    if (!checkADC(adc_.read(ADCChannel::Current, shunt_voltage), result)) return false;
    result.current = (shunt_voltage - result.zero_shunt_voltage) / SHUNT_RESISTANCE;
    if (result.current < MIN_CURRENT_A) {
        result.error = VRAError::CurrentLow;
        return false;
    }
    if (result.current > MAX_CURRENT_A) {
        result.error = VRAError::CurrentHigh;
        return false;
    }
    return true;
}

bool VRAAnalyzer::acquireData(VRAResult &result) {
    setLoad(true);
    const unsigned long started = micros();
    waitUntil(started, EARLY_SAMPLE_US);
    bool valid = readLoadedVoltage(result) && readLoadCurrent(result);
    if (valid) {
        waitUntil(started, PULSE_DURATION_US - FINAL_SAMPLE_LEAD_US);
        // Read voltage last to minimize the gap between V_load and release.
        valid = readLoadCurrent(result);
        if (valid) {
            result.current_trigger_us = adc_.conversionStartedUs() - started;
            valid = readLoadedVoltage(result);
            result.loaded_trigger_us = adc_.conversionStartedUs() - started;
        }
    }
    if (valid) waitUntil(started, PULSE_DURATION_US);
    setLoad(false);
    const unsigned long released = micros();

    if (!valid) return false;
    result.pulse_width_us = released - started;
    if (result.pulse_width_us > PULSE_DURATION_US + MAX_TIMING_OVERRUN_US) {
        result.error = VRAError::Timing;
        return false;
    }

    if (!checkADC(adc_.read(ADCChannel::Voltage, result.first_voltage), result)) return false;
    result.first_trigger_us = adc_.conversionStartedUs() - released;
    return collectRelaxation(released, result);
}

bool VRAAnalyzer::collectRelaxation(unsigned long released, VRAResult &result) {
    for (uint8_t i = 0; i < RELAX_SAMPLES; ++i) {
        const unsigned long target = (i + 1UL) * RELAX_SAMPLE_STEP_US;
        const unsigned long start_at = target - ADC_START_LEAD_US;
        waitUntil(released, start_at);
        if (micros() - released > start_at + MAX_TIMING_OVERRUN_US) {
            result.error = VRAError::Timing;
            return false;
        }
        if (!checkADC(adc_.startConversion(ADCChannel::Voltage), result)) return false;
        sample_trigger_us_[i] = adc_.conversionStartedUs() - released;
        waitUntil(released, target);
        if (!checkADC(adc_.readResult(voltage_[i]), result)) return false;
        if (micros() - released > target + MAX_TIMING_OVERRUN_US) {
            result.error = VRAError::Timing;
            return false;
        }
    }
    result.final_voltage = voltage_[RELAX_SAMPLES - 1];
    return true;
}

void VRAAnalyzer::calculateParams(VRAResult &result) {
    result.pulse_resistance = (result.open_voltage - result.loaded_voltage) / result.current;
    result.step_resistance = (result.first_voltage - result.loaded_voltage) / result.current;
    result.recovery_resistance = (result.final_voltage - result.first_voltage) / result.current;
}

void VRAAnalyzer::fitRelaxation(VRAResult &result) {
    float min_v = voltage_[0], max_v = voltage_[0];
    for (uint8_t i = 1; i < RELAX_SAMPLES; ++i) {
        if (voltage_[i] < min_v) min_v = voltage_[i];
        if (voltage_[i] > max_v) max_v = voltage_[i];
    }
    if (max_v - min_v < MIN_RELAX_MV / 1000.0f) return;

    // Center both axes at the first point for precision on AVR. Compute logs
    // after acquisition so fitting does not affect pulse or sample timing.
    float sum_x = 0, sum_y = 0, xx = 0, yy = 0, xy = 0;
    for (uint8_t i = 0; i < RELAX_SAMPLES; ++i) {
        const float x = logf(static_cast<float>(sample_trigger_us_[i]) / sample_trigger_us_[0]);
        const float y = voltage_[i] - voltage_[0];
        sum_x += x;
        sum_y += y;
        xx += x * x;
        yy += y * y;
        xy += x * y;
    }
    xx -= sum_x * sum_x / RELAX_SAMPLES;
    yy -= sum_y * sum_y / RELAX_SAMPLES;
    xy -= sum_x * sum_y / RELAX_SAMPLES;
    if (xx <= 0 || yy <= 0) return;

    const float r_squared = (xy * xy) / (xx * yy);
    result.r_squared = r_squared > 1.0f ? 1.0f : r_squared;
    result.fit_valid = true;
}
