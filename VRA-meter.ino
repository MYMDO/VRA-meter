#include "ads1115.h"
#include "vra.h"

ADS1115 ads;
VRAAnalyzer vra(ads);

unsigned long last_attempt_ms = 0;
bool attempted = false;
bool auto_mode = false;
bool csv_available = false;

void printHelp() {
    Serial.println(F("VRA Meter v" VERSION " | Single-cell pulse and recovery"));
    Serial.print(F("Load: "));
    Serial.print(LOAD_RESISTANCE, 1);
    Serial.print(F(" Ohm | Pulse: "));
    Serial.print(PULSE_DURATION_US / 1000);
    Serial.println(F(" ms"));
    Serial.println(F("m: Measure | a: Toggle auto | c: Export latest curve CSV | ?: Help"));
}

void printReading(const __FlashStringHelper *label, float value,
                  uint8_t digits, const __FlashStringHelper *unit) {
    Serial.print(label);
    Serial.print(value, digits);
    Serial.println(unit);
}

void printResult(const VRAResult &result) {
    printReading(F("V_open:     "), result.open_voltage, 4, F(" V"));
    printReading(F("V_load:     "), result.loaded_voltage, 4, F(" V"));
    printReading(F("V_first:    "), result.first_voltage, 4, F(" V"));
    printReading(F("V_final:    "), result.final_voltage, 4, F(" V"));
    printReading(F("Load current: "), result.current * 1000.0f, 1, F(" mA"));
    printReading(F("Step R:       "), result.step_resistance * 1000.0f, 1, F(" mOhm"));
    printReading(F("Recovery R:   "), result.recovery_resistance * 1000.0f, 1, F(" mOhm"));
    if (result.fit_valid) {
        printReading(F("Log fit R^2:  "), result.r_squared, 4, F(""));
    } else {
        Serial.println(F("Log fit R^2:  N/A (recovery samples span less than the fit threshold)"));
    }
    if (result.step_resistance < 0 || result.recovery_resistance < 0) {
        Serial.println(F("Negative resistance estimate: check contacts and repeat the test."));
    }
}

void printError(const VRAResult &result) {
    switch (result.error) {
        case VRAError::CellVoltage:
            printReading(F("ERROR: Cell outside configured voltage range: "), result.open_voltage, 3, F(" V"));
            break;
        case VRAError::LoadedVoltage:
            printReading(F("ERROR: Voltage outside range under load: "), result.loaded_voltage, 3, F(" V"));
            break;
        case VRAError::CurrentLow:
            printReading(F("ERROR: Load current too low. Check load path and shunt polarity: "), result.current, 3, F(" A"));
            break;
        case VRAError::CurrentHigh:
            printReading(F("ERROR: Load current exceeds configured limit: "), result.current, 3, F(" A"));
            break;
        case VRAError::ADCBus:
            Serial.println(F("ERROR: ADS1115 bus fault. Check supply, SDA/SCL pull-ups and address."));
            break;
        case VRAError::ADCTimeout:
            Serial.println(F("ERROR: ADS1115 conversion timed out."));
            break;
        case VRAError::ADCSaturated:
            Serial.println(F("ERROR: ADC input saturated. Check wiring and input range."));
            break;
        case VRAError::Timing:
            Serial.println(F("ERROR: Sampling missed its timing window."));
            break;
        case VRAError::None:
            Serial.println(F("ERROR: Measurement failed."));
            break;
    }
}

void exportCurve() {
    if (!csv_available) {
        Serial.println(F("No valid curve. Run a successful measurement first."));
        return;
    }
    Serial.println(F("nominal_time_ms,voltage_V"));
    for (uint8_t i = 0; i < RELAX_SAMPLES; ++i) {
        Serial.print((i + 1UL) * RELAX_SAMPLE_STEP_US / 1000);
        Serial.print(',');
        Serial.println(vra.voltageSample(i), 5);
    }
}

bool cooldownComplete() {
    return !attempted || millis() - last_attempt_ms >= COOLDOWN_MS;
}

void runMeasurement() {
    VRAResult result;
    csv_available = vra.measure(result);
    last_attempt_ms = millis();
    attempted = true;
    if (csv_available) {
        printResult(result);
        return;
    }
    printError(result);
    if (auto_mode) {
        auto_mode = false;
        Serial.println(F("Auto mode stopped after measurement error."));
    }
}

void setup() {
    vra.begin();
    Serial.begin(SERIAL_BAUD);
    printHelp();
}

void loop() {
    if (Serial.available()) {
        switch (Serial.read()) {
            case 'm':
            case 'M':
                if (cooldownComplete()) runMeasurement();
                else Serial.println(F("Cooling down. Retry in one second."));
                break;
            case 'a':
            case 'A':
                auto_mode = !auto_mode;
                Serial.println(auto_mode ? F("Auto mode: ON") : F("Auto mode: OFF"));
                break;
            case 'c':
            case 'C':
                exportCurve();
                break;
            case '?':
                printHelp();
                break;
        }
        return;
    }
    if (auto_mode && cooldownComplete()) runMeasurement();
}
