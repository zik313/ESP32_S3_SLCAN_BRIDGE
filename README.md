# ESP32_S3_SLCAN_BRIDGE

[![Build](https://github.com/zik313/ESP32_S3_SLCAN_BRIDGE/actions/workflows/build.yml/badge.svg)](https://github.com/zik313/ESP32_S3_SLCAN_BRIDGE/actions/workflows/build.yml)

Standalone ESP-IDF firmware for ESP32-S3 that bridges USB Serial/JTAG to a classical CAN bus using the LAWICEL/SLCAN protocol. Works with SavvyCAN and any SLCAN-compatible host software.

**Target:** ESP32-S3 · **Toolchain:** ESP-IDF v5.5.3 · **Protocol:** Classical CAN only

---

## Features

- Normal mode (bidirectional) and listen-only mode, switchable at runtime
- Standard and extended frames, data and remote frames
- Bitrate selection via `Sx` commands — no reflash required
- 16-bit timestamps (`Z0` / `Z1`)
- Status flags (`F`) with read-and-clear semantics
- SavvyCAN compatible — `X0`/`X1` auto-poll stub included
- Transceiver-agnostic — firmware controls only TWAI RX/TX pins
- Single configuration file, no menuconfig needed

## Non-goals

This project intentionally does not include:

- CAN FD
- ISO-TP, UDS, OBD-II
- DBC decoding
- Acceptance filter configuration (`M`/`m`)
- 10 kbit/s (`S0`) or custom bit timing
- Programmatic transceiver mode control (STB / S / RS / EN)

---

## Hardware

### ESP32-S3

Any ESP32-S3 board with USB Serial/JTAG. No external USB-UART adapter needed.

### CAN transceiver

The firmware is **transceiver-agnostic**. It drives only two GPIO lines — CAN RX and CAN TX. It does not control transceiver mode pins.

Requirements:
1. Transceiver RXD → CAN RX GPIO, transceiver TXD → CAN TX GPIO
2. Transceiver mode pin (STB / S / RS / EN) must be held in active/normal mode **by hardware** — the firmware does not touch it

### Wiring

```
ESP32-S3 GPIO (RX)  ←  Transceiver RXD
ESP32-S3 GPIO (TX)  →  Transceiver TXD
Transceiver VCC         per datasheet
Transceiver GND         common ground with CAN bus
Transceiver CANH    ↔  CAN bus CANH
Transceiver CANL    ↔  CAN bus CANL
```

> Do not add a 120 Ω terminator if the CAN bus already has termination. Ensure common ground between the ESP32-S3 and the bus.

---

## Configuration

All user-configurable parameters live in one file:

```
main/slcan_bridge_project_configuration.h
```

| Parameter | Default | Description |
|---|---|---|
| `SLCAN_BRIDGE_CAN_RECEIVE_GPIO` | 6 | GPIO ← transceiver RXD |
| `SLCAN_BRIDGE_CAN_TRANSMIT_GPIO` | 5 | GPIO → transceiver TXD |
| `SLCAN_BRIDGE_DEFAULT_SPEED_CODE` | `'6'` | Startup bitrate (500 kbit/s) |
| `SLCAN_BRIDGE_TWAI_RECEIVE_QUEUE_DEPTH` | 128 | RX queue depth (frames) |
| `SLCAN_BRIDGE_TWAI_TRANSMIT_QUEUE_DEPTH` | 32 | TX queue depth (frames) |
| `SLCAN_BRIDGE_LAWICEL_FIRMWARE_VERSION` | `"0100"` | Returned by `V` |
| `SLCAN_BRIDGE_LAWICEL_SERIAL_NUMBER` | `"0001"` | Returned by `N` |

ESP-IDF system settings (errata, flash size) are in `sdkconfig.defaults`.

---

## Build and flash

Requires [ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/) installed.

```bash
git clone https://github.com/zik313/ESP32_S3_SLCAN_BRIDGE
cd ESP32_S3_SLCAN_BRIDGE

# Adjust GPIO pins and settings in:
# main/slcan_bridge_project_configuration.h

idf.py set-target esp32s3
idf.py build
idf.py flash
```

---

## Quick start with SavvyCAN

1. Connect the ESP32-S3 via USB
2. Open SavvyCAN → **Connection → Add New Device Connection**
3. Type: **LAWICEL**, select the correct serial port
4. Speed: `500000`, enable **Listen Only** and **Enable Bus**
5. Click **Connect**

Frames will appear as soon as the bus has traffic. Use `O` instead of `L` for normal (transmit-capable) mode.

---

## SLCAN command reference

| Command | Description |
|---|---|
| `O` | Open — normal mode |
| `L` | Open — listen-only mode |
| `C` | Close channel |
| `Sx` | Set bitrate: `S2`=50k `S3`=100k `S4`=125k `S5`=250k `S6`=500k `S7`=800k `S8`=1M |
| `F` | Read status flags (read-and-clear; error if channel closed) |
| `V` | Firmware version |
| `N` | Serial number |
| `Z0` / `Z1` | Timestamps off / on |
| `X0` / `X1` | Auto-poll off / on (compatibility stub) |
| `tIIILDD…` | Standard data frame |
| `TIIIIIIIILDD…` | Extended data frame |
| `rIIIL` | Standard remote frame |
| `RIIIIIIIIL` | Extended remote frame |

---

## Limitations

- Classical CAN only — ESP32-S3 does not support CAN FD
- ROM/bootloader output may appear on the serial port before the application starts
- Listen-only mode is implemented via the TWAI hardware controller, not by controlling the transceiver mode pin
- Requires ESP-IDF v5.x; not compatible with Arduino core
