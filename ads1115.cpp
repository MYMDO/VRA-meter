#include "ads1115.h"
#include <Arduino.h>

// --- Direct Port Manipulation for ATmega328P ---
// A4 = SDA = PC4, A5 = SCL = PC5
#define SDA_DDR   DDRC
#define SDA_PORT  PORTC
#define SDA_PINR  PINC
#define SDA_BIT   4

#define SCL_DDR   DDRC
#define SCL_PORT  PORTC
#define SCL_PINR  PINC
#define SCL_BIT   5

// NOP delay: 4 NOPs ≈ 250ns at 16MHz → ~200kHz I2C clock
#define I2C_NOP() __asm__ __volatile__("nop\n\tnop\n\tnop\n\tnop")

// --- Low-level I2C Pin Control ---
// sdaHigh/sclHigh: set DDR bit to 0 (input mode) — external pull-up pulls HIGH
// sdaLow/sclLow:   set DDR bit to 1 (output) + clear PORT bit → driven LOW
void ADS1115::sdaHigh() { SDA_DDR &= ~(1 << SDA_BIT); }
void ADS1115::sdaLow()  { SDA_DDR |= (1 << SDA_BIT); SDA_PORT &= ~(1 << SDA_BIT); }
void ADS1115::sclHigh() { SCL_DDR &= ~(1 << SCL_BIT); }
void ADS1115::sclLow()  { SCL_DDR |= (1 << SCL_BIT); SCL_PORT &= ~(1 << SCL_BIT); }
bool ADS1115::sdaRead() { return SDA_PINR & (1 << SDA_BIT); }

// --- Bus Control ---
void ADS1115::begin() {
    sdaHigh();
    sclHigh();
    i2cStop();
}

void ADS1115::i2cStart() {
    sdaLow();
    I2C_NOP();
    sclLow();
    I2C_NOP();
}

void ADS1115::i2cStop() {
    sdaLow();
    I2C_NOP();
    sclHigh();
    I2C_NOP();
    sdaHigh();
    I2C_NOP();
}

// --- Byte-Level I2C ---
bool ADS1115::i2cWriteByte(uint8_t data) {
    for (int i = 7; i >= 0; i--) {
        if (data & (1 << i))
            sdaHigh();
        else
            sdaLow();
        I2C_NOP();
        sclHigh();
        I2C_NOP();
        sclLow();
        I2C_NOP();
    }
    // Read ACK/NACK on 9th clock
    sdaHigh();  // release SDA for slave response
    I2C_NOP();
    sclHigh();
    I2C_NOP();
    bool ack = sdaRead();  // LOW = ACK, HIGH = NACK
    sclLow();
    I2C_NOP();
    return ack;
}

uint8_t ADS1115::i2cReadByte(bool ack) {
    uint8_t data = 0;
    sdaHigh();  // release SDA for reading
    for (int i = 7; i >= 0; i--) {
        sclHigh();
        I2C_NOP();
        if (sdaRead()) data |= (1 << i);
        I2C_NOP();
        sclLow();
        I2C_NOP();
    }
    // Send ACK (LOW) or NACK (HIGH)
    if (ack)
        sdaLow();
    else
        sdaHigh();
    I2C_NOP();
    sclHigh();
    I2C_NOP();
    sclLow();
    I2C_NOP();
    sdaHigh();  // release
    return data;
}

// --- Register-Level I2C ---

void ADS1115::writeRegister(uint8_t reg, uint16_t value) {
    i2cStart();
    if (!i2cWriteByte((ADS1115_ADDR << 1) | 0)) { last_i2c_error_ = true; i2cStop(); return; }
    if (!i2cWriteByte(reg))                      { last_i2c_error_ = true; i2cStop(); return; }
    if (!i2cWriteByte((uint8_t)(value >> 8)))    { last_i2c_error_ = true; i2cStop(); return; }
    if (!i2cWriteByte((uint8_t)(value & 0xFF)))  { last_i2c_error_ = true; i2cStop(); return; }
    i2cStop();
}

uint16_t ADS1115::readRegister(uint8_t reg) {
    i2cStart();
    if (!i2cWriteByte((ADS1115_ADDR << 1) | 0)) { last_i2c_error_ = true; i2cStop(); return 0; }
    if (!i2cWriteByte(reg))                      { last_i2c_error_ = true; i2cStop(); return 0; }
    i2cStart();
    if (!i2cWriteByte((ADS1115_ADDR << 1) | 1)) { last_i2c_error_ = true; i2cStop(); return 0; }
    uint8_t msb = i2cReadByte(true);
    uint8_t lsb = i2cReadByte(false);
    i2cStop();
    return ((uint16_t)msb << 8) | lsb;
}

// --- High-Level API ---

float ADS1115::readDifferential(uint8_t channel, uint16_t pga, float fs) {
    startConversion(channel, pga);
    delay(ADC_START_LEAD_MS);
    // Poll OS-bit for completion
    uint16_t attempts = 0;
    while (!(readRegister(ADS1115_REG_CONFIG) & ADS1115_CFG_OS)) {
        delay(1);
        if (++attempts > 100) break;
    }
    return readResult(fs);
}

void ADS1115::startConversion(uint8_t channel, uint16_t pga) {
    uint16_t mux = (channel == 0) ? MUX_A0_A1 : MUX_A2_A3;
    uint16_t config = ADS1115_CFG_OS
                    | mux
                    | pga
                    | ADS1115_CFG_MODE_SINGLE
                    | ADS1115_CFG_RATE_860
                    | ADS1115_CFG_COMP_QUE;
    writeRegister(ADS1115_REG_CONFIG, config);
}

float ADS1115::readResult(float fs) {
    int16_t raw = (int16_t)readRegister(ADS1115_REG_CONVERSION);
    return ((float)raw / 32767.0f) * fs;
}

int16_t ADS1115::readResultRaw() {
    return (int16_t)readRegister(ADS1115_REG_CONVERSION);
}

uint16_t ADS1115::readConfigReg() {
    return readRegister(ADS1115_REG_CONFIG);
}
