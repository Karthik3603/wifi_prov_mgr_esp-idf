# ESP32 Wi-Fi Provisioning Manager with Button Trigger

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)

A robust Wi-Fi provisioning solution for ESP32 devices that combines BLE-based provisioning with physical button control for credential reset.

## 📌 Features

- **QR Code Provisioning**: Onboard new devices in 30 seconds with scannable QR codes
- **One-Button Reset**: GPIO32 button forces reprovisioning cycle
- **BLE Secure Transport**: ESP-BLE provisioning with Proof-of-Possession (PoP) security
- **Automatic Reconnection**: Persistent storage of credentials in NVS
- **Debounced Input**: Clean button press detection (50ms debounce)

## 🛠 Hardware Setup

### Components Required
| Component | Quantity |
|-----------|----------|
| ESP32 Dev Board | 1 |
| Tactile Button | 1 |
| 10KΩ Resistor | 1 |
| Breadboard | 1 |

### Wiring Diagram

> ESP32 GPIO32 ───┬───▶ Button ▶ 3.3V
  >               │
    >             └─── 10KΩ Pulldown ▶ GND

Find the code in 'main'-->'app_main.c'.

find the video of the expected outcome in 'working_video.mp4'.
