#include "ads1115.h"
#include <Arduino.h>

// Direct port manipulation for ATmega328P
// A4 = SDA = PC4, A5 = SCL = PC5
#define SDA_DDR   DDRC
#define SDA_PORT  PORTC
#define SDA_PINR  PINC
#define SDA_BIT   4

#define SCL_DDR   DDRC
#define SCL_PORT  PORTC
#define SCL_PINR  PINC
#define SCL_BIT   5

// Minimal I2C delay — one NOP = 62.5ns at 16MHz
// 4 NOPs ≈ 250ns per half-period → ~200kHz clock
#define I2C_NOP() __asm__ __volatile__("nop\n\tnop\n\tnop\n\tnop")

void ADS1115::sdaHigh() { SDA_DDR &= ~(1 << SDA_BIT); }  // input = pull-up
void ADS1115::sdaLow()  { SDA_DDR |= (1 << SDA_BIT); SDA_PORT &= ~(1 << SDA_BIT); }
void ADS1115::sdaInput(){ SDA_DDR &= ~(1 << SDA_BIT); }
void ADS1115::sclHigh() { SCL_DDR &= ~(1 << SCL_BIT); }
void ADS1115::sclLow()  { SCL_DDR |= (1 << SCL_BIT); SCL_PORT &= ~(1 << SCL_BIT); }
void ADS1115::sclInput(){ SCL_DDR &= ~(1 << SCL_BIT); }
bool ADS1115::sdaRead() { return SDA_PINR & (1 << SDA_BIT); }

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

void ADS1115::i2cWriteByte(uint8_t data) {
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
    // ACK
    sdaInput();
    I2C_NOP();
    sclHigh();
    I2C_NOP();
    sclLow();
    I2C_NOP();
    sdaHigh(); // release
}

uint8_t ADS1115::i2cReadByte(bool ack) {
    uint8_t data = 0;
    sdaInput();
    for (int i = 7; i >= 0; i--) {
        sclHigh();
        I2C_NOP();
        if (sdaRead()) data |= (1 << i);
        I2C_NOP();
        sclLow();
        I2C_NOP();
    }
    // ACK/NAK
    if (ack)
        sdaLow();
    else
        sdaHigh();
    I2C_NOP();
    sclHigh();
    I2C_NOP();
    sclLow();
    I2C_NOP();
    sdaHigh(); // release
    return data;
}

void ADS1115::writeRegister(uint8_t reg, uint16_t value) {
    i2cStart();
    i2cWriteByte((ADS1115_ADDR << 1) | 0);
    i2cWriteByte(reg);
    i2cWriteByte((uint8_t)(value >> 8));
    i2cWriteByte((uint8_t)(value & 0xFF));
    i2cStop();
}

uint16_t ADS1115::readRegister(uint8_t reg) {
    i2cStart();
    i2cWriteByte((ADS1115_ADDR << 1) | 0);
    i2cWriteByte(reg);
    i2cStart();
    i2cWriteByte((ADS1115_ADDR << 1) | 1);
    uint8_t msb = i2cReadByte(true);
    uint8_t lsb = i2cReadByte(false);
    i2cStop();
    return ((uint16_t)msb << 8) | lsb;
}

float ADS1115::readDifferential(uint8_t channel, uint16_t pga, float fs) {
    startConversion(channel, pga);
    delay(2); // 860 SPS → 1.16ms, give 0.84ms margin
    // Poll for completion
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
    // Conversion starts immediately after I2C write completes (~1ms)
    // Takes ~1.16ms at 860 SPS → ready in ~2.2ms from now
}

float ADS1115::readResult(float fs) {
    int16_t raw = (int16_t)readRegister(ADS1115_REG_CONVERSION);
    return ((float)raw / 32767.0f) * fs;
}

float ADS1115::readCurrent() {
    float v_shunt = readDifferential(ADS1115_CH_CURRENT, CURRENT_PGA, FS_0256V);
    return v_shunt / SHUNT_RESISTANCE;
}

float ADS1115::readVoltage() {
    return readDifferential(ADS1115_CH_VOLTAGE, VOLTAGE_PGA, FS_6144V);
}
