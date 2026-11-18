#pragma once

#include <cstdint>
#include <deque>
#include <string>

#define PROGMEM
#define pgm_read_float(address) (*(address))
#define F(text) reinterpret_cast<const __FlashStringHelper *>(text)

class __FlashStringHelper;
constexpr uint8_t LOW = 0, HIGH = 1, OUTPUT = 1;

unsigned long millis();
unsigned long micros();
void digitalWrite(uint8_t pin, uint8_t level);
void pinMode(uint8_t pin, uint8_t mode);

constexpr uint8_t PC4 = 4, PC5 = 5;
#define _BV(bit) (1 << (bit))
extern uint8_t DDRC, PORTC;
uint8_t getPinC();
#define PINC getPinC()

struct FakeSerial {
    std::deque<char> input;
    std::string output;
    void begin(unsigned long) {}
    bool available() const { return !input.empty(); }
    int read() {
        const char value = input.front();
        input.pop_front();
        return value;
    }
    void print(const __FlashStringHelper *value) {
        output += reinterpret_cast<const char *>(value);
    }
    void print(unsigned long value) { output += std::to_string(value); }
    void print(char value) { output += value; }
    void print(float value, int digits);
    template <typename T> void println(T value) { print(value); output += '\n'; }
    void println(float value, int digits) { print(value, digits); output += '\n'; }
};

extern FakeSerial Serial;
