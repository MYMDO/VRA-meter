#ifndef ADS1115_H
#define ADS1115_H

#include <Arduino.h>
#include "config.h"

// --- ADS1115 Register Addresses ---
#define ADS1115_REG_CONVERSION  0x00
#define ADS1115_REG_CONFIG      0x01

// --- Config Register Bits ---
#define ADS1115_CFG_OS          0x8000  // Start single conversion / read ready flag
#define ADS1115_CFG_MODE_SINGLE 0x0100  // Single-shot mode
#define ADS1115_CFG_RATE_860    0x00C0  // 860 SPS (fastest, ~1.16ms conversion)
#define ADS1115_CFG_COMP_QUE    0x0003  // Disable comparator

// --- Mux Values for Differential Readings ---
#define MUX_A0_A1  0x0000  // Differential A0 vs A1
#define MUX_A2_A3  0x3000  // Differential A2 vs A3

// --- PGA Gain Settings (bits [11:9]) ---
#define PGA_6144V   0x0000
#define PGA_4096V   0x0200
#define PGA_2048V   0x0400  // default
#define PGA_1024V   0x0600
#define PGA_0512V   0x0800
#define PGA_0256V   0x0A00

// --- Full-Scale Voltages ---
#define FS_0256V  0.256f
#define FS_6144V  6.144f

// --- ADC Saturation Detection ---
// Raw code near full-scale indicates PGA saturation (current > measurable range)
#define ADC_SATURATION_THRESHOLD  32700

// --- Channel Assignments (hardware-specific, not user-tunable) ---
#define ADS1115_CH_CURRENT  0   // A0-A1: differential current across shunt
#define ADS1115_CH_VOLTAGE  1   // A2-A3: differential voltage across battery (Kelvin)

// --- PGA per Channel ---
#define CURRENT_PGA  PGA_0256V  // ±256 mV range (max precision on shunt)
#define VOLTAGE_PGA  PGA_6144V  // ±6.144 V range (covers up to 4.25V Li-ion full charge)

class ADS1115 {
public:
    void begin();

    // Blocking read: start conversion, wait, return voltage (float)
    float readDifferential(uint8_t channel, uint16_t pga, float fs);

    // Non-blocking: start conversion (returns immediately, ~1ms I2C write)
    void startConversion(uint8_t channel, uint16_t pga);

    // Read completed result (call ~2ms after startConversion)
    float readResult(float fs);

    // Read raw int16_t code (for saturation detection)
    int16_t readResultRaw();

    // Read config register (for OS-bit completion polling)
    uint16_t readConfigReg();

    // I2C error flag — true if any write got NACK from slave
    bool hadI2CError() const { return last_i2c_error_; }

    // Clear I2C error flag (call before new transaction)
    void clearError() { last_i2c_error_ = false; }

private:
    void writeRegister(uint8_t reg, uint16_t value);
    uint16_t readRegister(uint8_t reg);

    // Bit-banged I2C via direct PORTC manipulation (~200kHz)
    void i2cStart();
    void i2cStop();
    bool i2cWriteByte(uint8_t data);   // true=ACK, false=NACK
    uint8_t i2cReadByte(bool ack);

    // ATmega328P: A4 = SDA = PC4, A5 = SCL = PC5
    inline void sdaHigh();
    inline void sdaLow();
    inline void sclHigh();
    inline void sclLow();
    inline bool sdaRead();

    bool last_i2c_error_ = false;
};

#endif
