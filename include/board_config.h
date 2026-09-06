#pragma once

#include <Arduino.h>

// ==============================================================================
// Waveshare ESP32-S3-LCD-3.16 Board Configuration & Pin Mapping
// ==============================================================================

// Display Dimensions (Physical panel: 320x820, rendered in landscape: 820x320)
#define LCD_WIDTH   820
#define LCD_HEIGHT  320

// ST7701 Sideband SPI Command Interface
#define LCD_PIN_CS      0   // Shared with BOOT button (used only after init)
#define LCD_PIN_MOSI    1
#define LCD_PIN_SCK     2
#define LCD_PIN_RST     16

// ST7701 RGB Parallel Timing & Clock
#define LCD_PIN_HSYNC   38
#define LCD_PIN_VSYNC   39
#define LCD_PIN_DE      40
#define LCD_PIN_PCLK    41
#define LCD_FREQ_HZ     14000000 // 14 MHz RGB clock (eliminates PSRAM DMA FIFO underrun ripples while maintaining 40Hz refresh)

// ST7701 RGB Bus Data Pins
// Red (5-bit: R0 - R4)
#define LCD_PIN_R0      17
#define LCD_PIN_R1      46
#define LCD_PIN_R2      3
#define LCD_PIN_R3      8
#define LCD_PIN_R4      18

// Green (6-bit: G0 - G5)
#define LCD_PIN_G0      14
#define LCD_PIN_G1      13
#define LCD_PIN_G2      12
#define LCD_PIN_G3      11
#define LCD_PIN_G4      10
#define LCD_PIN_G5      9

// Blue (5-bit: B0 - B4)
#define LCD_PIN_B0      21
#define LCD_PIN_B1      5
#define LCD_PIN_B2      45
#define LCD_PIN_B3      48
#define LCD_PIN_B4      47

// Backlight (Inverted PWM on GPIO 6)
#define LCD_PIN_BL      6
#define BL_PWM_CHANNEL  1
#define BL_PWM_FREQ     50000
#define BL_PWM_RES      8

// ==============================================================================
// Buttons Connected to I2C Port
// ==============================================================================
// On Waveshare ESP32-S3-LCD-3.16, the SH1.0 4-PIN I2C connector provides:
// Pin 1: 3V3
// Pin 2: GND
// Pin 3: SDA -> GPIO 15
// Pin 4: SCL -> GPIO 7
//
// When 2 buttons are connected between SCL -> GND and SDA -> GND:
#define PIN_BTN_1       7    // Button 1 (SCL pin): Toggle Style / Re-trigger R1 Sweep
#define PIN_BTN_2       15   // Button 2 (SDA pin): Cycle Ride Mode / Reset Trip

// Optional Onboard BOOT button
#define PIN_BTN_BOOT    0

// ==============================================================================
// VESC UART Interface (Flipsky 75100 V1 Basic)
// ==============================================================================
// Onboard PH1.0 4-PIN UART Header:
// TX (ESP32-S3 TXD) -> GPIO 43  ==> Connects to Flipsky RX
// RX (ESP32-S3 RXD) -> GPIO 44  ==> Connects to Flipsky TX
#define VESC_UART_PORT      Serial1
#define VESC_UART_TX_PIN    43
#define VESC_UART_RX_PIN    44
#define VESC_UART_BAUDRATE  115200

// Battery Analog Sensing (onboard divider if used)
#define PIN_BATTERY_ADC     4

// ==============================================================================
// Feature Flags
// ==============================================================================
// 1 = Self-running simulation mode (test without VESC connected)
// 0 = Live VESC UART communication with Flipsky 75100
#define SIMULATION_MODE     1
