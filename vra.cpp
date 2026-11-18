#include "vra.h"
#include "ads1115.h"

// ln(10), ln(20), ... ln(300), with time in nominal milliseconds.
namespace {
const float LOG_TIME[] PROGMEM = {
    2.302585, 2.995732, 3.401197, 3.688879, 3.912023,
    4.094345, 4.248495, 4.382027, 4.499810, 4.605170,
    4.700480, 4.787492, 4.867534, 4.941642, 5.010635,
    5.075174, 5.135798, 5.192957, 5.247024, 5.298317,
    5.347108, 5.393628, 5.438079, 5.480639, 5.521461,
    5.560682, 5.598422, 5.634790, 5.669881, 5.703782
};
static_assert(RELAX_SAMPLES == sizeof(LOG_TIME) / sizeof(LOG_TIME[0]),
              "Update LOG_TIME when changing the sample count.");
static_assert(RELAX_SAMPLE_STEP_US == 10000UL,
              "Update LOG_TIME when changing the sampling interval.");
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
    if (voltageInRange(result.open_voltage)) return true;
    result.error = VRAError::CellVoltage;
    return false;
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
    result.current = shunt_voltage / SHUNT_RESISTANCE;
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
        valid = readLoadCurrent(result) && readLoadedVoltage(result);
    }
    if (valid) waitUntil(started, PULSE_DURATION_US);
    setLoad(false);
    const unsigned long released = micros();

    if (!valid) return false;
    if (released - started > PULSE_DURATION_US + MAX_TIMING_OVERRUN_US) {
        result.error = VRAError::Timing;
        return false;
    }

    if (!checkADC(adc_.read(ADCChannel::Voltage, result.first_voltage), result)) return false;
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
    result.step_resistance = (result.first_voltage - result.loaded_voltage) / result.current;
    result.recovery_resistance = (result.final_voltage - result.first_voltage) / result.current;
}

void VRAAnalyzer::fitRelaxation(VRAResult &result) {
    float min_v = voltage_[0], max_v = voltage_[0];
    float sum_x = 0, sum_y = 0;
    for (uint8_t i = 0; i < RELAX_SAMPLES; ++i) {
        const float voltage = voltage_[i];
        if (voltage < min_v) min_v = voltage;
        if (voltage > max_v) max_v = voltage;
        sum_x += pgm_read_float(&LOG_TIME[i]);
        sum_y += voltage - voltage_[0];
    }
    if (max_v - min_v < MIN_RELAX_MV / 1000.0f) return;

    const float mean_x = sum_x / RELAX_SAMPLES;
    const float mean_y = sum_y / RELAX_SAMPLES;
    float xx = 0, yy = 0, xy = 0;
    for (uint8_t i = 0; i < RELAX_SAMPLES; ++i) {
        const float x = pgm_read_float(&LOG_TIME[i]) - mean_x;
        const float y = voltage_[i] - voltage_[0] - mean_y;
        xx += x * x;
        yy += y * y;
        xy += x * y;
    }
    if (yy <= 0) return;

    const float r_squared = (xy * xy) / (xx * yy);
    result.r_squared = r_squared > 1.0f ? 1.0f : r_squared;
    result.fit_valid = true;
}
