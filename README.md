# ESP32_S3_SLCAN_BRIDGE

Standalone ESP-IDF firmware that bridges USB Serial/JTAG to a classical CAN bus using the LAWICEL/SLCAN protocol. Works with SavvyCAN and any other SLCAN-compatible host software.

```
Host (SavvyCAN / terminal)
        │
   USB Serial/JTAG
        │
   serial_transport
        │
   slcan_protocol
        │
  bridge_application
        │
   can_bus_service
        │
   ESP-IDF TWAI driver
        │
   External CAN transceiver (hardware-wired)
        │
      CAN bus
```

---

## Project status

- Usable standalone SLCAN bridge
- Tested with ESP-IDF v5.5.3
- Target: ESP32-S3
- Classical CAN only (no CAN FD)

---

## Quick start

```bash
git clone https://github.com/your-org/ESP32_S3_SLCAN_BRIDGE
cd ESP32_S3_SLCAN_BRIDGE

# Edit GPIO pins and other settings if needed:
# main/slcan_bridge_project_configuration.h

idf.py set-target esp32s3
idf.py build
idf.py flash
```

Then open SavvyCAN, add a LAWICEL connection on the correct serial port, set speed to 500000, enable Listen Only and Enable Bus, and connect.

---

## What it does

- **Normal mode** — full bidirectional CAN (transmit and receive)
- **Listen-only mode** — receive only, no bus influence (uses TWAI hardware listen-only)
- **Bitrate switching** via SLCAN `Sx` commands, no reflash required
- **Mode switching** between normal and listen-only on the fly
- **Standard and extended frames**, data and remote frames
- **16-bit timestamps** via `Z0` / `Z1`
- **SLCAN status flags** (`F`) with read-and-clear semantics
- **SavvyCAN compatible** — includes `X0`/`X1` auto-poll stub

## What it does not do

- CAN FD
- ISO-TP / UDS / OBD-II
- DBC decoding
- Acceptance filter configuration (`M`/`m`)
- 10 kbit/s (`S0`) or custom bit timing (`sxxyy`)
- Programmatic transceiver mode control

---

## Hardware requirements

### ESP32-S3

Any ESP32-S3 board with USB Serial/JTAG. No external USB-UART adapter needed.

### CAN transceiver

The firmware is **transceiver-agnostic**. It uses only the ESP32-S3 TWAI controller and two configurable GPIO signals (CAN RX and CAN TX). It does not control transceiver mode pins (STB / S / RS / EN).

Any external classical CAN transceiver is supported provided:

1. **Correct wiring**: transceiver RXD → CAN RX GPIO, transceiver TXD → CAN TX GPIO.
2. **Hardware-fixed active mode**: the transceiver mode pin must be held in normal/high-speed mode by hardware — hard-wired to GND or via an external circuit. The firmware does not touch this pin.

### Wiring

| Signal | Direction | Description |
|---|---|---|
| Transceiver RXD | → ESP32-S3 | CAN RX GPIO (see config) |
| Transceiver TXD | ← ESP32-S3 | CAN TX GPIO (see config) |
| Transceiver VCC | — | Per transceiver datasheet |
| Transceiver GND | — | Common ground with CAN bus |
| CANH | — | CAN bus high |
| CANL | — | CAN bus low |

> If connecting to a CAN bus that already has termination resistors, do not add another 120 Ω terminator. Ensure common ground between the ESP32-S3 and the bus.

---

## Configuration

All user-configurable parameters are in one file — no menuconfig needed:

```
main/slcan_bridge_project_configuration.h
```

Key parameters:

| Parameter | Default | Description |
|---|---|---|
| `SLCAN_BRIDGE_CAN_RECEIVE_GPIO` | 6 | ESP32-S3 GPIO ← transceiver RXD |
| `SLCAN_BRIDGE_CAN_TRANSMIT_GPIO` | 5 | ESP32-S3 GPIO → transceiver TXD |
| `SLCAN_BRIDGE_DEFAULT_SPEED_CODE` | `'6'` | Default bitrate (500 kbit/s) |
| `SLCAN_BRIDGE_TWAI_RECEIVE_QUEUE_DEPTH` | 128 | TWAI RX queue depth (frames) |
| `SLCAN_BRIDGE_TWAI_TRANSMIT_QUEUE_DEPTH` | 32 | TWAI TX queue depth (frames) |
| `SLCAN_BRIDGE_LAWICEL_FIRMWARE_VERSION` | `"0100"` | Returned by `V` command |
| `SLCAN_BRIDGE_LAWICEL_SERIAL_NUMBER` | `"0001"` | Returned by `N` command |

ESP-IDF system settings (errata fixes, flash size) are in `sdkconfig.defaults`.

---

## SLCAN command reference

| Command | Description |
|---|---|
| `O` | Open channel, normal mode |
| `L` | Open channel, listen-only mode |
| `C` | Close channel |
| `Sx` | Set bitrate: S2=50k S3=100k S4=125k S5=250k S6=500k S7=800k S8=1M |
| `F` | Read status flags (read-and-clear; returns error if channel closed) |
| `V` | Firmware version |
| `N` | Serial number |
| `Z0` / `Z1` | Timestamps off / on |
| `X0` / `X1` | Auto-poll off / on (compatibility stub) |
| `tIIILDD...` | Transmit standard data frame |
| `TIIIIIIIILDD...` | Transmit extended data frame |
| `rIIIL` | Transmit standard remote frame |
| `RIIIIIIIIL` | Transmit extended remote frame |

---

## Limitations

- Classical CAN only — ESP32-S3 does not support CAN FD
- ROM/bootloader output may appear on the serial port before the application starts
- Listen-only mode uses the TWAI hardware controller, not transceiver mode control
- Requires ESP-IDF v5.x toolchain; not compatible with Arduino core
