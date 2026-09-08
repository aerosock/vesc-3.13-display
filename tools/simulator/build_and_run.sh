#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"

cd "$ROOT_DIR"

echo "=== Compiling VESC Display Simulator ==="
g++ -std=c++17 -O2 -DLGFX_LINUX_FB -DNATIVE_SIMULATOR \
  -Itools/simulator/include \
  -Iinclude \
  -I.pio/libdeps/waveshare-esp32-s3-lcd-316/LovyanGFX/src \
  src/dash_renderer.cpp \
  src/settings_manager.cpp \
  src/battery_manager.cpp \
  src/buttons.cpp \
  tools/simulator/sim_main.cpp \
  .pio/libdeps/waveshare-esp32-s3-lcd-316/LovyanGFX/src/lgfx/v1/LGFXBase.cpp \
  .pio/libdeps/waveshare-esp32-s3-lcd-316/LovyanGFX/src/lgfx/v1/LGFX_Sprite.cpp \
  .pio/libdeps/waveshare-esp32-s3-lcd-316/LovyanGFX/src/lgfx/v1/lgfx_fonts.cpp \
  .pio/libdeps/waveshare-esp32-s3-lcd-316/LovyanGFX/src/lgfx/Fonts/IPA/lgfx_font_japan.c \
  .pio/libdeps/waveshare-esp32-s3-lcd-316/LovyanGFX/src/lgfx/Fonts/efont/lgfx_efont_cn.c \
  .pio/libdeps/waveshare-esp32-s3-lcd-316/LovyanGFX/src/lgfx/Fonts/efont/lgfx_efont_ja.c \
  .pio/libdeps/waveshare-esp32-s3-lcd-316/LovyanGFX/src/lgfx/Fonts/efont/lgfx_efont_kr.c \
  .pio/libdeps/waveshare-esp32-s3-lcd-316/LovyanGFX/src/lgfx/Fonts/efont/lgfx_efont_tw.c \
  .pio/libdeps/waveshare-esp32-s3-lcd-316/LovyanGFX/src/lgfx/utility/lgfx_miniz.c \
  .pio/libdeps/waveshare-esp32-s3-lcd-316/LovyanGFX/src/lgfx/utility/lgfx_pngle.c \
  .pio/libdeps/waveshare-esp32-s3-lcd-316/LovyanGFX/src/lgfx/utility/lgfx_qoi.c \
  .pio/libdeps/waveshare-esp32-s3-lcd-316/LovyanGFX/src/lgfx/utility/lgfx_qrcode.c \
  .pio/libdeps/waveshare-esp32-s3-lcd-316/LovyanGFX/src/lgfx/utility/lgfx_tjpgd.c \
  .pio/libdeps/waveshare-esp32-s3-lcd-316/LovyanGFX/src/lgfx/v1/misc/common_function.cpp \
  .pio/libdeps/waveshare-esp32-s3-lcd-316/LovyanGFX/src/lgfx/v1/misc/pixelcopy.cpp \
  .pio/libdeps/waveshare-esp32-s3-lcd-316/LovyanGFX/src/lgfx/v1/misc/SpriteBuffer.cpp \
  .pio/libdeps/waveshare-esp32-s3-lcd-316/LovyanGFX/src/lgfx/v1/panel/Panel_Device.cpp \
  .pio/libdeps/waveshare-esp32-s3-lcd-316/LovyanGFX/src/lgfx/v1/platforms/framebuffer/common.cpp \
  -o tools/simulator/simulator

echo "=== Running Scenarios ==="
./tools/simulator/simulator

echo "=== Converting to PNG ==="
python3 -c '
import glob
from PIL import Image
for f in glob.glob("tools/simulator/screenshots/*.bmp"):
    im = Image.open(f)
    im.save(f.replace(".bmp", ".png"))
'
echo "=== Complete! PNGs available in tools/simulator/screenshots/ ==="
