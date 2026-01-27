#!/bin/bash
# Monitor ESP32 Hockey Panel - Continuous stability check

ESP32_IP="192.168.1.225"
LOG_FILE="/tmp/hockey_monitor.log"

echo "🏒 Hockey Panel Monitor (v1.10.0 debug)"
echo "ESP32: $ESP32_IP"
echo "Logs: $LOG_FILE"
echo "Press Ctrl+C to stop"
echo "========================="

while true; do
    TIMESTAMP=$(date '+%Y-%m-%d %H:%M:%S')
    
    # Ping ESP32
    if ping -c 1 -W 2 "$ESP32_IP" &>/dev/null; then
        PING_STATUS="🟢 ONLINE"
    else
        PING_STATUS="🔴 OFFLINE"
    fi
    
    # Check backend
    if curl -s -m 5 http://localhost:3080/api/status | grep -q '"ok":true'; then
        API_STATUS="🟢 OK"
    else
        API_STATUS="🔴 FAIL"
    fi
    
    # Try ESP32 OTA port (3232)
    if nc -z -w1 "$ESP32_IP" 3232 2>/dev/null; then
        OTA_STATUS="🟢 OTA"
    else
        OTA_STATUS="⚪ -"
    fi
    
    STATUS="$TIMESTAMP | ESP32: $PING_STATUS | API: $API_STATUS | $OTA_STATUS"
    echo "$STATUS"
    echo "$STATUS" >> "$LOG_FILE"
    
    # Check memory every 10th loop
    if [ $(($(date +%s) % 60)) -eq 0 ]; then
        FREE_MEM=$(free -h | awk '/^Mem:/ {print $7}')
        echo "   💾 Free memory: $FREE_MEM"
    fi
    
    sleep 10
done