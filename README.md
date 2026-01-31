# 🏒 Hockey Panel

ESP32-based hockey statistics display for SHL and HockeyAllsvenskan with touch interface and MQTT remote control.

## 🖥️ Hardware

- **Device:** ESP32-2432S028 "Cheap Yellow Display" (CYD)
- **Display:** 2.8" IPS LCD 320×240 pixels
- **Touch:** XPT2046 resistive touchscreen
- **CPU:** ESP32-D0WD-V3 Dual-core 240MHz
- **RAM:** 520KB + 4MB PSRAM
- **Flash:** 4MB
- **WiFi:** 802.11 b/g/n
- **MAC Address:** 20:e7:c8:ba:78:94

## 🏗️ Architecture

```
┌─────────────────┐     ┌──────────────┐     ┌─────────────────┐
│  SHL.se /       │ ──► │   Backend    │ ──► │   ESP32 CYD     │
│  HockeyAllsv.   │     │  (DevPi)     │     │  Touch Display  │
└─────────────────┘     └──────────────┘     └─────────────────┘
      Web Scraping         Port 3080           WiFi JSON + MQTT
                              │
                              ▼
                         ┌──────────────┐
                         │ MQTT Broker  │ ◄─── Remote Control
                         │ Port 1883    │
                         └──────────────┘
```

## 📱 Display Interface

| Mode | Description |
|------|-------------|
| **SHL** | Svenska Hockey Ligan standings (14 teams) |
| **Allsvenskan** | HockeyAllsvenskan standings (14 teams) |
| **Division 3** | Local division standings |
| **News** | Latest hockey news |
| **Team Details** | Touch any team for detailed stats |

### Touch Controls
- **Tap team row** → Show detailed statistics
- **Swipe left/right** → Navigate between leagues
- **Long press** → Calibration mode

## 📡 MQTT Remote Control

### Topics
| Topic | Purpose | Example |
|-------|---------|---------|
| `hockey/panel/status` | Device telemetry | `{"firmware":"v1.20.1","uptime":12345}` |
| `hockey/panel/command` | Remote commands | `refresh`, `reboot`, `calibrate` |
| `hockey/panel/data` | Hockey data updates | Live match scores |

### Commands
```bash
# Refresh hockey data
mosquitto_pub -h 192.168.1.224 -t "hockey/panel/command" -m "refresh"

# Reboot device  
mosquitto_pub -h 192.168.1.224 -t "hockey/panel/command" -m "reboot"

# Start touch calibration
mosquitto_pub -h 192.168.1.224 -t "hockey/panel/command" -m "calibrate"
```

### Status Monitoring
```bash
# Listen to device status
mosquitto_sub -h 192.168.1.224 -t "hockey/panel/status"
```

## ⚙️ Firmware Versions

| Version | Features | Status |
|---------|----------|---------|
| `v1.19.1` | Swedish UTF-8 support, stable touch | ✅ Stable |
| `v1.20.0` | MQTT + Enhanced OTA | ⚠️  HTTP timeouts |
| `v1.20.1` | Non-blocking MQTT + HTTP fixes | ✅ **Current** |

### Current Firmware: v1.20.1-mqtt-http-fix

**New Features:**
- ✅ **MQTT Integration** - Real-time remote control
- ✅ **Enhanced OTA** - Visual progress + error handling  
- ✅ **Non-blocking networking** - Prevents HTTP timeouts
- ✅ **Status heartbeats** - Device telemetry every 60 seconds
- ✅ **Swedish character support** - Perfect ÅÄÖ rendering

**Memory Usage:**
- **RAM:** 18.2% (59,600 bytes / 327,680 bytes)
- **Flash:** 92.5% (1,212,881 bytes / 1,310,720 bytes)

## 🔧 Backend Services

### Production Backend (DevPi)

```bash
# Status check
systemctl status hockey-panel

# View logs
journalctl -u hockey-panel -f

# Restart service
sudo systemctl restart hockey-panel
```

### Emergency Mock Backend

When main backend hangs (SHL scraping issues), use emergency mock:

```bash
cd backend
node mock-api.js &
```

**Mock Data Includes:**
- **SHL:** Frölunda HC, Skellefteå AIK, Luleå Hockey
- **Allsvenskan:** BIK Karlskoga, Västerås IK
- **Instant response** - No web scraping delays

## 🔌 Installation

### USB Flashing (First Time)

