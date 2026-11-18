#include "ads1115.h"
#include "config.h"

#if !defined(__AVR_ATmega328P__) || F_CPU != 16000000UL
#error "VRA Meter requires a 16 MHz ATmega328P (Uno or classic Nano)."
#endif

#include <util/delay.h>

namespace {
constexpr uint8_t SDA_MASK = _BV(PC4);
constexpr uint8_t SCL_MASK = _BV(PC5);
constexpr uint8_t REG_CONVERSION = 0x00;
constexpr uint8_t REG_CONFIG = 0x01;
constexpr uint16_t READY = 0x8000;
constexpr uint16_t SINGLE_SHOT_860 = 0x01C3;
constexpr uint16_t CURRENT_CONFIG = SINGLE_SHOT_860 | 0x0A00; // A0-A1, +/-0.256 V
constexpr uint16_t VOLTAGE_CONFIG = SINGLE_SHOT_860 | 0x3000; // A2-A3, +/-6.144 V
constexpr int16_t SATURATION = 32700;
constexpr unsigned long ADC_TIMEOUT_US = 4000UL;

void sdaRelease() { DDRC &= ~SDA_MASK; }
void sdaLow() { DDRC |= SDA_MASK; }
void sclRelease() { DDRC &= ~SCL_MASK; }
void sclLow() { DDRC |= SCL_MASK; }
bool sdaIsHigh() { return PINC & SDA_MASK; }
}

void ADS1115::begin() {
    pending_ = false;
    sdaRelease();
    sclRelease();
    PORTC &= ~(SDA_MASK | SCL_MASK); // Open drain with external pull-ups.
}

bool ADS1115::clockHigh() {
    sclRelease();
    _delay_us(3);
    return PINC & SCL_MASK; // ADS1115 does not require clock stretching.
}

bool ADS1115::startBus() {
    sdaRelease();
    _delay_us(3);
    if (!clockHigh() || !sdaIsHigh()) return false;
    sdaLow();
    _delay_us(3);
    sclLow();
    return true;
}

bool ADS1115::stopBus() {
    sclLow();
    sdaLow();
    _delay_us(3);
    const bool clockReleased = clockHigh();
    sdaRelease();
    _delay_us(3);
    return clockReleased && sdaIsHigh();
}

bool ADS1115::writeByte(uint8_t data) {
    for (uint8_t mask = 0x80; mask; mask >>= 1) {
        if (data & mask) sdaRelease();
        else sdaLow();
        _delay_us(3);
        if (!clockHigh()) return false;
        sclLow();
    }

    sdaRelease();
    _delay_us(3);
    if (!clockHigh()) return false;
    const bool acknowledged = !sdaIsHigh();
    sclLow();
    return acknowledged;
}

bool ADS1115::readByte(bool ack, uint8_t &data) {
    data = 0;
    sdaRelease();
    for (uint8_t bit = 0; bit < 8; ++bit) {
        _delay_us(3);
        if (!clockHigh()) return false;
        data = (data << 1) | sdaIsHigh();
        sclLow();
    }

    if (ack) sdaLow();
    _delay_us(3);
    const bool clockReleased = clockHigh();
    sclLow();
    sdaRelease();
    return clockReleased;
}

bool ADS1115::writeRegister(uint8_t reg, uint16_t value) {
    const bool written = startBus() && writeByte(ADS1115_ADDR << 1)
        && writeByte(reg) && writeByte(value >> 8) && writeByte(value & 0xFF);
    return stopBus() && written;
}

bool ADS1115::readRegister(uint8_t reg, uint16_t &value) {
    uint8_t high, low;
    const bool received = startBus() && writeByte(ADS1115_ADDR << 1)
        && writeByte(reg) && startBus() && writeByte((ADS1115_ADDR << 1) | 1)
        && readByte(true, high) && readByte(false, low);
    if (!stopBus() || !received) return false;
    value = (static_cast<uint16_t>(high) << 8) | low;
    return true;
}

ADCStatus ADS1115::startConversion(ADCChannel channel) {
    pending_ = false;
    config_ = channel == ADCChannel::Current ? CURRENT_CONFIG : VOLTAGE_CONFIG;
    if (!writeRegister(REG_CONFIG, READY | config_)) return ADCStatus::BusFault;
    started_us_ = micros();
    pending_ = true;
    return ADCStatus::Ok;
}

ADCStatus ADS1115::readResult(float &volts) {
    if (!pending_) return ADCStatus::BusFault;
    pending_ = false;
    // Allow single-shot power-up before polling the OS bit.
    while (micros() - started_us_ < 100UL) {}
    uint16_t config;
    for (;;) {
        if (!readRegister(REG_CONFIG, config) || (config & ~READY) != config_) {
            return ADCStatus::BusFault;
        }
        if (config & READY) break;
        if (micros() - started_us_ >= ADC_TIMEOUT_US) return ADCStatus::Timeout;
    }

    uint16_t value;
    if (!readRegister(REG_CONVERSION, value)) return ADCStatus::BusFault;
    const int16_t raw = static_cast<int16_t>(value);
    if (raw >= SATURATION || raw <= -SATURATION) return ADCStatus::Saturated;
    const float fullScale = config_ == CURRENT_CONFIG ? 0.256f : 6.144f;
    volts = raw * (fullScale / 32768.0f);
    return ADCStatus::Ok;
}

ADCStatus ADS1115::read(ADCChannel channel, float &volts) {
    const ADCStatus status = startConversion(channel);
    return status == ADCStatus::Ok ? readResult(volts) : status;
}
