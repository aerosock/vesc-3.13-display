THIS IS FULL AI SLOP WARNING!!! BUT IT WORKS


# Waveshare ESP32-S3-LCD-3.16 · eBike Dashboard for Flipsky 75100 V1

Custom, high-refresh-rate eBike dashboard running on the **Waveshare ESP32-S3-LCD-3.16** (820x320 landscape ST7701 RGB display), communicating with a **Flipsky 75100 V1 Basic** (VESC-compatible controller) over UART.

Includes two distinct selectable dashboard themes, a realistic physics simulation mode (for testing on your desk without the bike connected), hardware button controls wired to the I2C header, and a full interactive web simulator.

---

## 🌟 Dashboard Themes

### Theme 1: Practical Modern Sport
* **Top Horizontal RPM Bar:** Full-width dynamic tachometer bar (0–12,000 RPM) with smooth color transitions (Cyan $\rightarrow$ Amber $\rightarrow$ Red) and an active flashing **SHIFT LIGHT** at the redline.
* **Giant Center Speedometer:** High-contrast digital speed readout with units (`KM/H` or `MPH`), power map pill (`ECO`, `STD`, `SPORT`, `BOOST`), and real-time throttle bar.
* **Left Telemetry Wing:** Battery State of Charge (SoC %), graphical level bar, pack voltage (`54.2V`), and current draw (`18.4A`), plus real-time power output in Watts.
* **Right Telemetry Wing:** Dual thermal monitoring (Motor Temp & VESC Mosfet Temp with over-temperature alerts), Trip Odometer, and Lifetime ODO.
* **Bottom Status Ribbon:** VESC UART link status, button action guide, and system runtime clock.

### Theme 2: Yamaha R1 Superbike Analog Needle Dash
* **Left Circular Analog Gauge:** 270° dial face with numbered increments (0 to 12 x1000 RPM), redline zone (9k–12k), and a glowing, illuminated needle.
* **Iconic Superbike Startup Needle Sweep:** On power-up or whenever activated, the needle sweeps from 0 $\rightarrow$ 12,000 RPM, pauses at peak redline, and sweeps back to idle, just like a Yamaha YZF-R1 / superbike dash boot sequence!
* **Right Digital Racing Cluster:** Power Map / Gear box (`1`, `2`, `3`, `4`), large aggressive digital speedometer, high-visibility wattage (`W`), amperage (`A`), battery status, thermals, and trip distance.

---

## 🎮 Physical Button Controls (Connected to I2C Port)

The 2 buttons are connected to the onboard **SH1.0 4-PIN I2C connector**:
* **Button 1 $\rightarrow$ SCL (GPIO 7) & GND:**
  * **Short Press:** Switch Dashboard Style (Theme 1 $\leftrightarrow$ Theme 2).
  * **Long Press (>600ms):** Re-trigger the Yamaha R1 Needle Sweep test at any time!
* **Button 2 $\rightarrow$ SDA (GPIO 15) & GND:**
  * **Short Press:** Cycle Power Map / Riding Mode (`ECO` $\rightarrow$ `STD` $\rightarrow$ `SPORT` $\rightarrow$ `BOOST`).
  * **Long Press (>600ms):** Reset Trip Odometer to `0.0 km`.

---

## 🔌 Hardware Wiring: ESP32-S3 to Flipsky 75100 V1

### 1. UART Communication Wiring
Connect the **PH1.0 4-PIN UART** header on the Waveshare board to the **7-Pin COMM port** on the Flipsky 75100:

| Waveshare ESP32-S3 Pin | Signal | Flipsky 75100 COMM Pin | Notes |
| :--- | :--- | :--- | :--- |
| **GPIO 43 (TXD)** | UART TX $\rightarrow$ | **RX** (Pin 5) | Cross-over connection |
| **GPIO 44 (RXD)** | UART RX $\leftarrow$ | **TX** (Pin 4) | Cross-over connection |
| **GND** | Ground | **GND** (Pin 6/7) | Common ground reference |

> [!NOTE]
> Both the ESP32-S3 and the Flipsky 75100 STM32 MCU operate at **3.3V logic levels**. No logic-level shifter is required.

### 2. Power Supply Considerations (Crucial)
* The Waveshare ESP32-S3 board with its 3.16" LCD backlight can draw **300mA to 500mA peak**.
* The Flipsky 75100 internal 5V output (Pin 1 on COMM) is provided by a small internal buck/linear regulator primarily intended for hall sensors and receivers.
* **Best Practice:** Power the ESP32-S3 through an external **12V $\rightarrow$ 5V DC-DC buck converter** connected to your ebike battery, or via a USB 5V supply, to prevent any risk of brownouts or overloading the Flipsky internal regulator under peak backlight brightness.

---

## ⚙️ Flipsky 75100 VESC Tool Configuration

Before connecting the live UART:
1. Connect the Flipsky 75100 to your PC via USB and launch **VESC Tool**.
2. Go to **App Settings** $\rightarrow$ **General**.
3. Set **App to Use** to **`UART`** (or **`PPM and UART`** / **`ADC and UART`** depending on your throttle input).
4. Go to **App Settings** $\rightarrow$ **UART**.
5. Ensure the **Baud Rate** is set to **`115200 bps`**.
6. Click the **Write App Configuration** button (down arrow with 'M') to save.

---

## 💻 Flashing from Linux Fedora 42

### 1. Run the Fedora Setup Script
To configure your serial permissions (`dialout` group) and install the Espressif udev rules:
```bash
cd /home/aero/Documents/antigravity/blissful-pythagoras
./scripts/setup_fedora.sh
```
*(If prompted for group permissions, run `newgrp dialout` or re-login).*

### 2. Install PlatformIO
If not already installed:
```bash
uv tool install platformio
# or: pip install --user platformio
```

### 3. Connect the Board & Flash
1. Plug the Waveshare ESP32-S3 board into your Fedora PC using a quality USB-C data cable.
2. Build and upload firmware:
   ```bash
   pio run -t upload
   ```
3. Open the serial monitor to view live logs:
   ```bash
   pio run -t monitor
   ```

> [!TIP]
> **Entering Bootloader Mode (if upload hangs or fails):**
> Hold down the **BOOT** button, press and release the **RST** button, then release **BOOT**. The ESP32-S3 will enter ROM download mode immediately.

---

## 🔄 Switching from Simulation to Live VESC Mode

In `include/board_config.h`:
```cpp
// Change this:
#define SIMULATION_MODE  1  // Self-test simulation mode

// To this:
#define SIMULATION_MODE  0  // Live Flipsky 75100 UART communication
```
Re-flash with `pio run -t upload`. The dashboard will begin reading live motor RPM, battery voltage, current, temperatures, and distance directly from your Flipsky 75100 V1!
