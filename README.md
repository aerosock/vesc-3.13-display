>[!WARNING]  
>for the dear anti-ai purists, this project was in part made with its help.

# UART display for VESC-based controllers on Waveshare ESP32-S3-LCD-3.16 module
<p align="center">
        <img src="https://github.com/user-attachments/assets/4cbeecfd-657c-41e2-86b3-c21ae2f0b7b6" width="48%" alt="image 1"
 ┃   />
        <img src="https://github.com/user-attachments/assets/77a578a0-82db-45a7-a96b-6211b44c7af9" width="48%" alt="image 2" 
 ┃   />
      </p>

My spin on the VESC display. 

Simple 3D printed case, uses two MX style keyboard switches for navigation. Communicates with the VESC through UART, which does alright even on my 1m+ long wire harness. 

Based on this [module](https://www.waveshare.com/product/esp32-related/boards-kits/esp32-s3/esp32-s3-lcd-3.16.htm).
820×320(20,5:8 ratio) ST7701 RGB display, ESP32-S3 microcontroller

Tested on Flipsky 75100 V1(the cheapest crap ever) with firmware version 7. Might misbehave with other controllers because cuh.

Shows live stats(speed, wattage, amperage, voltage, battery info), features long term statistics, capable of changing most basic VESC settings on the go, supports USB to UART bridge for programming through desktop VESC Tool as well as TCP WIFI connection for the mobile app.

---

## Wiring
<img width="400" height="800" alt="schematic" src="https://github.com/user-attachments/assets/f898032d-5353-4e6c-a166-ca9a4cf585e8" />

I made this beautiful illustration because im too lazy to open easyeda. basically really simple. 

the screen sips like 300-500mA tops so you can feed it from the on board 5v from the vesc from my experience

the 3v3 on the uart connector is a power output not input so you dont want to connect it anywhere

the only 5v input is unfortunately on the usb plug and it means you have to do solderslop if you dont have the correct size plug for it(waveshare only bundles 2 plugs, for uart and i2c)

sda and scl pins on i2c port can be used as regular digital gpio ports as well, that how the buttons are implemented without an i2c splitter thingy

---

## Screens & Features

There are 2 dashboard styles. 

one more practical with the duty cycle bar on the top 

<img width="820" height="320" alt="05_horizontal_field_weakening" src="https://github.com/user-attachments/assets/e9a89805-953f-4a46-9c36-872eba122e20" />


and another one with an analog style tachometer-duty cycle scale. it also does cool needle swipe on start up like motorcycles and cars!!!!

<img width="820" height="320" alt="03_analog_field_weakening" src="https://github.com/user-attachments/assets/b3408bc5-dd2b-44e6-8b0f-a9d9d4c600b9" />

statistics screens and whatnot are pretty self explanatory so im not going to say anything about them

settings are a pretty basic menu that can be opened by holding down BTN1 and scrolled up and down with BTN1/BTN2. press BTN1+BTN2 to choose/save. hold BTN2 to go back in menu and on dashboard to reset trip.

## ai slop excerpt regarding other features

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

## Physical case

<img width="1467" height="1697" alt="image" src="https://github.com/user-attachments/assets/8b37e315-7533-42be-bc11-a173442a433d" />

pretty simple design. 7mm inner standoffs holding up the board to leave space for wiring. M2x10 screws get there... almost perfectly)

has mounting points for handlebar mounts, basically 2 small pegs and a long m3 screw going through the mount and screwing on with a nut on the inside. it hold fine. the mounts i have for regular 31.8mm handlebars. the gap i left in them is pretty small but when printing from petg they slide onto the handlebars even at the thickest point alright. secured with a pair M3 screw+nut.

buttons are massacred mx style switches. i used outemu blues because i have a lot of them and they have large windows for rgb diodes which can be cut off to make the casing footprint only 10.5x14 as opposed to default 14x14 which uhhh saves some space. you can just modify the casing and the top plate a bit to fit them fully or redesign for some better buttons. keycaps on mine i also modeled, nothing much to say about them except for the fact that i printed them with a circular pattern because the stem was getting messed up with other infill patterns. the switches are then glued into the casing really at any depth you like, just make sure that there's enough space for keycaps.

everything uses M3 hardware except for the pcb standoffs. i dont know lengths but if you dont already have a big box of different M3 bolts you should definitely get one.

### Important thing to note is that im a dumbass and most of the holes are a bit too small. i just drilled them out to proper diameters with a drill.

also im not sure about the exact waterproofness of the case. i put silicone rubber around all of the holes and gaps but i have yet to test how they hold up. maybe you could just make some gasket from like tpu and that would be better. 

as already said i printed with petg, i think 0.20mm layer height, 100% infill ofc. had a hard time getting the top frame to stick to the PEI plate so i had to print it with a skirt.

i provided the models for the top frame, bottom casing and the mounting brackets in STL and STEP. also here's the [onshape link](https://cad.onshape.com/documents/30012e10ce4cb7aa290a9fdc/w/881d1dbb9680ce4c04a9ae5d/e/2c86da4f851fc89c557b4d4e?renderMode=0&uiState=6ac1740a641697b139c08db8) for easier further modification

### how it looks on the bike assembled
<img width="960" height="1280" alt="image" src="https://github.com/user-attachments/assets/9714daea-9ef4-49aa-8510-316476684713" />
<img width="960" height="1280" alt="image" src="https://github.com/user-attachments/assets/4fb0b34e-616f-43dc-884f-3dcda7928919" />

---

## Building and Flashing(had ai generate this one and the next section. it's either self explanatory basics and you dont need to read it or you will ask ai how to do it anyway)

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


# the end

  that's about it. shout out to mr antigravity for coding and mr ENGINEER jooj from discord for moral and technical support.

  if you have any improvements/bugs - issues and bugs on github are very welcome. i daily drive this thing myself and will try to continuously improve on it. 

  in general im pretty happy with how it turned out considering how little engineering experience i possess. i was very worried about the brightness of the display being lackluster but it proved to be enough even on sunny days. the esp is also surprisingly good at driving that display - the framerate and responsiveness are great! 

  total costs for this project should be only a few dozen euro - the display module can be had for 20eur shipped from aliexpress, and the rest are just basic DIY materials.

  ## known issues/TODO list: 
  - add ON/OFF functionality of some sorts?
  - fix 3d models to be less shit
  - work on the UI since element spacing and whatnot kinda suck
  - test WIFI and USB bridges further. had some issues with them that were allegedly fixed but not 100%
  - make some better pictures lmao with a clean camera lens 
  - add support for displaying regen braking(my bike isnt DD)
