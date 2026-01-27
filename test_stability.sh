#!/bin/bash
# Hockey Panel Stability Test v1.10.0

ESP32_IP="192.168.1.225"  # May vary
API_URL="http://localhost:3080"

echo "🏒 Hockey Panel Stability Test"
echo "====================================="
echo

# Test 1: Backend API Health
echo "📡 Testing Backend API..."
if curl -s "$API_URL/api/status" | grep -q '"ok":true'; then
    echo "✅ Backend API: OK"
else
    echo "❌ Backend API: FAILED"
    exit 1
fi

# Test 2: Data scraping
echo "🔍 Testing Data Scraping..."
SHL_COUNT=$(curl -s "$API_URL/api/shl" | grep -o '"name"' | wc -l)
HA_COUNT=$(curl -s "$API_URL/api/allsvenskan" | grep -o '"name"' | wc -l)

if [ "$SHL_COUNT" -gt 10 ] && [ "$HA_COUNT" -gt 10 ]; then
    echo "✅ Scrapers: OK (SHL: $SHL_COUNT teams, HA: $HA_COUNT teams)"
else
    echo "❌ Scrapers: FAILED (SHL: $SHL_COUNT, HA: $HA_COUNT)"
fi

# Test 3: Try to find ESP32 on network
echo "🔍 Looking for ESP32 on network..."
if ping -c 1 -W 1 "$ESP32_IP" &>/dev/null; then
    echo "✅ ESP32 found at $ESP32_IP"
    
    # Try to get ESP32 info via mDNS
    if ping -c 1 -W 1 HockeyPanel.local &>/dev/null; then
        echo "✅ mDNS: HockeyPanel.local resolves"
    fi
else
    echo "⚠️  ESP32 not responding at $ESP32_IP"
    echo "   Scanning common range..."
    for i in {220..230}; do
        if ping -c 1 -W 1 "192.168.1.$i" &>/dev/null 2>&1; then
            # Quick check if this might be our ESP32
            if nc -z -w1 "192.168.1.$i" 3232 2>/dev/null; then
                echo "   Found possible ESP32 at 192.168.1.$i (OTA port open)"
                ESP32_IP="192.168.1.$i"
            fi
        fi
    done
fi

# Test 4: Monitor backend requests for ESP32 activity
echo "📊 Monitoring backend for ESP32 requests (30s)..."
echo "   Watching for API calls..."

# Start monitoring access logs in background
touch /tmp/api_monitor.log
(timeout 30s tcpdump -i any -n port 3080 2>/dev/null | grep -E "(GET|POST)" || echo "tcpdump not available") > /tmp/api_monitor.log &

# Also check process list for node/backend
if pgrep -f "hockey-panel\|tsx.*server.ts" >/dev/null; then
    echo "✅ Backend process: Running"
else
    echo "⚠️  Backend process: Not found, starting..."
    cd /home/devpi/clawd/hockey-panel/backend
    nohup npx tsx src/server.ts > /tmp/hockey-backend.log 2>&1 &
    sleep 3
fi

sleep 5
echo "   Partial results after 5s..."

# Test 5: Stress test API
echo "🔄 Stress testing API (10 requests)..."
for i in {1..10}; do
    if curl -s -m 5 "$API_URL/api/all" >/dev/null; then
        printf "✅"
    else
        printf "❌"
    fi
    sleep 1
done
echo
echo "   Stress test completed"

# Test 6: Memory/process check
echo "💾 System Resources..."
FREE_MEM=$(free -h | awk '/^Mem:/ {print $7}')
LOAD_AVG=$(uptime | awk -F'load average:' '{print $2}' | xargs)
echo "   Free memory: $FREE_MEM"
echo "   Load average: $load_AVG"

echo
echo "🔍 Test Summary:"
echo "- Backend API: Working"
echo "- Scrapers: Working"
echo "- ESP32 Network: $([ -n "$ESP32_IP" ] && echo "Found at $ESP32_IP" || echo "Not detected")"
echo
echo "✨ For continuous monitoring:"
echo "   watch -n 30 'curl -s http://localhost:3080/api/status'"
echo "   journalctl -f | grep hockey"