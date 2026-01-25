# Directive: Firmware Development

## Goal
Build and flash firmware for the ESP32-2432S028 "Cheap Yellow Display" (CYD).

## Hardware
- **Board**: ESP32-2432S028 (2.8" TFT, ILI9341, resistive touch)
- **Resolution**: 320x240
- **Touch**: XPT2046 resistive
- **WiFi**: 2.4GHz

## Tools
- PlatformIO (VS Code extension or CLI)
- `firmware/` directory contains the project

## Build & Flash

### USB Flash (first time or recovery)
```bash
cd firmware
pio run -t upload
```

### OTA Flash (over WiFi)
```bash
pio run -t upload --upload-port HockeyPanel.local
```

The device broadcasts mDNS as `HockeyPanel`.

## Configuration

Edit `firmware/src/main.cpp`:
```cpp
const char* WIFI_SSID = "IoT";
const char* WIFI_PASS = "IoTAccess123!";
const char* API_URL = "http://192.168.1.224:3080/api/all";
```

Change API_URL if backend IP changes.

## Features Checklist

- [x] SHL standings table
- [x] Allsvenskan standings table  
- [x] Upcoming matches view
- [x] News list with detail view
- [x] Team info on tap
- [x] Touch calibration (10s long press → settings)
- [x] OTA updates
- [x] Offline detection banner
- [x] Team colors

## Common Issues

### Touch not responding
**Fix**: Long press 10s to open settings → Calibrate touch

### "Backend not responding"
**Fix**: Check WiFi connection, verify API_URL IP address

### Display glitches after OTA
**Fix**: Power cycle the device

## Display Layout

```
┌────────────────────────────────┐
│ [SHL] [HA] [NASTA] [NEWS]  🟢 │ ← Header (28px)
├────────────────────────────────┤
│ # LAG              S  +/-   P  │
│ 1 Frölunda HC     38  +62  87 │
│ 2 Skellefteå AIK  39  +46  79 │
│ ...                            │
│                           ▲▼   │ ← Scroll indicators
└────────────────────────────────┘
```

## Learnings Log

- 2026-01-25: v1.9.0 - Touch calibration added, 4-tab layout
- Touch calibration values vary per unit, must be stored in NVS
