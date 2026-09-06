#!/usr/bin/env bash
# Setup script for Fedora 42: ESP32-S3 Flashing Environment
set -e

echo "=== Setting up ESP32-S3 Development Environment on Fedora 42 ==="

# 1. Add current user to dialout group for serial port access
CURRENT_USER=$(whoami)
echo "[1/4] Ensuring $CURRENT_USER is in dialout group..."
if groups "$CURRENT_USER" | grep &>/dev/null '\bdialout\b'; then
    echo "      Already in dialout group."
else
    echo "      Adding $CURRENT_USER to dialout group (requires sudo)..."
    sudo usermod -aG dialout "$CURRENT_USER"
    echo "      NOTE: You may need to run 'newgrp dialout' or log out and back in for group changes to take effect."
fi

# 2. Install udev rules for Espressif / USB CDC devices
echo "[2/4] Installing ESP32 udev rules..."
sudo bash -c 'cat << "EOF" > /etc/udev/rules.d/99-esp32.rules
# Espressif USB JTAG/serial debug unit
ATTRS{idVendor}=="303a", ATTRS{idProduct}=="1001", MODE="0666", GROUP="dialout"
# Espressif USB-CDC
ATTRS{idVendor}=="303a", ATTRS{idProduct}=="1002", MODE="0666", GROUP="dialout"
# Generic CP210x, CH340, FTDI
ATTRS{idVendor}=="10c4", ATTRS{idProduct}=="ea60", MODE="0666", GROUP="dialout"
ATTRS{idVendor}=="1a86", ATTRS{idProduct}=="7523", MODE="0666", GROUP="dialout"
ATTRS{idVendor}=="0403", ATTRS{idProduct}=="6001", MODE="0666", GROUP="dialout"
EOF'
sudo udevadm control --reload-rules
sudo udevadm trigger
echo "      udev rules updated successfully."

# 3. Check / Install PlatformIO
echo "[3/4] Checking PlatformIO CLI..."
if command -v pio &> /dev/null; then
    echo "      PlatformIO is already installed: $(pio --version)"
else
    echo "      PlatformIO not found. Installing via uv / pip..."
    if command -v uv &> /dev/null; then
        uv tool install platformio
        export PATH="$HOME/.local/bin:$PATH"
    elif command -v pip &> /dev/null; then
        pip install --user platformio
        export PATH="$HOME/.local/bin:$PATH"
    else
        echo "      Please install python3-pip or pipx: sudo dnf install -y python3-pip"
        exit 1
    fi
fi

# 4. Verification
echo "[4/4] Verifying USB devices..."
echo "      Connected serial ports:"
ls -l /dev/ttyACM* /dev/ttyUSB* 2>/dev/null || echo "      (No ESP32 connected yet - plug in the USB-C cable!)"

echo ""
echo "=== Setup Complete! ==="
echo "To compile and flash the dashboard, run:"
echo "  pio run -t upload"
echo "To open the serial telemetry monitor:"
echo "  pio run -t monitor"
