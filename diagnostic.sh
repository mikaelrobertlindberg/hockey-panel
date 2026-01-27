#!/bin/bash
# Hockey Panel v1.10.0 - Full Diagnostic Suite
# Tests all functions via serial monitoring and WiFi stability

SERIAL_PORT="/dev/ttyUSB0"
TEST_DURATION=${1:-300}  # Default 5 minutes
LOG_FILE="/tmp/hockey_diagnostic_$(date +%Y%m%d_%H%M%S).log"

echo "🏒 Hockey Panel Full Diagnostic v1.10.0"
echo "Serial: $SERIAL_PORT"
echo "Duration: ${TEST_DURATION}s"
echo "Log: $LOG_FILE"
echo "======================================="

# Check serial connection
if [ ! -c "$SERIAL_PORT" ]; then
    echo "❌ ERROR: Serial port $SERIAL_PORT not found"
    echo "   Connect ESP32 via USB and try again"
    exit 1
fi

# Configure serial port
stty -F "$SERIAL_PORT" 115200 raw -echo
echo "✅ Serial port configured (115200 baud)"

# Create diagnostic log header
cat > "$LOG_FILE" << EOF
Hockey Panel Diagnostic Log
===========================
Start time: $(date)
Firmware expected: v1.10.0
Test duration: ${TEST_DURATION}s
Serial port: $SERIAL_PORT

EOF

echo "📡 Starting serial monitoring and diagnostics..."
echo "   Press Ctrl+C to stop early"
echo "   Use 'tail -f $LOG_FILE' in another terminal to follow"

# Function to parse and analyze serial output
analyze_serial() {
    local line="$1"
    local timestamp=$(date '+%H:%M:%S')
    
    echo "[$timestamp] $line" >> "$LOG_FILE"
    
    # Check for key events
    case "$line" in
        *"Hockey Panel v"*)
            VERSION=$(echo "$line" | grep -o 'v[0-9]\+\.[0-9]\+\.[0-9]\+')
            echo "🚀 [$timestamp] Firmware detected: $VERSION"
            ;;
        *"WiFi"*"Connected"*|*"WiFi"*"OK"*)
            IP=$(echo "$line" | grep -o '[0-9]\+\.[0-9]\+\.[0-9]\+\.[0-9]\+')
            echo "📶 [$timestamp] WiFi connected: $IP"
            ;;
        *"RSSI"*|*"Signal"*)
            RSSI=$(echo "$line" | grep -o '\-[0-9]\+')
            if [ ! -z "$RSSI" ]; then
                if [ "$RSSI" -gt -50 ]; then
                    echo "📶 [$timestamp] WiFi RSSI: $RSSI dBm (Excellent)"
                elif [ "$RSSI" -gt -70 ]; then
                    echo "📶 [$timestamp] WiFi RSSI: $RSSI dBm (Good)"
                elif [ "$RSSI" -gt -80 ]; then
                    echo "⚠️  [$timestamp] WiFi RSSI: $RSSI dBm (Weak)"
                else
                    echo "❌ [$timestamp] WiFi RSSI: $RSSI dBm (Very weak)"
                fi
            fi
            ;;
        *"Touch cal"*)
            if echo "$line" | grep -q "VALID"; then
                echo "👆 [$timestamp] Touch calibration: Valid"
            else
                echo "⚠️  [$timestamp] Touch calibration: Invalid/Default"
            fi
            ;;
        *"Data"*"OK"*|*"Data:"*"SHL"*)
            echo "📊 [$timestamp] Data fetch successful"
            ;;
        *"HTTP error"*|*"Failed to fetch"*)
            ERROR_CODE=$(echo "$line" | grep -o '[0-9]\{3\}')
            echo "❌ [$timestamp] HTTP error: $ERROR_CODE"
            ;;
        *"WiFi lost"*|*"reconnecting"*)
            echo "🔄 [$timestamp] WiFi reconnection attempt"
            ;;
        *"Watchdog"*|*"WDT"*)
            echo "⚠️  [$timestamp] Watchdog activity detected"
            ;;
        *"Guru Meditation"*|*"Exception"*|*"PANIC"*)
            echo "💥 [$timestamp] CRASH DETECTED: $line"
            ;;
        *"Ready!"*)
            echo "✅ [$timestamp] System ready and operational"
            ;;
    esac
}

# Start background monitoring
{
    timeout "$TEST_DURATION" cat "$SERIAL_PORT" 2>/dev/null | while IFS= read -r line; do
        analyze_serial "$line"
    done
} &
SERIAL_PID=$!

