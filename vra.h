#ifndef VRA_H
#define VRA_H

#include <Arduino.h>
#include "config.h"

class ADS1115;
enum class ADCStatus : uint8_t;

enum class VRAError : uint8_t {
    None, CellVoltage, LoadedVoltage, CurrentLow, CurrentHigh,
    ADCBus, ADCTimeout, ADCSaturated, Timing
};

struct VRAResult {
    float open_voltage;
    float loaded_voltage;
    float first_voltage;
    float final_voltage;
    float current;
    float step_resistance;
    float recovery_resistance;
    float r_squared;
    bool fit_valid;
    VRAError error;
};

class VRAAnalyzer {
public:
    explicit VRAAnalyzer(ADS1115 &adc) : adc_(adc) {}
    void begin();
    bool measure(VRAResult &result);
    float voltageSample(uint8_t index) const { return voltage_[index]; }

private:
    ADS1115 &adc_;
    float voltage_[RELAX_SAMPLES];

    void setLoad(bool on);
    bool checkBattery(VRAResult &result);
    bool acquireData(VRAResult &result);
    bool readLoadedVoltage(VRAResult &result);
    bool readLoadCurrent(VRAResult &result);
    bool collectRelaxation(unsigned long released, VRAResult &result);
    bool checkADC(ADCStatus status, VRAResult &result);
    void calculateParams(VRAResult &result);
    void fitRelaxation(VRAResult &result);
};

#endif
