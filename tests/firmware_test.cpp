#include "ads1115.h"
#include "vra.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>

void setup();
void loop();

namespace {
struct Reading {
    ADCChannel channel;
    float voltage;
    ADCStatus status;
    unsigned long extra_us;
};

enum ReadingIndex {
    Open, ZeroShunt, EarlyVoltage, EarlyCurrent, LateCurrent, LateVoltage,
    FirstRecovery, ReleasedShunt, CurveStart
};

std::vector<Reading> responses;
size_t response_index = 0;
std::vector<unsigned long> sample_starts;
constexpr unsigned long MICROS_TICK_US = 4;
unsigned long time_us = 0;
float curve_amplitude = 0;
size_t delayed_start_index;
unsigned long turn_on_us = 0, turn_off_us = 0;
unsigned pulses = 0;
bool load_on = false;
bool output_enabled = false;

void scenario(float amplitude = 0.006f) {
    response_index = 0;
    delayed_start_index = static_cast<size_t>(-1);
    curve_amplitude = amplitude;
    sample_starts.clear();
    responses = {
        {ADCChannel::Voltage, 3.9f, ADCStatus::Ok, 0},  // open
        {ADCChannel::Current, 0.0005f, ADCStatus::Ok, 0}, // shunt zero
        {ADCChannel::Voltage, 3.7f, ADCStatus::Ok, 0},  // early pulse checks
        {ADCChannel::Current, 0.0395f, ADCStatus::Ok, 0},
        {ADCChannel::Current, 0.0375f, ADCStatus::Ok, 0}, // near the release
        {ADCChannel::Voltage, 3.7f, ADCStatus::Ok, 0},
        {ADCChannel::Voltage, 3.88f, ADCStatus::Ok, 0}, // first conversion after release
        {ADCChannel::Current, 0.0005f, ADCStatus::Ok, 0},
    };
    for (uint8_t i = 0; i < RELAX_SAMPLES; ++i) {
        responses.push_back({ADCChannel::Voltage, 0.0f, ADCStatus::Ok, 0});
    }
}

void input(char value) {
    Serial.input.push_back(value);
    loop();
}

void advanceCooldown() { time_us += (COOLDOWN_MS + 1) * 1000; }
}

FakeSerial Serial;

void FakeSerial::print(float value, int digits) {
    char buffer[40];
    std::snprintf(buffer, sizeof buffer, "%.*f", digits, value);
    output += buffer;
}

unsigned long millis() { return time_us / 1000; }
unsigned long micros() { return time_us += MICROS_TICK_US; }

void digitalWrite(uint8_t pin, uint8_t level) {
    assert(pin == MOSFET_PIN);
    const bool on = level == HIGH;
    if (on) {
        assert(output_enabled && !load_on);
        ++pulses;
        turn_on_us = time_us;
    } else if (load_on) {
        turn_off_us = time_us;
    }
    load_on = on;
}

void pinMode(uint8_t pin, uint8_t mode) {
    assert(pin == MOSFET_PIN && mode == OUTPUT && !load_on);
    output_enabled = true;
}

void ADS1115::begin() { assert(output_enabled && !load_on); }

ADCStatus ADS1115::startConversion(ADCChannel channel) {
    assert(response_index < responses.size());
    assert(responses[response_index].channel == channel);
    assert(load_on == (response_index >= EarlyVoltage && response_index <= LateVoltage));
    time_us += 300;
    if (response_index == delayed_start_index) time_us += MAX_TIMING_OVERRUN_US;
    started_us_ = time_us;
    if (response_index >= CurveStart) sample_starts.push_back(time_us - turn_off_us);
    return ADCStatus::Ok;
}

ADCStatus ADS1115::readResult(float &volts) {
    const size_t index = response_index++;
    const Reading reading = responses.at(index);
    time_us += 700 + reading.extra_us;
    if (reading.status == ADCStatus::Ok) {
        volts = index < CurveStart ? reading.voltage : 3.88f + reading.voltage + curve_amplitude *
            std::log(static_cast<float>(started_us_ - turn_off_us) / sample_starts.front());
    }
    return reading.status;
}

ADCStatus ADS1115::read(ADCChannel channel, float &volts) {
    startConversion(channel);
    time_us += 1200;
    return readResult(volts);
}

