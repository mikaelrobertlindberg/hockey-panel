#!/bin/bash
# Test WiFi stability and monitor RSSI

ESP32_IP="192.168.1.185"
echo "🏒 Testing WiFi Stability + RSSI for v1.10.0"
echo "ESP32: $ESP32_IP (HockeyPanel.local)"
echo "=========================================="

echo "📶 Ping test (20 packets)..."
ping -c 20 -i 0.5 "$ESP32_IP" | while read line; do
    if echo "$line" | grep -q "time="; then
        TIME=$(echo "$line" | grep -o 'time=[0-9.]*' | cut -d= -f2)
        if (( $(echo "$TIME < 50" | bc -l) )); then
            printf "✅${TIME}ms "
        elif (( $(echo "$TIME < 100" | bc -l) )); then
            printf "🟡${TIME}ms "
        else
            printf "🔴${TIME}ms "
        fi
    elif echo "$line" | grep -q "packet loss"; then
        echo
        echo "$line"
    fi
done

echo
echo "🔌 Testing OTA port (3232)..."
if nc -z -w3 "$ESP32_IP" 3232; then
    echo "✅ OTA port accessible"
else
    echo "❌ OTA port not responding"
fi

echo
echo "📊 Backend API responses..."
for i in {1..5}; do
    START=$(date +%s%N)
    if curl -s -m 3 "http://localhost:3080/api/status" >/dev/null; then
        END=$(date +%s%N)
        DURATION=$(( (END - START) / 1000000 ))
        echo "  ✅ API response: ${DURATION}ms"
    else
        echo "  ❌ API timeout"
    fi
    sleep 1
done

echo
echo "🔄 Continuous monitoring (30s) - Check for dropouts..."
DROPPED=0
TOTAL=0

for i in {1..30}; do
    if ping -c 1 -W 1 "$ESP32_IP" >/dev/null 2>&1; then
        printf "✅"
    else
        printf "❌"
        ((DROPPED++))
    fi
    ((TOTAL++))
    sleep 1
done

echo
echo "📈 Stability Report:"
echo "  Packets lost: $DROPPED/$TOTAL"
PERCENT=$(( 100 - (DROPPED * 100 / TOTAL) ))
echo "  Uptime: ${PERCENT}%"

if [ "$PERCENT" -gt 95 ]; then
    echo "  🎉 Excellent stability!"
elif [ "$PERCENT" -gt 85 ]; then
    echo "  ✅ Good stability"
else
    echo "  ⚠️  Unstable connection"
fi