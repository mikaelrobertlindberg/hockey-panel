/**
 * ESP32OTAClient - OTA Update Client for ESP32
 * Connects to a central OTA server for firmware updates
 */

#ifndef ESP32OTA_CLIENT_H
#define ESP32OTA_CLIENT_H

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <Update.h>
#include <ArduinoJson.h>

class ESP32OTAClient {
public:
    /**
     * Create OTA client
     * @param serverUrl Base URL of OTA server (e.g., "http://192.168.1.224:3333")
     * @param projectId Unique project identifier (e.g., "hockey-panel")
     * @param currentVersion Current firmware version (e.g., "1.0.0")
     */
    ESP32OTAClient(const char* serverUrl, const char* projectId, const char* currentVersion);
    
    /**
     * Initialize the client (call in setup)
     */
    void begin();
    
    /**
     * Check for updates and install if available
     * @param channel Release channel ("stable" or "beta")
     * @return true if update was installed and device will reboot
     */
    bool checkForUpdate(const char* channel = "stable");
    
    /**
     * Set callback for update progress
     * @param callback Function(int progress, int total)
     */
    void onProgress(void (*callback)(int progress, int total));
    
    /**
     * Set callback for log messages
     * @param callback Function(const char* message)
     */
    void onLog(void (*callback)(const char* message));
    
    /**
     * Get device ID (MAC-based)
     */
    String getDeviceId();
    
    /**
     * Set check interval for auto-check mode
     */
    void setCheckInterval(unsigned long intervalMs);
    
    /**
     * Auto-check loop (call in main loop for periodic checks)
     */
    void loop();

private:
    String _serverUrl;
    String _projectId;
    String _currentVersion;
    String _deviceId;
    unsigned long _checkInterval;
    unsigned long _lastCheck;
    
    void (*_progressCallback)(int progress, int total);
    void (*_logCallback)(const char* message);
    
    void log(const char* message);
    bool performUpdate(const char* firmwareUrl, size_t firmwareSize);
    void reportStatus(const char* status, const char* version);
};

#endif
