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

std::vector<Reading> responses;
size_t response_index = 0;
std::vector<unsigned long> sample_starts;
unsigned long time_us = 0;
unsigned long turn_on_us = 0, turn_off_us = 0;
unsigned pulses = 0;
bool load_on = false;
bool output_enabled = false;

void scenario(float amplitude = 0.006f) {
    response_index = 0;
    sample_starts.clear();
    responses = {
        {ADCChannel::Voltage, 3.9f, ADCStatus::Ok, 0},  // open
        {ADCChannel::Voltage, 3.7f, ADCStatus::Ok, 0},  // early pulse checks
        {ADCChannel::Current, 0.039f, ADCStatus::Ok, 0},
        {ADCChannel::Current, 0.037f, ADCStatus::Ok, 0}, // near the release
        {ADCChannel::Voltage, 3.7f, ADCStatus::Ok, 0},
        {ADCChannel::Voltage, 3.88f, ADCStatus::Ok, 0}, // first conversion after release
    };
    for (uint8_t i = 0; i < RELAX_SAMPLES; ++i) {
        const float volts = 3.88f + amplitude * std::log(1.0f + i);
        responses.push_back({ADCChannel::Voltage, volts, ADCStatus::Ok, 0});
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
unsigned long micros() { return time_us += 4; }

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
    assert(load_on == (response_index >= 1 && response_index <= 4));
    if (response_index >= 6) sample_starts.push_back(time_us - turn_off_us);
    time_us += 300;
    return ADCStatus::Ok;
}

ADCStatus ADS1115::readResult(float &volts) {
    const Reading reading = responses.at(response_index++);
    time_us += 700 + reading.extra_us;
    if (reading.status == ADCStatus::Ok) volts = reading.voltage;
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
    assert(std::fabs(result.current - 0.37f) < 0.0001f);
    assert(std::fabs(result.step_resistance - (3.88f - 3.7f) / 0.37f) < 0.0001f);
    assert(std::fabs(result.recovery_resistance - (result.final_voltage - 3.88f) / 0.37f) < 0.0001f);
    assert(result.fit_valid && result.r_squared > 0.99999f && result.r_squared <= 1.0f);
    assert(sample_starts.size() == RELAX_SAMPLES);
    for (size_t i = 0; i < sample_starts.size(); ++i) {
        const auto scheduled = (i + 1) * RELAX_SAMPLE_STEP_US - ADC_START_LEAD_US;
        assert(sample_starts[i] >= scheduled && sample_starts[i] <= scheduled + 12);
    }

    scenario(0.0001f);
    assert(meter.measure(result) && !result.fit_valid);
    scenario();
    responses[0].voltage = 4.5f;
    assert(!meter.measure(result) && result.error == VRAError::CellVoltage);
    assert(pulses == 2);
    scenario();
    responses[1].voltage = 2.8f;
    assert(!meter.measure(result) && result.error == VRAError::LoadedVoltage && !load_on);
    assert(turn_off_us - turn_on_us < 10000);
    scenario();
    responses[2].voltage = -0.01f;
    assert(!meter.measure(result) && result.error == VRAError::CurrentLow && !load_on);
    scenario();
    responses[2].voltage = 0.06f;
    assert(!meter.measure(result) && result.error == VRAError::CurrentHigh && !load_on);
    scenario();
    responses[3].status = ADCStatus::Timeout;
    assert(!meter.measure(result) && result.error == VRAError::ADCTimeout && !load_on);
    scenario();
    responses[2].status = ADCStatus::Saturated;
    assert(!meter.measure(result) && result.error == VRAError::ADCSaturated && !load_on);
    scenario();
    responses[20].status = ADCStatus::BusFault;
    assert(!meter.measure(result) && result.error == VRAError::ADCBus && !load_on && !result.fit_valid);

    scenario();
    responses[4].extra_us = FINAL_SAMPLE_LEAD_US;
    assert(!meter.measure(result) && result.error == VRAError::Timing && !load_on);
    scenario();
    responses[6].extra_us = MAX_TIMING_OVERRUN_US;
    assert(!meter.measure(result) && result.error == VRAError::Timing && !load_on);

    setup();
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
    assert(Serial.output.find("nominal_time_ms,voltage_V\n10,") == 0);
    assert(Serial.output.find("\n300,") != std::string::npos);

    advanceCooldown();
    scenario();
    responses[0].status = ADCStatus::BusFault;
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
