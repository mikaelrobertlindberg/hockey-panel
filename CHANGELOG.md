# Changelog

## v1.10.0 - 2026-01-27

### 🔧 Fixed
- **Touch calibration persistence**: Relaxed validation logic to prevent good calibrations from being invalidated
- **Unique NVS namespace**: Changed from "touch" to "hockey-touch" to avoid conflicts
- **WiFi auto-reconnect**: Added checkWiFiConnection() that runs every 30s to reconnect if WiFi drops
- **Watchdog timer**: Added ESP32 watchdog (30s timeout) to prevent hanging
- **HTTP timeout**: Reduced from 10s to 8s for faster error recovery
- **Better startup logic**: Only force calibration on completely fresh installs

### 📝 Changes
- Touch validation now only checks for obvious corruption (negative values, ridiculous ranges)
- Existing calibration values are trusted even if marked invalid
- WiFi reconnection attempts every 30 seconds if connection is lost
- Watchdog resets device if it hangs for more than 30 seconds
- Improved error handling during data fetching

### 🐛 Bug Reports Fixed
- "Screen needs calibration every startup" → Fixed with relaxed validation
- "Device hangs and loses connection" → Fixed with WiFi reconnect + watchdog

## v1.9.0 - 2026-01-25

### Features
- Touch calibration with NVS persistence
- 4 screens: SHL, Allsvenskan, Upcoming matches, News
- Team info details on tap
- Settings menu (10s long press)
- OTA updates
- Offline detection banner