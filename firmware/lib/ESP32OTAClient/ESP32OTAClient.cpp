/**
 * ESP32OTAClient Implementation
 */

#include "ESP32OTAClient.h"

ESP32OTAClient::ESP32OTAClient(const char* serverUrl, const char* projectId, const char* currentVersion) 
    : _serverUrl(serverUrl)
    , _projectId(projectId)
    , _currentVersion(currentVersion)
    , _checkInterval(600000)  // 10 minutes default
    , _lastCheck(0)
    , _progressCallback(nullptr)
    , _logCallback(nullptr)
{
}

void ESP32OTAClient::begin() {
    // Generate device ID from MAC address
    uint8_t mac[6];
    WiFi.macAddress(mac);
    char macStr[18];
    snprintf(macStr, sizeof(macStr), "%02X%02X%02X%02X%02X%02X", 
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    _deviceId = String(macStr);
    
    log("ESP32OTAClient initialized");
    char buf[100];
    snprintf(buf, sizeof(buf), "  Device ID: %s", _deviceId.c_str());
    log(buf);
    snprintf(buf, sizeof(buf), "  Project: %s v%s", _projectId.c_str(), _currentVersion.c_str());
    log(buf);
}

void ESP32OTAClient::onProgress(void (*callback)(int progress, int total)) {
    _progressCallback = callback;
}

void ESP32OTAClient::onLog(void (*callback)(const char* message)) {
    _logCallback = callback;
}

String ESP32OTAClient::getDeviceId() {
    return _deviceId;
}

void ESP32OTAClient::setCheckInterval(unsigned long intervalMs) {
    _checkInterval = intervalMs;
}

void ESP32OTAClient::log(const char* message) {
    if (_logCallback) {
        _logCallback(message);
    } else {
        Serial.println(message);
    }
}

void ESP32OTAClient::loop() {
    if (millis() - _lastCheck >= _checkInterval) {
        checkForUpdate();
    }
}

bool ESP32OTAClient::checkForUpdate(const char* channel) {
    _lastCheck = millis();
    
    if (WiFi.status() != WL_CONNECTED) {
        log("OTA: WiFi not connected");
        return false;
    }
    
    log("OTA: Checking for updates...");
    
    HTTPClient http;
    String url = _serverUrl + "/device/check?project=" + _projectId + 
                 "&version=" + _currentVersion + 
                 "&chip_id=" + _deviceId +
                 "&channel=" + String(channel);
    
    http.begin(url);
    http.setTimeout(10000);
    
    int httpCode = http.GET();
    
    if (httpCode != 200) {
        char buf[50];
        snprintf(buf, sizeof(buf), "OTA: Server error %d", httpCode);
        log(buf);
        http.end();
        return false;
    }
    
    String payload = http.getString();
    http.end();
    
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, payload);
    
    if (error) {
        log("OTA: JSON parse error");
        return false;
    }
    
    bool updateAvailable = doc["update_available"] | false;
    
    if (!updateAvailable) {
        log("OTA: Up to date!");
        return false;
    }
    
    // Update available!
    const char* newVersion = doc["version"];
    const char* downloadUrl = doc["download_url"];
    size_t firmwareSize = doc["size"] | 0;
    
    char buf[100];
    snprintf(buf, sizeof(buf), "OTA: Update available! %s -> %s", 
             _currentVersion.c_str(), newVersion);
    log(buf);
    
    // Build full download URL
    String fullUrl = _serverUrl + String(downloadUrl);
    
    // Perform the update
    if (performUpdate(fullUrl.c_str(), firmwareSize)) {
        reportStatus("success", newVersion);
        log("OTA: Update complete! Rebooting...");
        delay(1000);
        ESP.restart();
        return true;
    } else {
        reportStatus("failed", newVersion);
        log("OTA: Update failed!");
        return false;
    }
}

bool ESP32OTAClient::performUpdate(const char* firmwareUrl, size_t firmwareSize) {
    log("OTA: Downloading firmware...");
    
    HTTPClient http;
    http.begin(firmwareUrl);
    http.setTimeout(60000);  // 60s timeout for download
    
    int httpCode = http.GET();
    
    if (httpCode != 200) {
        char buf[50];
        snprintf(buf, sizeof(buf), "OTA: Download failed %d", httpCode);
        log(buf);
        http.end();
        return false;
    }
    
    int contentLength = http.getSize();
    if (contentLength <= 0) {
        contentLength = firmwareSize;
    }
    
    if (contentLength <= 0) {
        log("OTA: Unknown firmware size");
        http.end();
        return false;
    }
    
    char buf[60];
    snprintf(buf, sizeof(buf), "OTA: Firmware size: %d bytes", contentLength);
    log(buf);
    
    // Check if there's enough space
    if (!Update.begin(contentLength)) {
        log("OTA: Not enough space!");
        http.end();
        return false;
    }
    
    // Get the stream
    WiFiClient* stream = http.getStreamPtr();
    
    // Buffer for reading
    uint8_t buffer[1024];
    int bytesRead = 0;
    int totalRead = 0;
    int lastProgress = -1;
    
    log("OTA: Installing...");
    
    while (http.connected() && totalRead < contentLength) {
        size_t available = stream->available();
        
        if (available) {
            bytesRead = stream->readBytes(buffer, min(available, sizeof(buffer)));
            
            if (Update.write(buffer, bytesRead) != bytesRead) {
                log("OTA: Write error!");
                http.end();
                return false;
            }
            
            totalRead += bytesRead;
            
            // Report progress
            int progress = (totalRead * 100) / contentLength;
            if (progress != lastProgress && progress % 10 == 0) {
                lastProgress = progress;
                snprintf(buf, sizeof(buf), "OTA: %d%%", progress);
                log(buf);
                
                if (_progressCallback) {
                    _progressCallback(totalRead, contentLength);
                }
            }
        }
        
        delay(1);
    }
    
    http.end();
    
    if (totalRead != contentLength) {
        snprintf(buf, sizeof(buf), "OTA: Size mismatch %d vs %d", totalRead, contentLength);
        log(buf);
        return false;
    }
    
    if (!Update.end(true)) {
        snprintf(buf, sizeof(buf), "OTA: Finalize error %d", Update.getError());
        log(buf);
        return false;
    }
    
    log("OTA: Success!");
    return true;
}

void ESP32OTAClient::reportStatus(const char* status, const char* version) {
    HTTPClient http;
    String url = _serverUrl + "/device/status";
    
    http.begin(url);
    http.addHeader("Content-Type", "application/json");
    http.setTimeout(5000);
    
    JsonDocument doc;
    doc["device"] = _deviceId;
    doc["project"] = _projectId;
    doc["status"] = status;
    doc["version"] = version;
    doc["previous_version"] = _currentVersion;
    
    String json;
    serializeJson(doc, json);
    
    http.POST(json);
    http.end();
}
