#include "ads1115.h"
#include <Arduino.h>

// I2C pins - adjust to your board
#define SDA_PIN  A4
#define SCL_PIN  A5

void ADS1115::begin() {
    pinMode(SDA_PIN, INPUT_PULLUP);
    pinMode(SCL_PIN, INPUT_PULLUP);
    digitalWrite(SDA_PIN, HIGH);
    digitalWrite(SCL_PIN, HIGH);
    i2cStop();
}

void ADS1115::i2cDelay() {
    delayMicroseconds(I2C_DELAY_US);
}

void ADS1115::i2cStart() {
    digitalWrite(SDA_PIN, HIGH);
    pinMode(SDA_PIN, OUTPUT);
    i2cDelay();
    digitalWrite(SCL_PIN, HIGH);
    pinMode(SCL_PIN, OUTPUT);
    i2cDelay();
    digitalWrite(SDA_PIN, LOW);
    i2cDelay();
}

void ADS1115::i2cStop() {
    digitalWrite(SDA_PIN, LOW);
    pinMode(SDA_PIN, OUTPUT);
    i2cDelay();
    digitalWrite(SCL_PIN, HIGH);
    pinMode(SCL_PIN, INPUT);
    i2cDelay();
    digitalWrite(SDA_PIN, HIGH);
    pinMode(SDA_PIN, INPUT);
    i2cDelay();
}

void ADS1115::i2cWriteByte(uint8_t data) {
    for (int i = 7; i >= 0; i--) {
        digitalWrite(SDA_PIN, (data >> i) & 1);
        i2cDelay();
        digitalWrite(SCL_PIN, HIGH);
        i2cDelay();
        digitalWrite(SCL_PIN, LOW);
        i2cDelay();
    }
    // ACK
    pinMode(SDA_PIN, INPUT);
    i2cDelay();
    digitalWrite(SCL_PIN, HIGH);
    i2cDelay();
    digitalWrite(SCL_PIN, LOW);
    i2cDelay();
    pinMode(SDA_PIN, OUTPUT);
}

uint8_t ADS1115::i2cReadByte(bool ack) {
    uint8_t data = 0;
    pinMode(SDA_PIN, INPUT);
    for (int i = 7; i >= 0; i--) {
        digitalWrite(SCL_PIN, HIGH);
        i2cDelay();
        if (digitalRead(SDA_PIN)) data |= (1 << i);
        i2cDelay();
        digitalWrite(SCL_PIN, LOW);
        i2cDelay();
    }
    // ACK/NAK
    digitalWrite(SDA_PIN, ack ? LOW : HIGH);
    pinMode(SDA_PIN, OUTPUT);
    i2cDelay();
    digitalWrite(SCL_PIN, HIGH);
    i2cDelay();
    digitalWrite(SCL_PIN, LOW);
    i2cDelay();
    pinMode(SDA_PIN, INPUT);
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
    uint16_t mux;
    if (channel == 0) {
        mux = MUX_A0_A1;
    } else {
        mux = MUX_A2_A3;
    }
    
    uint16_t config = ADS1115_CFG_OS
                    | mux
                    | pga
                    | ADS1115_CFG_MODE_SINGLE
                    | ADS1115_CFG_RATE_860
                    | ADS1115_CFG_COMP_QUE;
    
    writeRegister(ADS1115_REG_CONFIG, config);
    
    // Wait for conversion (860 SPS ≈ 1.2 ms, give 2 ms margin)
    delay(2);
    
    // Poll for completion (OS bit goes high when done)
    uint16_t attempts = 0;
    while (!(readRegister(ADS1115_REG_CONFIG) & ADS1115_CFG_OS)) {
        delay(1);
        if (++attempts > 100) break; // timeout
    }
    
    int16_t raw = (int16_t)readRegister(ADS1115_REG_CONVERSION);
    
    // Convert to voltage: (raw / 32767.0) * full_scale
    float voltage = ((float)raw / 32767.0f) * fs;
    
    return voltage;
}

float ADS1115::readCurrent() {
    float v_shunt = readDifferential(ADS1115_CH_CURRENT, CURRENT_PGA, FS_0256V);
    return v_shunt / SHUNT_RESISTANCE;
}

float ADS1115::readVoltage() {
    return readDifferential(ADS1115_CH_VOLTAGE, VOLTAGE_PGA, FS_4096V);
}
