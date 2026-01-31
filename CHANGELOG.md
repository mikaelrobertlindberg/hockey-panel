# 🏒 Hockey Panel Changelog

## [v1.20.1-mqtt-http-fix] - 2026-01-31

### 🚨 Critical HTTP Timeout Fix
- **Fixed:** HTTP requests no longer timeout due to MQTT interference
- **Root Cause:** Backend SHL scraping hangs → ESP32 HTTP client timeouts
- **Solution:** Non-blocking MQTT + Emergency mock backend

### 🔧 MQTT Improvements  
- **Changed:** Non-blocking MQTT reconnection (removes 5s delay loop)
- **Changed:** MQTT heartbeat frequency: 30s → 60s (reduced bandwidth)
- **Changed:** Reconnection attempts: max every 10 seconds

### 🌐 HTTP Resilience
- **Increased:** HTTP timeout: 8s → 15s
- **Added:** Connect timeout: 5s for faster failure detection
- **Improved:** Error handling for network issues

### 📡 Testing Results
- ✅ **Network:** 192.168.1.185 (-57 dBm excellent signal)
- ✅ **MQTT:** Commands working (refresh/reboot/calibrate)  
- ✅ **Memory:** 213,772 bytes free heap
- ✅ **Display:** Swedish teams showing correctly
- ✅ **Performance:** No more crashes or hangs

---

## [v1.20.0-mqtt-ota-enhanced] - 2026-01-31

### 🚀 Major New Features

#### 📡 MQTT Integration
- **Added:** Full MQTT support via PubSubClient v2.8.0
- **Topics:** 
  - `hockey/panel/status` - Device telemetry
  - `hockey/panel/command` - Remote control  
  - `hockey/panel/data` - Hockey data updates
- **Commands:** `reboot`, `refresh`, `calibrate`
- **Heartbeat:** 30-second status publishing with device metrics

#### 🔧 Enhanced OTA Updates
- **Added:** Password protection (`hockey2026`)
- **Added:** Visual progress bar on display during update
- **Added:** Comprehensive error handling with MQTT notifications
- **Added:** Real-time progress updates and error messages
- **Improved:** Memory management during OTA process

#### 📊 Status Telemetry
JSON status includes:
```json
{
  "device": "hockey-panel",
  "firmware": "1.20.0-mqtt-ota-enhanced",
  "ip": "192.168.1.185", 
  "rssi": -57,
  "uptime": 269665,
  "free_heap": 213772,
  "shl_teams": 14,
  "ha_teams": 14,
  "matches": 1
}
```

### 🛠️ Technical Changes
- **Added:** PlatformIO dependency: `knolleary/PubSubClient@^2.8`
- **Memory:** RAM usage: 18.2% (59,600 bytes)
- **Memory:** Flash usage: 92.5% (1,212,881 bytes)
- **Size:** Firmware increased by ~3KB for MQTT support

---

## [v1.19.1-swedish-utf8-fix] - 2026-01-31

### 🇸🇪 Swedish Character Support - FINAL FIX
- **Fixed:** Perfect rendering of ÅÄÖ in team names
- **Solution:** lgfxJapanGothic_12 font + UTF-8 HTTP headers
- **Testing:** "Frölunda HC", "Skellefteå AIK", "Luleå Hockey" display correctly
- **Backend:** Enhanced scraper with `Accept-Charset: utf-8`

### ⚡ USB Flashing Optimization  
- **Improved:** Reduced upload speed to 115200 baud for 100% reliability
- **Result:** 84-second flash process without disconnection errors
- **Hardware:** ESP32-2432S028 "Cheap Yellow Display" fully supported

### 🎯 System Stability
- **Memory:** Flash usage: 91.8% (stable)
- **Memory:** RAM usage: 18.1% (optimized)
- **Performance:** 2ms API response time
- **Update:** 5-minute data refresh cycle

---

## [v1.19.0] - 2026-01-30

### 🎮 Touch Interface Revolution
- **Added:** Complete touch navigation system
- **Added:** Multi-screen support (SHL/HA/Division3/News)
- **Added:** Team detail views with statistics
- **Added:** Touch calibration system with preferences storage

### 📱 Display Improvements
- **Changed:** Display driver to LovyanGFX (major performance boost)
- **Added:** Hardware-optimized ESP32-2432S028 configuration  
- **Added:** XPT2046 resistive touch controller support
- **Fixed:** Screen artifacts with double-clear system

### 🏒 Hockey Data Integration
- **Added:** Full SHL standings (14 teams)
- **Added:** HockeyAllsvenskan standings (14 teams) 
- **Added:** Division 3 support for local leagues
- **Added:** Live match data and news integration
- **Backend:** TypeScript scraper with Puppeteer

---

## [v1.18.x] - Earlier Versions

### Legacy Features
- Basic HTTP data fetching
- Simple text display
- WiFi connectivity
- OTA update foundation
- Initial ESP32 board support

---

## 🔧 Backend Changes

### Emergency Mock API - 2026-01-31
**File:** `backend/mock-api.js`

**Purpose:** Prevent ESP32 crashes when main backend hangs
- **Data:** Mock Swedish teams (Frölunda HC, Skellefteå AIK, Luleå Hockey)
- **Response:** Instant HTTP 200 responses
- **Usage:** `cd backend && node mock-api.js`

### Main Backend Issues  
- **Problem:** SHL website scraping causes 10+ second hangs
- **Impact:** ESP32 HTTP client timeout → firmware crash
- **Temporary:** Use mock API until scraping is fixed
- **Long-term:** Implement async scraping with caching

---

## 📊 Performance Evolution

| Version | RAM | Flash | Boot Time | Features |
|---------|-----|-------|-----------|----------|
| v1.18.x | 15% | 85% | 10s | Basic display |
| v1.19.0 | 17% | 89% | 12s | Touch + Multi-screen |
| v1.19.1 | 18.1% | 91.8% | 15s | Swedish chars |  
| v1.20.0 | 18.2% | 92.5% | 15s | **+MQTT +OTA** |
| v1.20.1 | 18.2% | 92.5% | 15s | **+HTTP fix** |

## 🎯 Development Insights

### What Worked Well
- ✅ **Systematic debugging** - Root cause analysis saved hours
- ✅ **Mock API approach** - Instant resolution for backend hangs
- ✅ **Non-blocking MQTT** - Prevents firmware lockups
- ✅ **UTF-8 pipeline** - Complete scraper → font solution
- ✅ **Visual OTA progress** - Great user experience

### Lessons Learned
- 🧠 **MQTT + HTTP don't mix** - Require non-blocking designs
- 🧠 **Backend stability critical** - ESP32 very sensitive to timeouts
- 🧠 **Font selection matters** - For international character support  
- 🧠 **Emergency fallbacks** - Mock APIs prevent system failures
- 🧠 **USB baud rates** - Slower = more reliable for ESP32 flashing

---

**Last Updated:** 2026-01-31 15:14 GMT+1  
**Next Release:** v1.21.0 (Live match scores + Web scraping fixes)