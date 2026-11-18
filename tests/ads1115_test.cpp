#include "ads1115.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <deque>

uint8_t DDRC = 0, PORTC = 0;

namespace {
constexpr uint8_t SDA = _BV(PC4), SCL = _BV(PC5);
std::deque<uint8_t> pin_reads;
unsigned long time_us = 0;

void start() {
    pin_reads.push_back(SCL);
    pin_reads.push_back(SDA);
}

void stop() {
    pin_reads.push_back(SCL);
    pin_reads.push_back(SDA);
}

void writtenByte(bool acknowledged = true) {
    for (int bit = 0; bit < 9; ++bit) pin_reads.push_back(SCL);
    pin_reads.push_back(acknowledged ? 0 : SDA);
}

void receivedByte(uint8_t value) {
    for (uint8_t mask = 0x80; mask; mask >>= 1) {
        pin_reads.push_back(SCL);
        pin_reads.push_back(value & mask ? SDA : 0);
    }
    pin_reads.push_back(SCL);
}

void registerWrite() {
    start();
    for (int i = 0; i < 4; ++i) writtenByte();
    stop();
}

void registerRead(uint16_t value) {
    start();
    writtenByte();
    writtenByte();
    start();
    writtenByte();
    receivedByte(value >> 8);
    receivedByte(value & 0xff);
    stop();
}
}

uint8_t getPinC() {
    assert(!pin_reads.empty());
    const uint8_t value = pin_reads.front();
    pin_reads.pop_front();
    return value;
}

void _delay_us(double duration) { time_us += static_cast<unsigned long>(duration); }
unsigned long micros() { return time_us += 4; }

int main() {
    ADS1115 adc;
    adc.begin();
    float voltage = 123.0f;

    start();
    writtenByte(false);
    stop();
    assert(adc.read(ADCChannel::Voltage, voltage) == ADCStatus::BusFault);
    assert(voltage == 123.0f && pin_reads.empty() && DDRC == 0);

    registerWrite();
    registerRead(0xB1C3);
    registerRead(20000);
    assert(adc.read(ADCChannel::Voltage, voltage) == ADCStatus::Ok);
    assert(std::fabs(voltage - 3.75f) < 0.00001f && pin_reads.empty());

    registerWrite();
    registerRead(0x8BC3);
    registerRead(0xC000);
    assert(adc.read(ADCChannel::Current, voltage) == ADCStatus::Ok);
    assert(std::fabs(voltage - (-0.128f)) < 0.00001f && pin_reads.empty());

    registerWrite();
    registerRead(0x8BC3);
    registerRead(0x8000);
    assert(adc.read(ADCChannel::Current, voltage) == ADCStatus::Saturated);
    assert(pin_reads.empty());

    registerWrite();
    assert(adc.startConversion(ADCChannel::Current) == ADCStatus::Ok);
    time_us += 4100;
    registerRead(0x0BC3); // Conversion still busy after the deadline.
    assert(adc.readResult(voltage) == ADCStatus::Timeout && pin_reads.empty());

    registerWrite();
    registerRead(0xB1C3); // A voltage result is stale for the requested current channel.
    assert(adc.read(ADCChannel::Current, voltage) == ADCStatus::BusFault && pin_reads.empty());

    pin_reads.push_back(0); // SCL held low at START and STOP.
    pin_reads.push_back(0);
    assert(adc.startConversion(ADCChannel::Current) == ADCStatus::BusFault && pin_reads.empty());
    assert(DDRC == 0);
    std::puts("ADS1115 driver tests passed.");
}
