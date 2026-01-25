/**
 * Settings Management for Hockey Panel
 * Handles persistent storage of user preferences
 */

#ifndef SETTINGS_H
#define SETTINGS_H

#include <Preferences.h>

// Settings structure
struct HockeyPanelSettings {
    // Display settings
    uint8_t brightness;      // 0-100%
    uint8_t contrast;        // 0-100% (mapped to display gamma)
    bool autoSleep;          // Auto sleep display
    uint16_t sleepTimeout;   // Minutes before sleep (0 = disabled)
    
    // Network settings
    char wifiSSID[33];
    char wifiPass[65];
    char backendURL[128];
    
    // Update settings
    uint16_t updateInterval; // Seconds between updates (normal)
    uint16_t liveInterval;   // Seconds between updates (live match)
    
    // UI settings
    uint8_t defaultTab;      // Default tab on startup (0=SHL, 1=Allsv, 2=Matcher, 3=Live)
    bool showSeconds;        // Show seconds in clock
    uint8_t colorTheme;      // 0=Dark, 1=Light, 2=Hockey Blue
    
    // About info (read-only in UI)
    char deviceName[32];
};

class SettingsManager {
private:
    Preferences prefs;
    HockeyPanelSettings settings;
    bool modified;
    
public:
    SettingsManager() : modified(false) {
        // Defaults
        settings.brightness = 80;
        settings.contrast = 50;
        settings.autoSleep = true;
        settings.sleepTimeout = 30;
        
        strcpy(settings.wifiSSID, "");
        strcpy(settings.wifiPass, "");
        strcpy(settings.backendURL, "http://192.168.1.223:3080");
        
        settings.updateInterval = 300;  // 5 min
        settings.liveInterval = 30;     // 30 sec
        
        settings.defaultTab = 0;
        settings.showSeconds = false;
        settings.colorTheme = 0;  // Dark
        
        strcpy(settings.deviceName, "HockeyPanel");
    }
    
    void begin() {
        prefs.begin("hockey-panel", false);
        load();
    }
    
    void load() {
        settings.brightness = prefs.getUChar("brightness", 80);
        settings.contrast = prefs.getUChar("contrast", 50);
        settings.autoSleep = prefs.getBool("autoSleep", true);
        settings.sleepTimeout = prefs.getUShort("sleepTime", 30);
        
        prefs.getString("wifiSSID", settings.wifiSSID, sizeof(settings.wifiSSID));
        prefs.getString("wifiPass", settings.wifiPass, sizeof(settings.wifiPass));
        prefs.getString("backendURL", settings.backendURL, sizeof(settings.backendURL));
        
        settings.updateInterval = prefs.getUShort("updateInt", 300);
        settings.liveInterval = prefs.getUShort("liveInt", 30);
        
        settings.defaultTab = prefs.getUChar("defaultTab", 0);
        settings.showSeconds = prefs.getBool("showSec", false);
        settings.colorTheme = prefs.getUChar("theme", 0);
        
        prefs.getString("deviceName", settings.deviceName, sizeof(settings.deviceName));
        
        modified = false;
    }
    
    void save() {
        if (!modified) return;
        
        prefs.putUChar("brightness", settings.brightness);
        prefs.putUChar("contrast", settings.contrast);
        prefs.putBool("autoSleep", settings.autoSleep);
        prefs.putUShort("sleepTime", settings.sleepTimeout);
        
        prefs.putString("wifiSSID", settings.wifiSSID);
        prefs.putString("wifiPass", settings.wifiPass);
        prefs.putString("backendURL", settings.backendURL);
        
        prefs.putUShort("updateInt", settings.updateInterval);
        prefs.putUShort("liveInt", settings.liveInterval);
        
        prefs.putUChar("defaultTab", settings.defaultTab);
        prefs.putBool("showSec", settings.showSeconds);
        prefs.putUChar("theme", settings.colorTheme);
        
        prefs.putString("deviceName", settings.deviceName);
        
        modified = false;
    }
    
    void resetToDefaults() {
        prefs.clear();
        *this = SettingsManager();  // Reset to defaults
        begin();
    }
    
    // Getters
    HockeyPanelSettings& get() { return settings; }
    const HockeyPanelSettings& get() const { return settings; }
    
    // Individual setters (mark as modified)
    void setBrightness(uint8_t val) { 
        if (settings.brightness != val) {
            settings.brightness = val; 
            modified = true; 
        }
    }
    void setContrast(uint8_t val) { 
        if (settings.contrast != val) {
            settings.contrast = val; 
            modified = true; 
        }
    }
    void setAutoSleep(bool val) { settings.autoSleep = val; modified = true; }
    void setSleepTimeout(uint16_t val) { settings.sleepTimeout = val; modified = true; }
    
    void setWiFi(const char* ssid, const char* pass) {
        strncpy(settings.wifiSSID, ssid, sizeof(settings.wifiSSID) - 1);
        strncpy(settings.wifiPass, pass, sizeof(settings.wifiPass) - 1);
        modified = true;
    }
    
    void setBackendURL(const char* url) {
        strncpy(settings.backendURL, url, sizeof(settings.backendURL) - 1);
        modified = true;
    }
    
    void setUpdateInterval(uint16_t normal, uint16_t live) {
        settings.updateInterval = normal;
        settings.liveInterval = live;
        modified = true;
    }
    
    void setTheme(uint8_t theme) { settings.colorTheme = theme; modified = true; }
    void setDefaultTab(uint8_t tab) { settings.defaultTab = tab; modified = true; }
    void setShowSeconds(bool val) { settings.showSeconds = val; modified = true; }
    
    bool isModified() const { return modified; }
};

#endif // SETTINGS_H
