#ifndef ADS1115_H
#define ADS1115_H

#include <Arduino.h>

enum class ADCChannel : uint8_t { Current, Voltage };
enum class ADCStatus : uint8_t { Ok, BusFault, Timeout, Saturated };

class ADS1115 {
public:
    void begin();
    ADCStatus read(ADCChannel channel, float &volts);
    ADCStatus startConversion(ADCChannel channel);
    ADCStatus readResult(float &volts);
    unsigned long conversionStartedUs() const { return started_us_; }

private:
    bool startBus();
    bool stopBus();
    bool writeByte(uint8_t data);
    bool readByte(bool ack, uint8_t &data);
    bool clockHigh();
    bool writeRegister(uint8_t reg, uint16_t value);
    bool readRegister(uint8_t reg, uint16_t &value);

    bool pending_ = false;
    uint16_t config_ = 0;
    unsigned long started_us_ = 0;
};

#endif
