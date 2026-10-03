# Waveshare ESP32-S3-LCD-3.16 · VESC eBike Dashboard

Custom instrument cluster firmware for the **Waveshare ESP32-S3-LCD-3.16** (820×320 ST7701 RGB display), built for VESC-compatible motor controllers (tested on Flipsky 75100 V1).

Communicates over hardware UART at 115200 baud, reads live telemetry via VESC protocol packets, and features onboard persistent settings, battery analytics, two cluster UI styles, and transparent USB / Wi-Fi passthrough bridges for VESC Tool configuration without unplugging the display.

---

## Hardware & Wiring

### Pinout Reference

| Function | Pin | Connector on Waveshare Board | Notes |
| :--- | :--- | :--- | :--- |
| **VESC UART TX** | **GPIO 43** | PH1.0 4-pin UART (labeled TXD) | Connect to Flipsky COMM **RX** |
| **VESC UART RX** | **GPIO 44** | PH1.0 4-pin UART (labeled RXD) | Connect to Flipsky COMM **TX** |
| **GND** | **GND** | PH1.0 4-pin UART / USB | Common ground with VESC |
| **Button 1** | **GPIO 7** | SH1.0 4-pin I2C (SCL pin) | Active LOW (internal pullup), connect button to GND |
| **Button 2** | **GPIO 15** | SH1.0 4-pin I2C (SDA pin) | Active LOW (internal pullup), connect button to GND |
| **Display Panel** | **ST7701** | Internal RGB565 bus | 820×320, 16MB Flash, 8MB Octal PSRAM |

> **Warning on Power Supply:**  
> The 3.16" LCD backlight draws 350–500mA at full brightness. **Do not** power the board from the VESC COMM port's internal 5V pin. The controller's internal 5V regulator is meant for Hall sensors and logic, and pulling 500mA through it can overheat the controller or cause brownouts. Use an external 12V→5V DC-DC step-down converter from your main battery or power via USB 5V.

---

## Screens & Features

### 1. Main Ride Dashboard (`SCREEN_RIDE_DASH`)
Selectable in Settings:
* **Style A (Analog Dial):** Left-hugging 180° tachometer arc (0–120% duty cycle, redline past 100% for Field Weakening). Digital duty readout sits inside the central hub cap. Giant high-contrast digital speedometer, battery percentage and pack voltage, real-time power (W) and motor phase amps (A), live throttle bar, and trip distance.
* **Style B (Horizontal Bar):** Wide horizontal duty bar across the top with redline FW zone, large center speedometer, and status metrics below.

### 2. Energy & Battery Analytics (`SCREEN_ENERGY_STATS`)
* Real-time consumption efficiency (**Wh/km**).
* Total trip energy consumed (Wh) and charge used (Ah).
* Remaining usable energy (Wh) and dynamic range estimation (km).
* Battery State of Health (SoH %) and nominal vs. learned pack capacity.

### 3. Power & Dynamics Analytics (`SCREEN_PERF_STATS`)
* Peak electrical power recorded this trip (W / kW).
* Peak battery current (A) and peak motor phase current (A).
* Dual thermal monitoring: Motor temperature and VESC MOSFET temperature with color-coded warnings.
* Trip odometer and lifetime vehicle odometer (stored in flash).

### 4. Interactive Settings Menu (`SCREEN_SETTINGS_MENU`)
Divided into 5 submenus with inline editing and NVS flash persistence:
1. **Power & Drive Limits:** Battery current limit (A), Phase current limit (A), Field Weakening current (A), Speed limit (km/h), Throttle ADC input channel selection.
2. **Display & UI:** Cluster style toggle (Analog / Horizontal), Screen backlight brightness (20%–100%).
3. **Battery Profiles:** Dual profile switcher (e.g. 52V Fresh pack vs. Daily pack), nominal Wh ratings, voltage calibration trim, learning reset.
4. **Wheel & Gearing:** Motor pole pairs (e.g. 15 for BBS / direct drive), tire diameter (mm), mechanical gear ratio.
5. **System & Diagnostics:** Pull/sync live motor limits from VESC (`COMM_GET_MCCONF`), start USB passthrough bridge, start Wi-Fi bridge, reset trip stats, factory reset, hardware link status.

### 5. Transparent VESC USB Bridge (`SCREEN_VESC_BRIDGE`)
* Plugs into your PC with a USB-C cable while connected to the bike.
* Acts as a transparent serial forwarding bridge between PC VESC Tool and the Flipsky controller.
* Configured with 4KB FreeRTOS queues on USB CDC and hardware UART so that large packets (like `COMM_SET_MCCONF`, ~650 bytes) write without packet drops or timeouts.

