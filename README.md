<div align="center">

# 🎛️ ESP32 20-Channel Industrial Control System

[![Platform](https://img.shields.io/badge/Platform-ESP32-blue.svg)](https://www.espressif.com/en/products/socs/esp32)
[![Framework](https://img.shields.io/badge/Framework-Arduino-00979D.svg)](https://www.arduino.cc/)
[![License](https://img.shields.io/badge/License-MIT-green.svg)](LICENSE)
[![PlatformIO](https://img.shields.io/badge/PlatformIO-Ready-orange.svg)](https://platformio.org/)
[![Status](https://img.shields.io/badge/Status-Production%20Ready-success.svg)]()

**Professional-grade relay control system with dual communication modes (MQTT/WebSocket), real-time web interface, and industrial reliability for 7-day continuous operation testing.**

[Features](#-features) • [Hardware](#-hardware-requirements) • [Installation](#-installation) • [Usage](#-usage) • [API](#-api-reference)

![Dashboard Preview](docs/images/dashboard-preview.png)

</div>

---

## 📑 Table of Contents

- [✨ Features](#-features)
- [🔧 Hardware Requirements](#-hardware-requirements)
- [📂 Project Structure](#-project-structure)
- [🚀 Installation](#-installation)
- [⚙️ Configuration](#️-configuration)
- [💻 Usage](#-usage)
- [📡 Communication Modes](#-communication-modes)
- [🔌 API Reference](#-api-reference)
- [🛠️ Serial Commands](#️-serial-commands)
- [🎯 Industrial Testing](#-industrial-testing)
- [🐛 Troubleshooting](#-troubleshooting)
- [🤝 Contributing](#-contributing)
- [📝 License](#-license)

---

## ✨ Features

### 🎛️ Core Features

- ✅ **20 Independent Channels** - Control via ESP32 GPIO + 2× PCF8574 I/O Expanders
- ✅ **Dual Communication Mode** - Switch between MQTT & WebSocket without restart
- ✅ **Real-time Web Dashboard** - Responsive UI with live status updates (500ms polling)
- ✅ **Auto Mode with Intervals** - Configurable ON/OFF timing per channel
- ✅ **Synchronized Groups** - Channels with same interval toggle together perfectly
- ✅ **LCD Status Display** - 16×2 I2C LCD for real-time monitoring
- ✅ **Secure Authentication** - Web login with customizable credentials
- ✅ **Persistent Configuration** - Settings saved to LittleFS filesystem
- ✅ **Serial Command Interface** - Full control via USB/UART
- ✅ **Hardware Validation Mode** - Built-in continuous testing for industrial reliability

### 🌟 Advanced Features

- 🔄 **Hot-swappable Modes** - Switch MQTT ↔ WebSocket on-the-fly via web/serial/remote
- ⚡ **Bulk Operations** - Set all 20 channels ON/OFF/Interval simultaneously
- 📊 **Real-time Status Polling** - Configurable 500ms-2s update intervals
- 🔐 **Token-based Auth** - Secure remote MQTT/WebSocket connections
- 📱 **Mobile Responsive** - Works perfectly on desktop, tablet, and smartphone
- 🎨 **Modern UI/UX** - Clean Material Design with smooth animations
- 🔧 **I2C Auto-detection** - Automatic device scanning and verification
- 💾 **Non-volatile Storage** - Configuration persists across power cycles
- 🌐 **AP Mode Fallback** - Creates WiFi hotspot if no network found
- 📈 **Production Ready** - Tested for 7-day continuous industrial operation

---

## 🔧 Hardware Requirements

### Required Components

| Component                | Specification      | Qty | Notes                          |
| ------------------------ | ------------------ | --- | ------------------------------ |
| **ESP32 Dev Board**      | ESP-WROOM-32       | 1   | 4MB+ flash recommended         |
| **PCF8574 I/O Expander** | 8-bit I2C          | 2   | Addresses: 0x20, 0x24          |
| **LCD Display**          | 16×2 I2C (HD44780) | 1   | Address: 0x27                  |
| **Relay Module**         | 5V/12V Optocoupler | 20  | According to load requirements |
| **Power Supply**         | 5V 5A minimum      | 1   | Higher current for more relays |
| **Pull-up Resistors**    | 4.7kΩ              | 2   | For I2C lines (SDA/SCL)        |

### Pin Configuration

**ESP32 Direct GPIO Channels:**

- **CH01** → GPIO 4
- **CH02** → GPIO 17
- **CH11** → GPIO 25
- **CH12** → GPIO 32

**PCF8574 #1 (0x20) - Channels 3-10:**

- P0-P7 → CH03 to CH10 (inverted logic)

**PCF8574 #2 (0x24) - Channels 13-20:**

- P0-P7 → CH13 to CH20 (inverted logic)

**I2C Bus:**

- SDA → GPIO 21 (4.7kΩ pull-up to VCC)
- SCL → GPIO 22 (4.7kΩ pull-up to VCC)

> 📘 **Detailed wiring guide:** [docs/WIRING.md](docs/WIRING.md)

---

## 📂 Project Structure

```
ESP32-20Channel-Control/
│
├── 📁 src/
│ └── main.cpp # Main firmware (2000+ lines)
│
├── 📁 data/ # LittleFS web files
│ ├── index.html # Dashboard UI
│ ├── login.html # Authentication page
│ ├── config.html # Configuration interface
│ ├── script.js # Frontend logic (500+ lines)
│ └── style.css # Responsive styling
│
├── 📁 docs/ # Documentation
│ ├── WIRING.md # Hardware wiring guide
│ ├── API.md # API documentation
│ └── images/ # Screenshots & diagrams
│
├── platformio.ini # Build configuration
├── README.md # This file
├── LICENSE # MIT License
├── CONTRIBUTING.md # Contribution guidelines
└── .gitignore # Git ignore rules
```

---

## 🚀 Installation

### Method 1: PlatformIO (Recommended)

```bash
# 1. Clone repository
git clone https://github.com/yourusername/ESP32-20Channel-Control.git
cd ESP32-20Channel-Control

# 2. Open in PlatformIO (VSCode)
code .

# 3. Build & upload firmware
pio run --target upload

# 4. Upload web files to LittleFS
pio run --target uploadfs

# 5. Open serial monitor
pio device monitor -b 115200

```

## Method 2: Arduino IDE

###Install Required Libraries:

Open Arduino IDE → Library Manager
Install these libraries:
ArduinoJson v6.21.3
PubSubClient v2.8
PCF8574 library v2.3.4
LiquidCrystal_I2C v1.1.4
WebSockets v2.4.1

### Board Configuration:

Board: ESP32 Dev Module
Upload Speed: 921600
Flash Size: 4MB (LittleFS partition)
Partition Scheme: Default 4MB with spiffs

### Upload Steps:

Open src/main.cpp in Arduino IDE
Select board and port
Click Upload
Install ESP32 Sketch Data Upload
Upload data/ folder to LittleFS

## ⚙️ Configuration

###First Boot Setup

1. Power on ESP32

   - LCD displays: ESP32 Control v3.1 Booting...
   - Creates WiFi AP: ESP32-Control

2. Connect to WiFi AP

   - SSID: ESP32-Control
   - Password: 12345678
   - Device IP: 192.168.4.1

3. Login to Web Interface

   - URL: http://192.168.4.1
   - Default Username: admin
   - Default Password: admin123

4. Configure System

   - Navigate to Config page
   - Enter WiFi credentials
   - Choose communication mode
   - Configure MQTT/WebSocket server
   - Save & Restart

### Configuration Parameters

### WiFi Settings

- SSID: Your network name
- Password: WiFi password

### Communication Mode Selection

Option 1: MQTT Mode

- Best for: Home Assistant, Node-RED, MQTT brokers
- Server IP: Broker address (e.g., 192.168.1.100)
- Port: 1883 (standard) or custom
- Topic: MQTT topic path (e.g., esp32/outputs)
- Token: MQTT password (optional)
  Option 2: WebSocket Mode (Default)

- Best for: Real-time web apps, custom dashboards
- Server IP: WebSocket server address
- Port: 80, 443, or custom
- Path: WebSocket endpoint (e.g., /ws)
- Token: Authentication token (optional)
  Web Authentication
- Username: Admin username
- Password: Admin password
- Note: Leave password field empty to keep current password

## 💻 Usage

###Web Dashboard

```
Dashboard Interface
```

### Main Features:

Status Bar:

- WiFi connection indicator
- Remote server connection status
- Current communication mode badge
- Switch mode button
  Control Actions:

- ⚡ All ON - Turn all 20 channels ON simultaneously
- ⏸ All OFF - Turn all 20 channels OFF simultaneously
- ⚙️ Set All Interval - Bulk configure intervals for all channels
  Channel Cards:
  Each card displays:

- Channel name (editable)
- Current state (ON/OFF badge)
- Auto mode indicator
- Interval ON time (seconds)
- Interval OFF time (seconds)
- Toggle button
- Settings button (⚙️)
  Editing Channels
  Click ⚙️ on any channel to configure:

- Name: Custom label (e.g., "Pump Motor 1")
- Auto Mode: Enable automatic interval-based toggling
- Interval ON: Duration to stay ON (seconds)
- Interval OFF: Duration to stay OFF (seconds)

## 📡 Communication Modes

### MQTT Mode

Publishing:

- Auto-publishes status every 5 seconds
- JSON format with all 20 channel states
  Subscribing:

- Listens on `{topic}/control`
- Accepts JSON commands
  Example MQTT Command:

```
# Using mosquitto_pub
mosquitto_pub -h 192.168.1.100 -t "esp32/outputs/control" -m '{
  "action": "setState",
  "channel": 5,
  "state": true
}'
```

### WebSocket Mode

Connection:

```
const ws = new WebSocket('ws://192.168.1.100:80/ws');

ws.onopen = () => {
  console.log('Connected to ESP32');
};

ws.onmessage = (event) => {
  const data = JSON.parse(event.data);
  console.log('Status:', data.outputs);
};

// Send command
ws.send(JSON.stringify({
  action: 'setState',
  channel: 5,
  state: true
}));
```

# Contributing to ESP32 20-Channel Control

First off, thank you for considering contributing to this project! 🎉

## 📋 Table of Contents

- [Code of Conduct](#code-of-conduct)
- [How Can I Contribute?](#how-can-i-contribute)
- [Development Setup](#development-setup)
- [Coding Standards](#coding-standards)
- [Commit Guidelines](#commit-guidelines)
- [Pull Request Process](#pull-request-process)

---

## Code of Conduct

This project and everyone participating in it is governed by our Code of Conduct. By participating, you are expected to uphold this code.

**Be respectful, be professional, be constructive.**

---

## How Can I Contribute?

### 🐛 Reporting Bugs

Before creating bug reports, please check the existing issues to avoid duplicates.

**Great bug reports include:**

- Clear, descriptive title
- Exact steps to reproduce
- Expected vs actual behavior
- Screenshots (if applicable)
- Hardware setup details
- Serial monitor output
- Software versions (ESP32 core, libraries)

**Example:**

### 💡 Suggesting Enhancements

Enhancement suggestions are tracked as GitHub issues.

**Include:**

- Clear use case
- Expected benefits
- Potential implementation approach
- Mockups or examples (if UI-related)

### 🔧 Code Contributions

**Areas needing help:**

- [ ] OTA (Over-The-Air) update support
- [ ] Home Assistant MQTT auto-discovery
- [ ] Mobile app (Flutter/React Native)
- [ ] Cloud integration (AWS IoT, Azure IoT)
- [ ] Energy monitoring features
- [ ] Scheduler/calendar functionality
- [ ] Data logging to SD card
- [ ] GraphQL API support

---

## Development Setup

### Prerequisites

- [PlatformIO](https://platformio.org/)
- [Git](https://git-scm.com/)
- ESP32 hardware for testing

### Setup Steps

```bash
# 1. Fork and clone
git clone https://github.com/YOUR_USERNAME/ESP32-20Channel-Control.git
cd ESP32-20Channel-Control

# 2. Create branch
git checkout -b feature/my-feature

# 3. Install dependencies
pio lib install

# 4. Build
pio run

# 5. Upload and test
pio run --target upload
pio run --target uploadfs
pio device monitor
```