# Function tests via network (if ESP32 is online)
test_network_functions() {
    echo "🌐 Testing network functions..."
    
    # Try to find ESP32 IP
    ESP32_IP=""
    for i in {220..230}; do
        if ping -c 1 -W 1 "192.168.1.$i" &>/dev/null; then
            if nc -z -w1 "192.168.1.$i" 3232 2>/dev/null; then
                ESP32_IP="192.168.1.$i"
                echo "🎯 Found ESP32 at: $ESP32_IP"
                break
            fi
        fi
    done
    
    if [ -n "$ESP32_IP" ]; then
        echo "📶 Testing ESP32 network stability..."
        for i in {1..20}; do
            if ping -c 1 -W 1 "$ESP32_IP" &>/dev/null; then
                printf "✅"
            else
                printf "❌"
            fi
            sleep 1
        done
        echo
        echo "🔄 Network ping test completed"
    else
        echo "⚠️  ESP32 not found on network, skipping network tests"
    fi
}

# Backend stability test
test_backend() {
    echo "🖥️  Testing backend stability..."
    
    for i in {1..10}; do
        START_TIME=$(date +%s%N)
        if curl -s -m 5 "http://localhost:3080/api/status" >/dev/null; then
            END_TIME=$(date +%s%N)
            DURATION=$(( (END_TIME - START_TIME) / 1000000 ))
            printf "✅${DURATION}ms "
        else
            printf "❌ "
        fi
        sleep 2
    done
    echo
    echo "🔄 Backend response test completed"
}

# System resource monitoring
monitor_resources() {
    echo "💾 Monitoring system resources..."
    
    for i in {1..5}; do
        FREE_MEM=$(free -m | awk '/^Mem:/ {print $7}')
        LOAD_AVG=$(uptime | awk -F'load average:' '{print $2}' | awk '{print $1}' | tr -d ',')
        CPU_TEMP=""
        
        if [ -f /sys/class/thermal/thermal_zone0/temp ]; then
            CPU_TEMP=$(cat /sys/class/thermal/thermal_zone0/temp)
            CPU_TEMP=$((CPU_TEMP / 1000))
            echo "📊 Memory: ${FREE_MEM}MB free | Load: $LOAD_AVG | CPU: ${CPU_TEMP}°C"
        else
            echo "📊 Memory: ${FREE_MEM}MB free | Load: $LOAD_AVG"
        fi
        sleep 10
    done
}

# Start parallel tests
echo "🔄 Running parallel diagnostics..."

# Network tests
test_network_functions &
NET_PID=$!

# Backend tests  
test_backend &
BACKEND_PID=$!

# Resource monitoring
monitor_resources &
RESOURCE_PID=$!

# Wait for serial monitoring to finish
wait $SERIAL_PID

# Wait for other tests
wait $NET_PID 2>/dev/null
wait $BACKEND_PID 2>/dev/null  
wait $RESOURCE_PID 2>/dev/null

echo
echo "📋 Diagnostic Summary"
echo "===================="

# Analyze log for issues
if [ -f "$LOG_FILE" ]; then
    echo "🔍 Log analysis:"
    
    CRASHES=$(grep -c "CRASH DETECTED\|Exception\|PANIC" "$LOG_FILE")
    WIFI_RECONNECTS=$(grep -c "WiFi lost\|reconnecting" "$LOG_FILE")
    HTTP_ERRORS=$(grep -c "HTTP error" "$LOG_FILE")
    WATCHDOG_EVENTS=$(grep -c "Watchdog\|WDT" "$LOG_FILE")
    
    echo "   Crashes: $CRASHES"
    echo "   WiFi reconnects: $WIFI_RECONNECTS"
    echo "   HTTP errors: $HTTP_ERRORS"
    echo "   Watchdog events: $WATCHDOG_EVENTS"
    
    if [ "$CRASHES" -eq 0 ] && [ "$HTTP_ERRORS" -lt 3 ]; then
        echo "✅ System appears stable"
    else
        echo "⚠️  Issues detected, check log: $LOG_FILE"
    fi
    
    # Show RSSI trend if available
    if grep -q "RSSI" "$LOG_FILE"; then
        echo
        echo "📶 WiFi Signal Strength:"
        grep "RSSI" "$LOG_FILE" | tail -5
    fi
    
else
    echo "⚠️  No log file created"
fi

echo
echo "📄 Full log: $LOG_FILE"
echo "🔍 Use: grep -E '(ERROR|CRASH|WiFi|RSSI)' $LOG_FILE"
echo "✨ Diagnostic completed!"