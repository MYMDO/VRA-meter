#ifndef ADS1115_H
#define ADS1115_H

#include <Arduino.h>
#include "config.h"

// ADS1115 register addresses
#define ADS1115_REG_CONVERSION  0x00
#define ADS1115_REG_CONFIG      0x01

// Config register bits
#define ADS1115_CFG_OS          0x8000  // Start single conversion
#define ADS1115_CFG_MODE_SINGLE 0x0100  // Single-shot mode
#define ADS1115_CFG_RATE_860    0x00C0  // 860 SPS (fastest)
#define ADS1115_CFG_RATE_475    0x00A0  // 475 SPS
#define ADS1115_CFG_RATE_250    0x0080  // 250 SPS
#define ADS1115_CFG_COMP_QUE    0x0003  // Disable comparator

// Mux values for differential readings
#define MUX_A0_A1  0x0000  // Differential A0 vs A1
#define MUX_A2_A3  0x3000  // Differential A2 vs A3

class ADS1115 {
public:
    void begin();
    
    // Blocking read: start conversion + wait + read result
    float readDifferential(uint8_t channel, uint16_t pga, float fs);
    
    // Non-blocking: start conversion now (returns immediately)
    void startConversion(uint8_t channel, uint16_t pga);
    
    // Non-blocking: read result (call after conversion completes ~1.2ms later)
    float readResult(float fs);
    
    float readCurrent();
    float readVoltage();

private:
    void writeRegister(uint8_t reg, uint16_t value);
    uint16_t readRegister(uint8_t reg);
    void i2cStart();
    void i2cStop();
    void i2cWriteByte(uint8_t data);
    uint8_t i2cReadByte(bool ack);
    
    // Direct port manipulation for speed (~200kHz I2C)
    inline void sdaHigh();
    inline void sdaLow();
    inline void sdaInput();
    inline void sclHigh();
    inline void sclLow();
    inline void sclInput();
    inline bool sdaRead();
};

#endif