```bash
cd firmware

# Build firmware
python3 -m platformio run

# Flash via USB (ESP32 connected)
python3 -m platformio run -t upload

# Monitor serial output
python3 -m platformio device monitor
```

### OTA Updates (Wireless)

```bash
# Update via network (after initial USB flash)
python3 -m platformio run -e esp32-cyd-ota -t upload

# OTA credentials
# Hostname: HockeyPanel  
# Password: hockey2026
```

**OTA shows visual progress bar on display during update.**

## 📊 API Endpoints

| Endpoint | Response | Status |
|----------|----------|---------|
| `GET /api/status` | `{"ok":true,"pollInterval":300}` | ✅ Working |
| `GET /api/shl` | SHL teams + matches | ⚠️ Scraping hangs |
| `GET /api/allsvenskan` | HA teams + matches | ⚠️ Scraping hangs |
| `GET /api/all` | Combined data | ⚠️ Scraping hangs |

### Network Configuration

**Production:**
- **ESP32:** 192.168.1.185
- **Backend:** 192.168.1.224:3080  
- **MQTT:** 192.168.1.224:1883
- **WiFi:** "IoT" network

## 🛠️ Development

### Build System
- **Platform:** PlatformIO + ESP32 Arduino Framework
- **Graphics:** LovyanGFX (faster than standard libraries)
- **Font:** lgfxJapanGothic_12 (Swedish character support)
- **Touch:** XPT2046 resistive with calibration
- **MQTT:** PubSubClient v2.8.0

### Key Dependencies
```ini
lib_deps = 
    bblanchon/ArduinoJson@^7.0.0
    lovyan03/LovyanGFX@^1.1.16
    knolleary/PubSubClient@^2.8
```

### File Structure
```
firmware/
├── src/
│   ├── main.cpp              # Main application + MQTT + OTA
│   └── display_config.hpp    # LovyanGFX hardware config  
├── platformio.ini           # Build configuration
└── .pio/                    # Build artifacts (ignored in git)

backend/
├── src/                     # TypeScript backend (original)
├── mock-api.js             # Emergency JavaScript mock server
├── backend.log             # Runtime logs
└── package.json
```

## 🚨 Troubleshooting

### Display Issues
```bash
# Check device connection
ls -la /dev/ttyUSB0

# Monitor boot process
timeout 10s cat /dev/ttyUSB0

# Hard reset
# Press physical reset button on ESP32
```

### Network Problems  
```bash
# Test ESP32 connectivity
ping 192.168.1.185

# Test backend API
curl -m 5 http://192.168.1.224:3080/api/status

# Test MQTT broker  
mosquitto_pub -h 192.168.1.224 -t "test" -m "hello"
```

### Backend Hanging
```bash
# Kill stuck backend
pkill -f "node.*server.js"

# Start emergency mock
cd backend && node mock-api.js &

# The ESP32 will automatically use the mock API
```

### MQTT Issues
```bash
# Check MQTT broker status
nmap -p 1883 192.168.1.224

# Monitor device status
mosquitto_sub -h 192.168.1.224 -t "hockey/panel/status"

# Send test command
mosquitto_pub -h 192.168.1.224 -t "hockey/panel/command" -m "refresh"
```

## 📈 Performance Metrics

**Current System (v1.20.1):**
- **Boot time:** ~15 seconds
- **Data refresh:** 5 minutes interval  
- **Touch response:** <100ms
- **WiFi signal:** -57 dBm (excellent)
- **HTTP timeout:** 15 seconds
- **MQTT heartbeat:** 60 seconds
- **Free heap:** 213,772 bytes

## 🎯 Roadmap

### Completed ✅
- [x] Swedish character support (ÅÄÖ)
- [x] Touch calibration system  
- [x] MQTT remote control
- [x] Enhanced OTA updates
- [x] HTTP timeout resilience
- [x] Emergency mock backend

### Planned 🎯
- [ ] Live match score updates
- [ ] Player statistics integration
- [ ] Multi-device MQTT fleet management
- [ ] Web-based configuration interface
- [ ] Automated SHL scraping improvements

## 📝 License

MIT License - See LICENSE file for details.

---

**Last Updated:** 2026-01-31 15:14 GMT+1  
**Firmware:** v1.20.1-mqtt-http-fix  
**Device:** ESP32-2432S028 (20:e7:c8:ba:78:94)