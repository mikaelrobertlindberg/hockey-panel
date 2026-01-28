# Hockey Panel v1.10.0 - Debug Guide

## 🔍 Current Status (2026-01-27 08:22)

### ✅ Working:
- Backend API responding normally (port 3080)
- SHL scraper: 14 teams
- Allsvenskan scraper: 14 teams  
- Stress test: 10/10 requests OK
- System resources: Good (12GB free RAM)

### ❓ Investigating:
- **ESP32 not responding on network**
- Expected IP 192.168.1.225 = offline
- OTA port (3232) not accessible

## 🛠️ Troubleshooting Steps

### Step 1: Check ESP32 Power & Serial
```bash
# Check if ESP32 is connected via USB
ls -la /dev/ttyUSB*

# Basic serial read (if ESP32 is logging)
timeout 10s cat /dev/ttyUSB0
```

### Step 2: Find ESP32 on Network  
```bash
# Quick network scan for ESP32
cd /home/devpi/clawd/hockey-panel
./monitor_esp32.sh

# Or manual IP scan (check OTA port)
nmap -p 3232 192.168.1.0/24 | grep -B2 "3232/tcp open"
```

### Step 3: WiFi Connection Issues
**Possible causes:**
- WiFi credentials changed
- Router assigned different IP  
- ESP32 crashed and not connecting
- Network isolation/firewall

**Check router DHCP table for "HockeyPanel" or MAC: `20:e7:c8:ba:78:94`**

### Step 4: Force Reset & Recalibrate
If ESP32 is found but acting strange:

```bash
# Reset via serial (if connected)
# 1. Hold RESET button
# 2. Release
# 3. Monitor serial output

# Or flash fresh firmware
cd firmware  
python3 -m platformio run -t upload -e esp32-cyd
```

### Step 5: Backend Logs
```bash
# Check for ESP32 API requests
journalctl -f | grep "192.168.1"
tail -f /tmp/hockey-backend.log
```

## 🔧 v1.10.0 Fixes Applied

- **✅ Touch calibration**: More relaxed validation, unique NVS namespace
- **✅ WiFi reconnect**: Automatic every 30s if connection drops  
- **✅ Watchdog timer**: 30s timeout prevents hanging
- **✅ Error recovery**: Shorter HTTP timeouts, better logging

## 🎯 Expected Behavior

**Healthy ESP32 should:**
1. Boot and show firmware version (v1.10.0)  
2. Connect to WiFi "IoT"
3. Get IP address (probably 192.168.1.x)
4. Fetch data from 192.168.1.224:3080 every 5 minutes
5. Display SHL/HA tables, allow touch navigation
6. Remember calibration between reboots

## 🚨 If Still Having Issues

1. **Calibration loop**: Fixed in v1.10.0, but if still happens:
   - Long press 10s → Settings → Calibrate Touch
   - Or clear NVS: reflash firmware

2. **WiFi instability**: v1.10.0 has auto-reconnect
   - Check router logs for device disconnects
   - Consider dedicated 2.4GHz SSID for IoT devices

3. **Hanging**: Watchdog should prevent this now
   - Check serial for crash logs
   - Monitor using `./monitor_esp32.sh`

## 📊 Monitoring Commands

```bash
# Continuous ESP32 monitoring
./monitor_esp32.sh

# API health check  
watch -n 30 'curl -s http://localhost:3080/api/status'

# System resources
watch -n 60 'free -h && uptime'
```

## v1.12.0-responsive (2026-01-27)
**EVENT-DRIVEN RESPONSIVE UI - Löser touch lag & skärmblinking**

### 🎯 Problem Åtgärdat:
- **Touch lag** - För frekventa display updates blockerade touch handling
- **Skärmblinking** - Display uppdaterades varje loop-iteration onödigt
- **Låsta loopar** - Blocking operations förhindrade responsiv UI

### ✅ Nya Features:
1. **Display Dirty Flag System** - Bara rita om när innehåll faktiskt ändrats
2. **Event-Driven Touch** - 50Hz touch check med debouncing
3. **Non-Blocking Timing** - Allt styrs via millis() istället för delay()
4. **State Management** - Proper screen state tracking
5. **Rate-Limited Operations** - Network (30s), Touch (20ms), Display (vid behov)

### ⚡ Performance:
- **Main loop:** 200Hz (5ms delay) för ultra-responsiv UI
- **Touch check:** 50Hz (20ms interval) 
- **Network requests:** Var 30:e sekund (non-blocking)
- **Display updates:** Endast vid faktiska förändringar

### 🔄 Testing Checklist (v1.12.0):
- [ ] Touch reagerar direkt (ingen lag)  
- [ ] Skärm slutar blinka/flicker
- [ ] Smooth navigation mellan skärmar
- [ ] Serial commands respond (`status`, `main`, `fetch`)

