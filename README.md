# ⏰ Alarm Clock

A WiFi-enabled alarm clock built with Arduino Uno R4 WiFi, featuring automatic time sync, weather display, and reliable alarm functionality.

## Features

- **Automatic Time Sync**: NTP-based time synchronization with multiple fallback servers
- **Weather Display**: Real-time weather information with automatic location detection
- **Reliable Alarm**: Non-blocking alarm system with visual and audio feedback
- **Smart Display**: OLED display with automatic brightness control and power management
- **Network Diagnostics**: Built-in network monitoring and troubleshooting tools
- **EEPROM Storage**: Persistent settings and alarm configuration
- **LED Matrix Support**: Optional LED matrix for enhanced visual alarm (Uno R4 WiFi)

## Hardware

- Arduino Uno R4 WiFi
- SSD1306 OLED Display (128x32)
- Two push buttons for control
- One Piezo Speaker for the Alarm
- Optional LED Matrix (Uno R4 WiFi)

## Setup

1. Configure WiFi credentials via serial console
2. API keys are stored in `src/env.h` (see `API_KEYS_SETUP.md`)
3. Upload firmware and monitor serial output for setup instructions

## Motivation

Created to address inconsistent phone alarm reliability, providing a dedicated, reliable alarm solution with additional useful features like weather display and automatic time synchronization.

## License

Open source - feel free to modify and improve. 