int main() {
    ADS1115 adc;
    VRAAnalyzer meter(adc);
    VRAResult result;
    meter.begin();

    scenario();
    assert(meter.measure(result));
    assert(response_index == responses.size() && !load_on);
    assert(turn_off_us - turn_on_us >= PULSE_DURATION_US);
    assert(turn_off_us - turn_on_us < PULSE_DURATION_US + 20);
    assert(std::fabs(result.pulse_resistance - (3.9f - 3.7f) / 0.37f) < 0.0001f);
    assert(result.current_trigger_us < result.loaded_trigger_us);
    assert(result.loaded_trigger_us < result.pulse_width_us);
    assert(result.first_trigger_us < 400);
    assert(std::fabs(result.current - 0.37f) < 0.0001f);
    assert(std::fabs(result.step_resistance - (3.88f - 3.7f) / 0.37f) < 0.0001f);
    assert(std::fabs(result.recovery_resistance - (result.final_voltage - 3.88f) / 0.37f) < 0.0001f);
    assert(result.fit_valid && result.r_squared > 0.99999f && result.r_squared <= 1.0f);
    assert(sample_starts.size() == RELAX_SAMPLES);
    for (size_t i = 0; i < sample_starts.size(); ++i) {
        const auto scheduled = (i + 1) * RELAX_SAMPLE_STEP_US - ADC_START_LEAD_US;
        assert(sample_starts[i] >= scheduled + 300 && sample_starts[i] <= scheduled + 312);
        assert(meter.sampleTriggerUs(static_cast<uint8_t>(i)) + MICROS_TICK_US == sample_starts[i]);
    }

    const float rising_fit = result.r_squared;
    scenario(-0.006f);
    assert(meter.measure(result) && result.recovery_resistance < 0);
    assert(result.fit_valid && std::fabs(result.r_squared - rising_fit) < 0.00001f);

    scenario();
    responses[LateVoltage].voltage -= 0.050f * 0.37f;
    assert(meter.measure(result));
    const float reference_pulse_r = (3.9f - 3.7f) / 0.37f;
    assert(std::fabs(result.pulse_resistance - reference_pulse_r - 0.050f) < 0.0001f);

    scenario(0.0001f);
    assert(meter.measure(result) && !result.fit_valid);
    scenario();
    responses[Open].voltage = 4.5f;
    assert(!meter.measure(result) && result.error == VRAError::CellVoltage);
    assert(pulses == 4);
    scenario();
    responses[ZeroShunt].voltage = 0.0015f;
    assert(!meter.measure(result) && result.error == VRAError::ShuntZero && pulses == 4);
    scenario();
    responses[EarlyVoltage].voltage = 2.8f;
    assert(!meter.measure(result) && result.error == VRAError::LoadedVoltage && !load_on);
    assert(turn_off_us - turn_on_us < 10000);
    scenario();
    responses[EarlyCurrent].voltage = -0.01f;
    assert(!meter.measure(result) && result.error == VRAError::CurrentLow && !load_on);
    scenario();
    responses[EarlyCurrent].voltage = 0.06f;
    assert(!meter.measure(result) && result.error == VRAError::CurrentHigh && !load_on);
    scenario();
    responses[LateCurrent].status = ADCStatus::Timeout;
    assert(!meter.measure(result) && result.error == VRAError::ADCTimeout && !load_on);
    scenario();
    responses[EarlyCurrent].status = ADCStatus::Saturated;
    assert(!meter.measure(result) && result.error == VRAError::ADCSaturated && !load_on);
    scenario();
    responses[CurveStart + 14].status = ADCStatus::BusFault;
    assert(!meter.measure(result) && result.error == VRAError::ADCBus && !load_on && !result.fit_valid);

    scenario();
    responses[LateVoltage].extra_us = FINAL_SAMPLE_LEAD_US;
    assert(!meter.measure(result) && result.error == VRAError::Timing && !load_on);
    scenario();
    responses[CurveStart].extra_us = MAX_TIMING_OVERRUN_US;
    assert(!meter.measure(result) && result.error == VRAError::Timing && !load_on);

    scenario();
    responses[ReleasedShunt].voltage = 0.038f;
    assert(!meter.measure(result) && result.error == VRAError::ResidualCurrent);
    assert(response_index == CurveStart && !load_on && sample_starts.empty());
    scenario();
    responses[ReleasedShunt].status = ADCStatus::BusFault;
    assert(!meter.measure(result) && result.error == VRAError::ADCBus && !load_on);
    scenario();
    responses[ReleasedShunt].extra_us = RELAX_SAMPLE_STEP_US;
    assert(!meter.measure(result) && result.error == VRAError::Timing && sample_starts.empty());
    scenario();
    responses[FirstRecovery].voltage = 0;
    assert(!meter.measure(result) && result.error == VRAError::RecoveryVoltage);
    scenario();
    responses.back().voltage = 1.0f;
    assert(!meter.measure(result) && result.error == VRAError::RecoveryVoltage);
    assert(result.recovery_fault_voltage > BATTERY_MAX_V && !result.fit_valid);
    scenario();
    responses[LateCurrent].extra_us = FINAL_SAMPLE_LEAD_US;
    assert(!meter.measure(result) && result.error == VRAError::Timing && !load_on);
    assert(response_index == LateVoltage);

    for (auto index : {FirstRecovery, CurveStart}) {
        scenario();
        delayed_start_index = index;
        assert(!meter.measure(result) && result.error == VRAError::Timing && !load_on);
    }

    setup();
    input('x');
    assert(Serial.output.find("Unknown command") != std::string::npos);
    const unsigned before = pulses;
    scenario();
    input('m');  // Single byte, without CR/LF.
    assert(pulses == before + 1);
    input('\r');
    input('\n');
    input('m');
    assert(pulses == before + 1);
    Serial.output.clear();
    input('c');
    assert(Serial.output.find("nominal_time_ms,voltage_V,trigger_us_after_release\n10,") == 0);
    assert(Serial.output.find("\n300,") != std::string::npos);

    advanceCooldown();
    scenario();
    responses[Open].status = ADCStatus::BusFault;
    input('a');
    loop();
    assert(Serial.output.find("Auto mode stopped after measurement error.") != std::string::npos);
    Serial.output.clear();
    input('c');
    assert(Serial.output.find("No valid curve") != std::string::npos);
    advanceCooldown();
    loop();
    assert(pulses == before + 1);
    std::puts("Firmware tests passed.");
}
