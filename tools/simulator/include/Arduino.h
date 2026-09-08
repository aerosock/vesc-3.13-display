#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <chrono>
#include <thread>
#include <algorithm>

typedef bool boolean;
typedef uint8_t byte;

#define INPUT 0
#define OUTPUT 1
#define INPUT_PULLUP 2
#define LOW 0
#define HIGH 1

inline int g_sim_pin_state[64] = {
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1
};
inline uint32_t g_sim_time_ms = 1000;

inline void pinMode(int pin, int mode) {}
inline int digitalRead(int pin) { return (pin >= 0 && pin < 64) ? g_sim_pin_state[pin] : HIGH; }
inline void simSetPin(int pin, int state) { if (pin >= 0 && pin < 64) g_sim_pin_state[pin] = state; }
inline void simAdvanceMs(uint32_t ms) { g_sim_time_ms += ms; }

inline unsigned long millis(void) {
    return g_sim_time_ms;
}

inline unsigned long micros(void) {
    static auto start_time = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();
    return (unsigned long)std::chrono::duration_cast<std::chrono::microseconds>(now - start_time).count();
}

inline void delay(unsigned long ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

inline void delayMicroseconds(unsigned int us) {
    std::this_thread::sleep_for(std::chrono::microseconds(us));
}

inline long map(long x, long in_min, long in_max, long out_min, long out_max) {
    return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

class MockSerial {
public:
    void print(const char *s) { printf("%s", s); }
    void print(int n) { printf("%d", n); }
    void print(float f) { printf("%f", f); }
    void println(const char *s = "") { printf("%s\n", s); }
    void println(int n) { printf("%d\n", n); }
    void println(float f) { printf("%f\n", f); }
    template<typename... Args>
    void printf(const char *fmt, Args... args) { ::printf(fmt, args...); }
};

extern MockSerial Serial;
