# PIM670 Zabbix Display

Real-time Zabbix alert visualizer for the **Pimoroni Cosmic Unicorn** (32×32 RGB LED matrix on a Raspberry Pi Pico W). Fetches alerts via a custom CSV HTTP API and renders them as colored blocks on the LED matrix over an animated "eighties supercomputer" background.

## Project layout

```
pim670-zabbix-display/   ← main source
  main.cpp               ← display loop, state machine, animation
  opt/config.c           ← USB-based config.txt reader
  opt/httpclient.c       ← async HTTP(S) client (TLS 1.2)
  usbfs/                 ← USB mass storage (FatFS)
  zabbix/                ← alert parsing (CSV + tiny-json)
```

## Building

Prerequisites (need sudo):
```bash
sudo apt install cmake gcc-arm-none-eabi
```

Clone libraries (no root needed, next to the project):
```bash
cd /home/claude/projects/zabbix-display
mkdir lib && cd lib
git clone --recursive https://github.com/raspberrypi/pico-sdk.git
git clone --recursive https://github.com/pimoroni/pimoroni-pico.git
```

Build:
```bash
cd pim670-zabbix-display
mkdir -p build && cd build
cmake -DPICO_SDK_PATH=/home/claude/projects/zabbix-display/lib/pico-sdk \
      -DPIMORONI_PICO_PATH=/home/claude/projects/zabbix-display/lib/pimoroni-pico ..
make -j$(nproc)
```

Output: `build/pim670-zabbix-display.uf2` — copy to `RPI-RP2` USB mount.

## API response format

The device polls `ZABBIX_API?a=v0.1/triggers` and expects a CSV response. See `example-triggers.csv` for a real example. Format:

```
clock;severity;suppressed;hostid;host;name
1776412802;5;0;12035;;
1774451786;5;1;12623;;
```

- `clock` — Unix timestamp of the trigger
- `severity` — Zabbix severity (5 = disaster)
- `suppressed` — `1` = suppressed (shown white/gray), `0` = active (shown red)
- `hostid` — numeric host ID
- `host` / `name` — may be empty (not used by the display firmware)

First row is a header and skipped. Up to 225 rows are processed (fits a 15×15 grid).

## Display logic (main.cpp)

- **Background**: animated lime-green "rain" pixels using per-pixel `age`/`lifetime` arrays
- **Alert blocks**: dynamically sized red rectangles (suppressed = white/gray); now animated with same age/lifetime rhythm
- **Colors**: red = active alert, white/gray = suppressed, dimmed = no recent data (>30 s)
- **Brightness**: hardware +/− buttons on device