### 6. Wireless Wi-Fi TCP Bridge (`SCREEN_WIFI_BRIDGE`)
* Launches an on-demand 2.4 GHz SoftAP on the ESP32-S3:
  * **SSID:** `VESC-DASH-AP` (Open, no password)
  * **IP:** `192.168.4.1`
  * **Port:** `6510` (VESC standard TCP port)
* Connect your phone to the Wi-Fi network and open the mobile VESC Tool app → Connection → TCP → `192.168.4.1:6510`.
* Radio completely powers down when exiting bridge mode to eliminate idle power draw.

---

## Controls & Navigation

### Normal Riding Mode
| Button Action | Result |
| :--- | :--- |
| **BTN1 Short** | Previous Screen (`Ride Dash` ↔ `Perf Stats` ↔ `Energy Stats`) |
| **BTN2 Short** | Next Screen (`Ride Dash` → `Energy Stats` → `Perf Stats`) |
| **BTN1 Long (>600ms)** | Open Settings Menu |
| **BTN2 Long (>600ms)** | Reset Trip Distance & Trip Statistics |
| **BTN1 + BTN2 Short** | Quick Toggle Battery Profile (Profile 1 ↔ Profile 2) |

### In Settings Menu
| Button Action | Result |
| :--- | :--- |
| **BTN1 Short** | Cursor UP (or Decrement value in Edit Mode) |
| **BTN2 Short** | Cursor DOWN (or Increment value in Edit Mode) |
| **BTN1 + BTN2 Short** | Select / Enter Submenu / Toggle Edit Mode / Save |
| **BTN1 Long** | Cancel Edit / Back to Root Menu |
| **BTN2 Long** | Exit Settings and Return to Ride Dashboard |

### In Bridge Mode (USB or Wi-Fi)
| Button Action | Result |
| :--- | :--- |
| **Hold BTN1 / BTN2** or **[1+2]** | Stop bridge, turn off Wi-Fi radio, and return to Ride Dashboard |

---

## Building and Flashing

### Requirements
* [PlatformIO CLI](https://platformio.org/) (`pio`) or VS Code with the PlatformIO extension.
* Quality USB-C cable (ensure it carries data lines, not charge-only).

### Compile and Flash via USB
```bash
# Clone the repository
git clone https://github.com/aerosock/vesc-3.13-display.git
cd vesc-3.13-display

# Build and upload to connected ESP32-S3
pio run -t upload

# Open serial console (115200 baud)
pio run -t monitor
```

### Linux Permissions (udev)
If upload fails with permission errors on `/dev/ttyACM0`:
```bash
# Add yourself to dialout group
sudo usermod -aG dialout $USER

# Or run the included setup script (Fedora/Ubuntu):
chmod +x scripts/setup_fedora.sh
./scripts/setup_fedora.sh
```

---

## Hardware Quirks & Troubleshooting

* **Green On-Board LED stays lit:**  
  The small green LED on the Waveshare board is directly wired to the 3.3V power rail after the LDO. It is a hardware power indicator, not connected to a GPIO pin. It cannot be disabled in software. If you don't want it visible in an enclosure, cover it with black electrical tape or desolder its current-limiting resistor.

* **ESP32-S3 doesn't enter bootloader automatically:**  
  Hold down the **BOOT** button (GPIO 0), press and release the **RESET** button, then release **BOOT**. This forces the chip into ROM download mode. Run `pio run -t upload` again.

* **Throttle gauge stuck at 33%:**  
  The firmware defaults to VESC ADC channel 1 (Channel index `0`). If your throttle is connected to ADC2 on the VESC or if ADC values are inverted, verify your VESC Tool ADC app settings under *App Settings → ADC*. The display automatically migrates older NVS configurations to ADC channel 0 on boot.

* **Writing motor settings in VESC Tool fails over USB:**  
  VESC Tool configuration writes send a single 500–650+ byte burst. In standard Arduino ESP32 cores, USB CDC buffers default to 256 bytes, truncating the write packet. This firmware configures 4096-byte queues before `Serial.begin()`. If you modify `main.cpp`, do not reduce buffer sizes.

* **Testing on a desk without a bike (Simulation Mode):**  
  In `include/board_config.h`, set:
  ```cpp
  #define SIMULATION_MODE 1
  ```
  This runs an automated 45-second simulated ride cycle with acceleration, steady cruise, and field weakening without requiring a VESC connection. Set back to `0` for real UART operation.

* **Native Linux Headless Simulator:**  
  You can build and test all 13 screens on a Linux desktop without any ESP32 hardware:
  ```bash
  chmod +x tools/simulator/build_and_run.sh
  ./tools/simulator/build_and_run.sh
  ```
  Screenshots of every screen state are rendered to `tools/simulator/screenshots/`.
