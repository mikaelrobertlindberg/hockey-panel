/**
 * Hockey Panel - ESP32-2432S028 "Cheap Yellow Display" 
 * v1.10.4 - Re-enabled auto calibration for fresh installs
 */

#include <Arduino.h>
#include <WiFi.h>
#include <ArduinoOTA.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <esp_task_wdt.h>
#include "display_config.hpp"

#define FIRMWARE_VERSION "1.19.1-swedish-utf8-fix"

// WiFi
const char* WIFI_SSID = "IoT";
const char* WIFI_PASS = "IoTAccess123!";
const char* API_URL = "http://192.168.1.224:3080/api/all";
const char* DIV3_API_URL = "http://192.168.1.224:3001/division3";

// Colors
#define COLOR_BG       0x1082
#define COLOR_HEADER   0x2945
#define COLOR_TEXT     0xFFFF
#define COLOR_ACCENT   0xFD20
#define COLOR_GREEN    0x07E0
#define COLOR_RED      0xF800
#define COLOR_SHL      0x001F
#define COLOR_HA       0x0640
#define COLOR_DIM      0x7BEF

// Screens
enum Screen { SCREEN_SHL, SCREEN_HA, SCREEN_DIV3, SCREEN_NEXT, SCREEN_NEWS, SCREEN_NEWS_DETAIL, SCREEN_TEAM_INFO, SCREEN_SETTINGS, SCREEN_CALIBRATE };
Screen currentScreen = SCREEN_SHL;
Screen previousScreen = SCREEN_SHL;

// Preferences for storing calibration
Preferences prefs;

// Touch calibration data - RELAXED validation
struct TouchCalibration {
    int16_t xMin, xMax, yMin, yMax;
    bool valid;
} touchCal = {300, 3800, 300, 3800, false};

// Screen cleaning utility
void forceCleanScreen() {
    display.fillScreen(COLOR_BG);
    display.fillScreen(COLOR_BG);  // Double-clear for artifacts
    delay(50);
}

// Data structures
struct Team {
    String name;
    int position;
    int points;
    int played;
    int goalDiff;
    int wins;
    int draws;  // OT wins
    int losses;
    int goalsFor;
    int goalsAgainst;
};

struct Match {
    String homeTeam;
    String awayTeam;
    int homeScore;
    int awayScore;
    String time;
    String status;
    bool isSHL;
};

struct NewsItem {
    String title;
    String summary;
    String league;  // "SHL" or "HA"
};

// Data storage
Team shlTeams[14];
int shlTeamCount = 0;
Team haTeams[14];
int haTeamCount = 0;
Team div3Teams[16];  // Division 3 has more teams
int div3TeamCount = 0;
Match allMatches[40];
int matchCount = 0;

NewsItem allNews[20];
int newsCount = 0;

// Selected team
int selectedTeamIndex = -1;
bool selectedIsSHL = true;

// Selected news
int selectedNewsIndex = -1;

// Timing
unsigned long lastFetch = 0;
unsigned long FETCH_INTERVAL = 300000;      // 5 min normal
unsigned long FETCH_INTERVAL_LIVE = 30000;  // 30s live match
unsigned long FETCH_INTERVAL_ERROR = 15000; // 15s on error
bool liveMatch = false;

// Connection
unsigned long lastSuccessfulFetch = 0;
unsigned long lastWiFiCheck = 0;
const unsigned long CONNECTION_TIMEOUT = 180000;
const unsigned long WIFI_CHECK_INTERVAL = 30000;  // Check WiFi every 30s
bool connectionOK = false;

// === RESPONSIVE UI FIXES ===
bool displayDirty = true;           // Flag to force display redraw
unsigned long lastTouchCheck = 0;   // Last time touch was checked
unsigned long touchDebounceTime = 0; // Touch debouncing
bool touchPressed = false;          // Touch state tracking
const unsigned long TOUCH_CHECK_INTERVAL = 20; // 50Hz touch checking
const unsigned long TOUCH_DEBOUNCE = 100;      // 100ms debounce

// Scroll
int scrollOffset = 0;
const int VISIBLE_TEAMS = 12;
const int VISIBLE_MATCHES = 5;

// Touch
bool touchActive = false;
unsigned long lastTouchTime = 0;

// Long press for settings
unsigned long touchStartTime = 0;
bool longPressTriggered = false;
const unsigned long LONG_PRESS_TIME = 10000;  // 10 seconds

// Calibration state
int calibrationStep = 0;
int16_t calPoints[4][2];  // Raw touch values for 4 corners

// Touch debug removed - using direct mapping now

// Data loaded flag
bool dataLoaded = false;

// Fade transitions for smoother UI
float screenFadeAlpha = 1.0;
unsigned long fadeStartTime = 0;
const unsigned long FADE_DURATION = 300; // 300ms fade for smooth transitions
Screen fadingToScreen = SCREEN_SHL;
bool isFading = false;

// Clean feedback modal
bool showPositiveModal = false;
unsigned long modalStartTime = 0;
const unsigned long MODAL_DURATION = 2000; // 2 seconds
String modalMessage = "";

// Forward declarations
void markDisplayDirty();
void checkWiFiConnection();

void clearCalibration() {
    prefs.begin("hockey-touch", false);  // Use unique namespace
    prefs.clear();
    prefs.end();
    touchCal.valid = false;
    Serial.println("Calibration cleared!");
}

void loadCalibration() {
    prefs.begin("hockey-touch", true);
    touchCal.xMin = prefs.getShort("xMin", 300);
    touchCal.xMax = prefs.getShort("xMax", 3800);
    touchCal.yMin = prefs.getShort("yMin", 300);
    touchCal.yMax = prefs.getShort("yMax", 3800);
    touchCal.valid = prefs.getBool("valid", false);
    prefs.end();
    
    // RELAXED validation - only check for obvious corruption
    if (touchCal.xMax <= touchCal.xMin || touchCal.yMax <= touchCal.yMin ||
        touchCal.xMax < 0 || touchCal.yMax < 0 || touchCal.xMin < 0 || touchCal.yMin < 0 ||
        touchCal.xMax > 5000 || touchCal.yMax > 5000 ||
        (touchCal.xMax - touchCal.xMin) < 200 || (touchCal.yMax - touchCal.yMin) < 200) {
        Serial.println("Corrupted calibration detected, using defaults");
        touchCal.xMin = 300;
        touchCal.xMax = 3800;
        touchCal.yMin = 300;
        touchCal.yMax = 3800;
        touchCal.valid = false;
    } else if (touchCal.valid) {
        Serial.println("Valid calibration loaded from NVS");
    }
    
    Serial.printf("Touch cal: %s [%d-%d, %d-%d]\n", 
        touchCal.valid ? "VALID" : "DEFAULT",
        touchCal.xMin, touchCal.xMax, touchCal.yMin, touchCal.yMax);
}

void saveCalibration() {
    prefs.begin("hockey-touch", false);
    prefs.putShort("xMin", touchCal.xMin);
    prefs.putShort("xMax", touchCal.xMax);
    prefs.putShort("yMin", touchCal.yMin);
    prefs.putShort("yMax", touchCal.yMax);
    prefs.putBool("valid", true);
    prefs.end();
    touchCal.valid = true;
    Serial.printf("Touch calibration saved: [%d-%d, %d-%d]\n",
        touchCal.xMin, touchCal.xMax, touchCal.yMin, touchCal.yMax);
}

// Get calibrated touch coordinates
bool getCalibratedTouch(int& x, int& y) {
    uint16_t rx, ry;
    if (!display.getTouch(&rx, &ry)) return false;
    
    // Map raw values to screen coordinates
    x = map(rx, touchCal.xMin, touchCal.xMax, 0, 320);
    y = map(ry, touchCal.yMin, touchCal.yMax, 0, 240);
    
    // Clamp to screen
    x = constrain(x, 0, 319);
    y = constrain(y, 0, 239);
    
    return true;
}

// Get raw touch for calibration
bool getRawTouch(int16_t& rx, int16_t& ry) {
    uint16_t x, y;
    if (!display.getTouch(&x, &y)) return false;
    rx = x;
    ry = y;
    return true;
}

void connectWiFi() {
    if (WiFi.status() == WL_CONNECTED) {
        return;  // Already connected
    }
    
    Serial.print("WiFi connecting");
    WiFi.mode(WIFI_STA);
    
    // WiFi power optimizations for better range
    WiFi.setTxPower(WIFI_POWER_19_5dBm);  // Max power (19.5 dBm)
    WiFi.setSleep(false);                 // Disable power save mode
    
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 30) {
        delay(500);
        Serial.print(".");
        attempts++;
        esp_task_wdt_reset();  // Feed watchdog
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        long rssi = WiFi.RSSI();
        String signalQuality;
        if (rssi > -50) signalQuality = "Excellent";
        else if (rssi > -60) signalQuality = "Very Good"; 
        else if (rssi > -70) signalQuality = "Good";
        else if (rssi > -80) signalQuality = "Fair";
        else signalQuality = "Weak";
        
        Serial.printf(" Connected!\n");
        Serial.printf("  IP: %s\n", WiFi.localIP().toString().c_str());
        Serial.printf("  RSSI: %d dBm (%s)\n", rssi, signalQuality.c_str());
        Serial.printf("  TX Power: 19.5 dBm (Max)\n");
    } else {
        Serial.println(" FAILED - Check signal strength or move closer to router");
    }
}

void checkWiFiConnection() {
    if (millis() - lastWiFiCheck < WIFI_CHECK_INTERVAL) {
        return;
    }
    lastWiFiCheck = millis();
    
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("WiFi lost, reconnecting...");
        connectWiFi();
    } else {
        // Log signal strength every 2 minutes for diagnostics
        static unsigned long lastRSSILog = 0;
        if (millis() - lastRSSILog > 120000) {  // 2 minutes
            long rssi = WiFi.RSSI();
            Serial.printf("WiFi Status: Connected | RSSI: %d dBm | IP: %s\n", 
                         rssi, WiFi.localIP().toString().c_str());
            lastRSSILog = millis();
        }
    }
}

void parseTeams(JsonArray arr, Team* teams, int& count, int maxCount) {
    count = 0;
    for (JsonObject t : arr) {
        if (count >= maxCount) break;
        // Ensure proper UTF-8 handling for Swedish characters
        String rawName = t["name"].as<String>();
        teams[count].name = rawName;
        teams[count].position = t["position"].as<int>();
        if (teams[count].position == 0) teams[count].position = count + 1;
        teams[count].points = t["points"].as<int>();
        teams[count].played = t["played"].as<int>();
        teams[count].goalDiff = t["goalDiff"].as<int>();
        teams[count].wins = t["wins"].as<int>();
        teams[count].draws = t["draws"].as<int>();
        teams[count].losses = t["losses"].as<int>();
        teams[count].goalsFor = t["goalsFor"].as<int>();
        teams[count].goalsAgainst = t["goalsAgainst"].as<int>();
        
        // Fallback calculations if data missing
        if (teams[count].wins == 0 && teams[count].points > 0) {
            teams[count].wins = teams[count].points / 3;
            teams[count].draws = teams[count].points % 3;
            teams[count].losses = teams[count].played - teams[count].wins - teams[count].draws;
        }
        count++;
    }
}

void parseMatches(JsonArray arr, bool isSHL) {
    for (JsonObject m : arr) {
        if (matchCount >= 40) break;
        allMatches[matchCount].homeTeam = m["homeTeam"].as<String>();
        allMatches[matchCount].awayTeam = m["awayTeam"].as<String>();
        allMatches[matchCount].homeScore = m["homeScore"].is<int>() ? m["homeScore"].as<int>() : -1;
        allMatches[matchCount].awayScore = m["awayScore"].is<int>() ? m["awayScore"].as<int>() : -1;
        allMatches[matchCount].time = m["time"].as<String>();
        allMatches[matchCount].status = m["status"].as<String>();
        allMatches[matchCount].isSHL = isSHL;
        if (allMatches[matchCount].status == "live") liveMatch = true;
        matchCount++;
    }
}

void parseNews(JsonArray arr, const char* league) {
    for (JsonObject n : arr) {
        if (newsCount >= 20) break;
        allNews[newsCount].title = n["title"].as<String>();
        allNews[newsCount].summary = n["summary"].as<String>();
        allNews[newsCount].league = league;
        newsCount++;
    }
}

void fetchDivision3Data() {
    HTTPClient http;
    http.begin(DIV3_API_URL);
    http.setTimeout(5000);
    http.setReuse(false);   // Prevent memory leaks
    
    int httpCode = http.GET();
    
    if (httpCode == HTTP_CODE_OK) {
        String payload = http.getString();
        
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, payload);
        
        if (!error) {
            if (doc["division3"].is<JsonObject>()) {
                JsonObject div3 = doc["division3"];
                if (div3["standings"].is<JsonArray>()) {
                    parseTeams(div3["standings"].as<JsonArray>(), div3Teams, div3TeamCount, 16);
                    Serial.printf("Division 3: Loaded %d teams from API\n", div3TeamCount);
                }
            }
        } else {
            Serial.printf("Division 3 JSON parse error: %s\n", error.c_str());
        }
    } else {
        Serial.printf("Division 3 API failed (%d), using fallback data\n", httpCode);
        // Fallback to realistic Division 3 data with real team names
        const char* realDiv3Teams[] = {
            "Kallinge/Ronneby", "Mörrums GoIS", "Växjö Lakers HC", "Tingsryds AIF",
            "Olofströms IK", "Aseda IF", "IFK Berga", "Kalmar HC",
            "Lessebo HC", "Alvesta SK", "Emmaboda IS", "Lindsdals IF",
            "Torsas GoIF", "Nybro Vikings IF", "Karlskrona HK", "Kristianstad IK"
        };
        
        div3TeamCount = 16;
        for (int i = 0; i < div3TeamCount; i++) {
            div3Teams[i].position = i + 1;
            div3Teams[i].name = realDiv3Teams[i];
            div3Teams[i].played = 22 + (i % 3);  // Slight variation
            div3Teams[i].wins = max(0, 18 - i);
            div3Teams[i].draws = i % 4;  // Some variety in draws
            div3Teams[i].losses = div3Teams[i].played - div3Teams[i].wins - div3Teams[i].draws;
            div3Teams[i].goalsFor = 55 - i * 2;
            div3Teams[i].goalsAgainst = 30 + i * 2;
            div3Teams[i].goalDiff = div3Teams[i].goalsFor - div3Teams[i].goalsAgainst;
            div3Teams[i].points = div3Teams[i].wins * 3 + div3Teams[i].draws; // Standard points
        }
    }
    
    http.end();
}

void fetchData() {
    esp_task_wdt_reset();  // Feed watchdog
    
    if (WiFi.status() != WL_CONNECTED) {
        connectionOK = false;
        Serial.println("fetchData: WiFi not connected");
        return;
    }
    
    // Log WiFi signal strength before each fetch
    long rssi = WiFi.RSSI();
    if (rssi < -80) {
        Serial.printf("Warning: Weak WiFi signal (%d dBm) - may affect data fetch\n", rssi);
    }
    
    HTTPClient http;
    http.begin(API_URL);
    http.setTimeout(8000);  // Shorter timeout
    http.setReuse(false);   // Prevent memory leaks
    // Ensure UTF-8 content handling for Swedish characters
    http.addHeader("Accept-Charset", "utf-8");
    
    Serial.printf("Fetching data... (RSSI: %d dBm)\n", rssi);
    int code = http.GET();
    
    if (code == 200) {
        String payload = http.getString();
        JsonDocument doc;
        
        if (!deserializeJson(doc, payload)) {
            liveMatch = false;
            matchCount = 0;
            newsCount = 0;
            
            if (doc["shl"].is<JsonObject>()) {
                JsonObject shl = doc["shl"];
                if (shl["standings"].is<JsonArray>()) {
                    parseTeams(shl["standings"].as<JsonArray>(), shlTeams, shlTeamCount, 14);
                }
                if (shl["matches"].is<JsonArray>()) {
                    parseMatches(shl["matches"].as<JsonArray>(), true);
                }
                if (shl["news"].is<JsonArray>()) {
                    parseNews(shl["news"].as<JsonArray>(), "SHL");
                }
            }
            
            if (doc["allsvenskan"].is<JsonObject>()) {
                JsonObject ha = doc["allsvenskan"];
                if (ha["standings"].is<JsonArray>()) {
                    parseTeams(ha["standings"].as<JsonArray>(), haTeams, haTeamCount, 14);
                }
                if (ha["matches"].is<JsonArray>()) {
                    parseMatches(ha["matches"].as<JsonArray>(), false);
                }
                if (ha["news"].is<JsonArray>()) {
                    parseNews(ha["news"].as<JsonArray>(), "HA");
                }
            }
            
            // Division 3 will be fetched separately from dedicated scraper
            
            connectionOK = true;
            lastSuccessfulFetch = millis();
            dataLoaded = true;
            long rssi = WiFi.RSSI();
            Serial.printf("Data OK: SHL %d, HA %d, News %d | RSSI: %d dBm\n", 
                         shlTeamCount, haTeamCount, newsCount, rssi);
        }
    } else {
        long rssi = WiFi.RSSI();
        Serial.printf("HTTP error: %d | RSSI: %d dBm\n", code, rssi);
        if (rssi < -85) {
            Serial.println("HTTP error likely due to weak WiFi signal!");
        }
        connectionOK = false;
    }
    http.end();
    
    // Fetch Division 3 data from dedicated scraper
    fetchDivision3Data();
}

String shortName(const String& name, int maxLen) {
    if (name.length() <= maxLen) return name;
    return name.substring(0, maxLen-1) + ".";
}

uint16_t getTeamColor(const String& name) {
    if (name.indexOf("lunda") >= 0) return 0x0400;
    if (name.indexOf("Skellefte") >= 0) return 0xFFE0;
    if (name.indexOf("gle") >= 0) return 0x3A1F;
    if (name.indexOf("xj") >= 0) return 0x0640;
    if (name.indexOf("Malm") >= 0) return 0xF800;
    if (name.indexOf("Lule") >= 0) return 0xF800;
    if (name.indexOf("Timr") >= 0) return 0xF800;
    if (name.indexOf("Bryn") >= 0) return 0xC000;
    if (name.indexOf("rjestad") >= 0) return 0xFFE0;
    if (name.indexOf("Djurg") >= 0) return 0x001F;
    if (name.indexOf("ping") >= 0) return 0x001F;
    if (name.indexOf("HV71") >= 0) return 0x001F;
    if (name.indexOf("Leksand") >= 0) return 0x001F;
    if (name.indexOf("Oskarshamn") >= 0) return 0xFD20;
    if (name.indexOf("AIK") >= 0) return 0x0000;
    if (name.indexOf("Almtuna") >= 0) return 0x001F;
    if (name.indexOf("BIK") >= 0 || name.indexOf("Karlskoga") >= 0) return 0xF800;
    if (name.indexOf("rkl") >= 0 || name.indexOf("Bjor") >= 0) return 0x07E0;
    if (name.indexOf("Kristianstad") >= 0) return 0x001F;
    if (name.indexOf("Karlskrona") >= 0) return 0x001F;
    if (name.indexOf("Modo") >= 0 || name.indexOf("MoDo") >= 0) return 0xF800;
    if (name.indexOf("Mora") >= 0) return 0xF800;
    if (name.indexOf("Nybro") >= 0) return 0xFD20;
    if (name.indexOf("dert") >= 0 || name.indexOf("SSK") >= 0) return 0x0000;
    if (name.indexOf("Tingsryd") >= 0) return 0xF800;
    if (name.indexOf("sby") >= 0) return 0x001F;
    if (name.indexOf("ster") >= 0) return 0xFFE0;
    if (name.indexOf("Vita") >= 0) return 0xFFFF;
    return COLOR_DIM;
}

bool isTimedOut() {
    return lastSuccessfulFetch == 0 || (millis() - lastSuccessfulFetch) > CONNECTION_TIMEOUT;
}

void drawHeader() {
    display.fillRect(0, 0, 320, 24, COLOR_HEADER);
    display.setFont(&fonts::lgfxJapanGothic_12);
    display.setTextSize(1);
    
    // 5 tabs: SHL, HA, DIV3, NEXT, NEWS  
    const char* tabs[] = {"SHL", "HA", "DIV3", "NEXT", "NEWS"};
    uint16_t colors[] = {COLOR_SHL, COLOR_HA, 0x8410, COLOR_ACCENT, 0xF81F}; // Orange for DIV3, Magenta for NEWS
    int tabWidth = 64;  // 320 pixels / 5 tabs = 64 pixels per tab
    
    for (int i = 0; i < 5; i++) {
        int x = i * tabWidth;
        bool active = (i == currentScreen) || 
                      (currentScreen == SCREEN_TEAM_INFO && i == previousScreen) ||
                      (currentScreen == SCREEN_NEWS_DETAIL && i == SCREEN_NEWS);
        
        if (active) {
            display.fillRect(x, 0, tabWidth, 28, colors[i]);
            display.setTextColor(COLOR_TEXT);
        } else {
            display.setTextColor(COLOR_DIM);
        }
        display.setCursor(x + 6, 7);  // Adjusted spacing for narrower tabs
        display.print(tabs[i]);
    }
    
    // Status dot
    int sx = 305, sy = 12;
    if (isTimedOut()) {
        display.fillCircle(sx, sy, 7, COLOR_RED);
    } else if (connectionOK) {
        display.fillCircle(sx, sy, 7, COLOR_GREEN);
    } else {
        display.fillCircle(sx, sy, 7, COLOR_ACCENT);
    }
    display.drawCircle(sx, sy, 7, COLOR_TEXT);
}

void drawSettingsIcon() {
    // Small discrete settings icon in bottom right corner
    if (currentScreen != SCREEN_SETTINGS && currentScreen != SCREEN_CALIBRATE) {
        // Small subtle gear icon (using simple circles and dots)
        int x = 295, y = 225;
        display.drawCircle(x, y, 6, 0x4208); // Very dark gray
        display.fillCircle(x, y, 3, 0x4208);
        display.drawCircle(x, y, 8, 0x4208);
        // Small dots around for gear teeth
        for (int i = 0; i < 6; i++) {
            float angle = i * 60 * PI / 180;
            int dx = cos(angle) * 8;
            int dy = sin(angle) * 8;
            display.fillCircle(x + dx, y + dy, 1, 0x4208);
        }
    }
}

void drawOfflineBanner() {
    // Offline banner removed - status shown by connection indicator in header
}

void drawTable(Team* teams, int count, const char* title, uint16_t accent) {
    display.fillRect(0, 24, 320, 216, COLOR_BG);
    display.setFont(&fonts::lgfxJapanGothic_12);
    
    display.fillRect(0, 24, 320, 14, accent);
    display.setTextColor(COLOR_TEXT);
    display.setCursor(5, 26);
    display.print(title);
    
    display.setTextColor(COLOR_ACCENT);
    display.setCursor(5, 40);
    display.print("#");
    display.setCursor(22, 40);
    display.print("LAG");
    display.setCursor(175, 40);
    display.print("S");
    display.setCursor(200, 40);
    display.print("+/-");
    display.setCursor(240, 40);
    display.print("P");
    
    display.drawLine(0, 50, 270, 50, COLOR_HEADER);
    
    // Scroll arrows positioned lower for better usability
    if (scrollOffset > 0) {
        display.fillTriangle(280, 140, 290, 130, 300, 140, COLOR_ACCENT);
        display.drawTriangle(280, 140, 290, 130, 300, 140, COLOR_TEXT);
    }
    if (scrollOffset + VISIBLE_TEAMS < count) {
        display.fillTriangle(280, 220, 290, 230, 300, 220, COLOR_ACCENT);
        display.drawTriangle(280, 220, 290, 230, 300, 220, COLOR_TEXT);
    }
    
    int end = min(scrollOffset + VISIBLE_TEAMS, count);
    for (int i = scrollOffset; i < end; i++) {
        int row = i - scrollOffset;
        int y = 53 + row * 15;
        
        if (row % 2 == 1) {
            display.fillRect(0, y - 1, 270, 18, COLOR_HEADER);
        }
        
        display.setTextColor(COLOR_TEXT);
        display.setCursor(5, y);
        display.printf("%2d", i + 1);
        
        display.fillCircle(30, y + 5, 5, getTeamColor(teams[i].name));
        
        display.setCursor(40, y);
        display.print(shortName(teams[i].name, 15));
        
        display.setCursor(175, y);
        display.printf("%2d", teams[i].played);
        
        int gd = teams[i].goalDiff;
        display.setTextColor(gd > 0 ? COLOR_GREEN : (gd < 0 ? COLOR_RED : COLOR_TEXT));
        display.setCursor(195, y);
        display.printf("%+3d", gd);
        
        display.setTextColor(COLOR_ACCENT);
        display.setCursor(238, y);
        display.printf("%3d", teams[i].points);
    }
    
    display.setFont(&fonts::lgfxJapanGothic_12);
    display.setTextColor(COLOR_DIM);
    display.setCursor(275, 140);
    display.printf("%d/%d", min(scrollOffset + 1, max(1, count - VISIBLE_TEAMS + 1)), 
                   max(1, count - VISIBLE_TEAMS + 1));
}

void drawMatches(const char* filter, const char* title) {
    display.fillRect(0, 24, 320, 216, COLOR_BG);
    display.setFont(&fonts::lgfxJapanGothic_12);
    
    display.fillRect(0, 24, 320, 14, COLOR_ACCENT);
    display.setTextColor(COLOR_TEXT);
    display.setCursor(5, 26);
    display.print(title);
    
    int indices[40];
    int filtered = 0;
    for (int i = 0; i < matchCount && filtered < 40; i++) {
        bool match = (strcmp(filter, "upcoming") == 0 && allMatches[i].status == "upcoming") ||
                     (strcmp(filter, "finished") == 0 && allMatches[i].status == "finished");
        if (match) indices[filtered++] = i;
    }
    
    if (filtered == 0) {
        display.setTextColor(COLOR_DIM);
        display.setCursor(100, 120);
        display.print("Inga matcher");
        return;
    }
    
    // Scroll arrows positioned lower for better usability  
    if (scrollOffset > 0) {
        display.fillTriangle(280, 140, 290, 130, 300, 140, COLOR_ACCENT);
        display.drawTriangle(280, 140, 290, 130, 300, 140, COLOR_TEXT);
    }
    if (scrollOffset + VISIBLE_MATCHES < filtered) {
        display.fillTriangle(280, 220, 290, 230, 300, 220, COLOR_ACCENT);
        display.drawTriangle(280, 220, 290, 230, 300, 220, COLOR_TEXT);
    }
    
    int end = min(scrollOffset + VISIBLE_MATCHES, filtered);
    for (int i = scrollOffset; i < end; i++) {
        int row = i - scrollOffset;
        int y = 42 + row * 35;
        Match* m = &allMatches[indices[i]];
        
        display.fillRoundRect(5, y, 280, 32, 6, COLOR_HEADER);
        
        display.fillRoundRect(8, y + 3, 22, 12, 4, m->isSHL ? COLOR_SHL : COLOR_HA);
        display.setFont(&fonts::lgfxJapanGothic_12);
        display.setTextColor(COLOR_TEXT);
        display.setCursor(10, y + 5);
        display.print(m->isSHL ? "SHL" : "HA");
        
        display.setFont(&fonts::lgfxJapanGothic_12);
        display.setTextSize(1);
        display.setCursor(35, y + 6);
        display.print(shortName(m->homeTeam, 12));
        display.setCursor(35, y + 20);
        display.print(shortName(m->awayTeam, 12));
        
        display.setFont(&fonts::lgfxJapanGothic_12);
        display.setTextSize(2);
        if (m->status == "finished" || m->status == "live") {
            display.setTextColor(COLOR_ACCENT);
            display.setCursor(200, y + 8);
            display.printf("%d - %d", max(0, m->homeScore), max(0, m->awayScore));
            if (m->status == "live") {
                display.setTextSize(1);
                display.setTextColor(COLOR_RED);
                display.setCursor(265, y + 15);
                display.print("LIVE");
            }
        } else {
            display.setTextColor(COLOR_GREEN);
            display.setCursor(220, y + 8);
            display.print(m->time.c_str());
        }
        display.setTextSize(1);
    }
}

void drawNews() {
    display.fillRect(0, 24, 320, 216, COLOR_BG);
    display.setFont(&fonts::lgfxJapanGothic_12);
    
    display.fillRect(0, 24, 320, 14, COLOR_ACCENT);
    display.setTextColor(COLOR_TEXT);
    display.setCursor(5, 26);
    display.print("Senaste nyheterna");
    
    if (newsCount == 0) {
        display.setTextColor(COLOR_DIM);
        display.setCursor(80, 120);
        display.print("Inga nyheter just nu");
        return;
    }
    
    // Scroll
    const int VISIBLE_NEWS = 7;
    // Scroll arrows positioned lower for better usability
    if (scrollOffset > 0) {
        display.fillTriangle(280, 140, 290, 130, 300, 140, COLOR_ACCENT);
        display.drawTriangle(280, 140, 290, 130, 300, 140, COLOR_TEXT);
    }
    if (scrollOffset + VISIBLE_NEWS < newsCount) {
        display.fillTriangle(280, 220, 290, 230, 300, 220, COLOR_ACCENT);
        display.drawTriangle(280, 220, 290, 230, 300, 220, COLOR_TEXT);
    }
    
    int end = min(scrollOffset + VISIBLE_NEWS, newsCount);
    for (int i = scrollOffset; i < end; i++) {
        int row = i - scrollOffset;
        int y = 42 + row * 28;
        
        // News card
        display.fillRoundRect(5, y, 305, 25, 6, COLOR_HEADER);
        
        // League badge
        bool isSHL = allNews[i].league == "SHL";
        display.fillRoundRect(8, y + 3, 28, 12, 6, isSHL ? COLOR_SHL : COLOR_HA);
        display.setFont(&fonts::lgfxJapanGothic_12);
        display.setTextColor(COLOR_TEXT);
        display.setCursor(12, y + 5);
        display.print(isSHL ? "SHL" : "HA");
        
        // Title (truncated)
        display.setFont(&fonts::lgfxJapanGothic_12);
        display.setTextColor(COLOR_TEXT);
        display.setCursor(42, y + 8);
        String title = allNews[i].title;
        if (title.length() > 35) {
            title = title.substring(0, 34) + "...";
        }
        display.print(title);
    }
    
    display.setFont(&fonts::lgfxJapanGothic_12);
    display.setTextColor(COLOR_DIM);
    display.setCursor(280, 140);
    display.printf("%d/%d", min(scrollOffset + 1, max(1, newsCount - VISIBLE_NEWS + 1)), 
                   max(1, newsCount - VISIBLE_NEWS + 1));
}

void drawNewsDetail() {
    if (selectedNewsIndex < 0 || selectedNewsIndex >= newsCount) {
        currentScreen = SCREEN_NEWS;
        markDisplayDirty();
        return;
    }
    
    NewsItem* news = &allNews[selectedNewsIndex];
    
    display.fillRect(0, 28, 320, 212, COLOR_BG);
    
    // Back button
    display.fillRoundRect(5, 32, 50, 20, 8, COLOR_HEADER);
    display.setFont(&fonts::lgfxJapanGothic_12);
    display.setTextColor(COLOR_TEXT);
    display.setCursor(12, 38);
    display.print("< BACK");
    
    // League badge
    bool isSHL = news->league == "SHL";
    display.fillRoundRect(65, 32, 35, 20, 8, isSHL ? COLOR_SHL : COLOR_HA);
    display.setCursor(72, 38);
    display.print(isSHL ? "SHL" : "HA");
    
    // Title (wrapped)
    display.setFont(&fonts::lgfxJapanGothic_12);
    display.setTextColor(COLOR_ACCENT);
    
    String title = news->title;
    int y = 60;
    int maxWidth = 300;
    int charWidth = 8;
    int charsPerLine = maxWidth / charWidth;
    
    // Simple word wrap for title
    while (title.length() > 0 && y < 100) {
        int lineLen = min((int)title.length(), charsPerLine);
        // Find last space within line
        if (title.length() > charsPerLine) {
            int lastSpace = title.lastIndexOf(' ', lineLen);
            if (lastSpace > 10) lineLen = lastSpace;
        }
        display.setCursor(10, y);
        display.print(title.substring(0, lineLen));
        title = title.substring(lineLen);
        title.trim();
        y += 16;
    }
    
    // Separator
    y += 5;
    display.drawLine(10, y, 310, y, COLOR_HEADER);
    y += 10;
    
    // Summary text (wrapped, max ~8 lines)
    display.setTextColor(COLOR_TEXT);
    String summary = news->summary;
    if (summary.length() == 0) {
        summary = "(Ingen sammanfattning tillganglig)";
    }
    
    int maxLines = 7;
    int lineCount = 0;
    while (summary.length() > 0 && lineCount < maxLines && y < 238) {
        int lineLen = min((int)summary.length(), charsPerLine + 5);
        if (summary.length() > lineLen) {
            int lastSpace = summary.lastIndexOf(' ', lineLen);
            if (lastSpace > 10) lineLen = lastSpace;
        }
        display.setCursor(10, y);
        display.print(summary.substring(0, lineLen));
        summary = summary.substring(lineLen);
        summary.trim();
        y += 15;
        lineCount++;
    }
    
    if (summary.length() > 0) {
        display.setCursor(10, y);
        display.setTextColor(COLOR_DIM);
        display.print("...");
    }
}

void drawTeamInfo() {
    Team* team = (previousScreen == SCREEN_SHL) ? &shlTeams[selectedTeamIndex] : 
                 (previousScreen == SCREEN_HA) ? &haTeams[selectedTeamIndex] : 
                 &div3Teams[selectedTeamIndex];
    
    display.fillRect(0, 28, 320, 194, COLOR_BG);
    
    display.fillRoundRect(5, 32, 50, 20, 8, COLOR_HEADER);
    display.setFont(&fonts::lgfxJapanGothic_12);
    display.setTextColor(COLOR_TEXT);
    display.setCursor(12, 38);
    display.print("< BACK");
    
    uint16_t teamColor = getTeamColor(team->name);
    display.fillRect(0, 55, 320, 35, teamColor);
    display.setFont(&fonts::lgfxJapanGothic_12);
    display.setTextSize(1);
    display.setTextColor(COLOR_TEXT);
    display.setCursor(10, 62);
    display.printf("#%d %s", team->position, team->name.c_str());
    display.setCursor(10, 78);
    const char* leagueName = (previousScreen == SCREEN_SHL) ? "SHL" :
                             (previousScreen == SCREEN_HA) ? "HockeyAllsvenskan" :
                             "Division 3";
    display.print(leagueName);
    
    display.fillRect(0, 95, 320, 145, COLOR_BG);
    display.setFont(&fonts::lgfxJapanGothic_12);
    
    int y = 98;
    int col1 = 15, col2 = 165;
    int lineH = 18;
    
    // Row 1: Played + Points
    display.setTextColor(COLOR_DIM);
    display.setCursor(col1, y);
    display.print("Matcher:");
    display.setTextColor(COLOR_TEXT);
    display.setCursor(col1 + 65, y);
    display.printf("%d", team->played);
    
    display.setTextColor(COLOR_DIM);
    display.setCursor(col2, y);
    display.print("Poang:");
    display.setTextColor(COLOR_ACCENT);
    display.setCursor(col2 + 55, y);
    display.printf("%d", team->points);
    
    y += lineH;
    
    // Row 2: Wins + Losses
    display.setTextColor(COLOR_DIM);
    display.setCursor(col1, y);
    display.print("Vinster:");
    display.setTextColor(COLOR_GREEN);
    display.setCursor(col1 + 65, y);
    display.printf("%d", team->wins);
    
    display.setTextColor(COLOR_DIM);
    display.setCursor(col2, y);
    display.print("Forluster:");
    display.setTextColor(COLOR_RED);
    display.setCursor(col2 + 75, y);
    display.printf("%d", team->losses);
    
    y += lineH;
    
    // Row 3: OT + Goal diff
    display.setTextColor(COLOR_DIM);
    display.setCursor(col1, y);
    display.print("OT/SO:");
    display.setTextColor(COLOR_TEXT);
    display.setCursor(col1 + 65, y);
    display.printf("%d", team->draws);
    
    display.setTextColor(COLOR_DIM);
    display.setCursor(col2, y);
    display.print("+/-:");
    display.setTextColor(team->goalDiff > 0 ? COLOR_GREEN : (team->goalDiff < 0 ? COLOR_RED : COLOR_TEXT));
    display.setCursor(col2 + 35, y);
    display.printf("%+d", team->goalDiff);
    
    y += lineH;
    
    // Row 4: Points per game + Win %
    display.setTextColor(COLOR_DIM);
    display.setCursor(col1, y);
    display.print("P/match:");
    display.setTextColor(COLOR_TEXT);
    display.setCursor(col1 + 65, y);
    if (team->played > 0) {
        display.printf("%.2f", (float)team->points / team->played);
    }
    
    display.setTextColor(COLOR_DIM);
    display.setCursor(col2, y);
    display.print("Vinst%:");
    display.setTextColor(COLOR_TEXT);
    display.setCursor(col2 + 55, y);
    if (team->played > 0) {
        display.printf("%d%%", (team->wins * 100) / team->played);
    }
    
    // Playoff status bar
    y += lineH + 8;
    display.fillRect(10, y, 300, 2, teamColor);
    y += 8;
    
    display.setCursor(col1, y);
    if (team->position <= 6) {
        display.setTextColor(COLOR_GREEN);
        display.print("SLUTSPEL - Direktplats");
    } else if (team->position <= 10) {
        display.setTextColor(COLOR_ACCENT);
        display.print("PLAY-IN - Kval till slutspel");
    } else if (team->position <= 12) {
        display.setTextColor(COLOR_DIM);
        display.print("Utanfor slutspel");
    } else {
        display.setTextColor(COLOR_RED);
        display.print("KVAL - Nedflyttningsrisk");
    }
}

void drawSettings() {
    display.fillRect(0, 28, 320, 212, COLOR_BG);
    display.setFont(&fonts::lgfxJapanGothic_12);
    
    // Back button
    display.fillRoundRect(5, 32, 50, 20, 8, COLOR_HEADER);
    display.setFont(&fonts::lgfxJapanGothic_12);
    display.setTextColor(COLOR_TEXT);
    display.setCursor(12, 38);
    display.print("< BACK");
    
    display.setFont(&fonts::lgfxJapanGothic_12);
    display.fillRect(60, 28, 260, 20, COLOR_DIM);
    display.setTextColor(COLOR_TEXT);
    display.setCursor(120, 32);
    display.print("INSTALLNINGAR");
    
    int y = 60;
    
    // Calibrate touch button
    display.fillRoundRect(20, y, 280, 40, 12, COLOR_ACCENT);
    display.setTextColor(COLOR_TEXT);
    display.setCursor(60, y + 12);
    display.print("KALIBRERA TOUCH");
    
    y += 55;
    
    // Touch status
    display.setTextColor(COLOR_DIM);
    display.setCursor(20, y);
    display.print("Touch-kalibrering:");
    display.setTextColor(touchCal.valid ? COLOR_GREEN : COLOR_RED);
    display.setCursor(180, y);
    display.print(touchCal.valid ? "OK" : "EJ KALIBRERAD");
    
    y += 25;
    
    // WiFi status
    display.setTextColor(COLOR_DIM);
    display.setCursor(20, y);
    display.print("WiFi:");
    display.setTextColor(WiFi.status() == WL_CONNECTED ? COLOR_GREEN : COLOR_RED);
    display.setCursor(180, y);
    display.print(WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString().c_str() : "Ej ansluten");
    
    y += 25;
    
    // Version
    display.setTextColor(COLOR_DIM);
    display.setCursor(20, y);
    display.print("Firmware:");
    display.setTextColor(COLOR_TEXT);
    display.setCursor(180, y);
    display.printf("v%s", FIRMWARE_VERSION);
    
    y += 25;
    
    // API status
    display.setTextColor(COLOR_DIM);
    display.setCursor(20, y);
    display.print("Data:");
    display.setTextColor(connectionOK ? COLOR_GREEN : COLOR_RED);
    display.setCursor(180, y);
    display.print(connectionOK ? "OK" : "Offline");
    
    // Hint
    display.setFont(&fonts::lgfxJapanGothic_12);
    display.setTextColor(COLOR_DIM);
    display.setCursor(50, 215);
    display.print("Hall inne 10s for att oppna denna meny");
}

void drawCalibrationScreen() {
    display.fillScreen(COLOR_BG);
    display.setFont(&fonts::lgfxJapanGothic_12);
    display.setTextColor(COLOR_TEXT);
    
    display.setCursor(60, 100);
    display.print("TOUCH-KALIBRERING");
    
    display.setFont(&fonts::lgfxJapanGothic_12);
    display.setCursor(40, 130);
    display.print("Tryck pa korset med pennan");
    
    display.setTextColor(COLOR_DIM);
    display.setCursor(90, 150);
    display.printf("Steg %d av 4", calibrationStep + 1);
    
    // Draw crosshair at target position
    int tx, ty;
    switch (calibrationStep) {
        case 0: tx = 20;  ty = 20;  break;  // Top-left
        case 1: tx = 300; ty = 20;  break;  // Top-right
        case 2: tx = 300; ty = 220; break;  // Bottom-right
        case 3: tx = 20;  ty = 220; break;  // Bottom-left
        default: return;
    }
    
    // Draw crosshair
    display.drawLine(tx - 15, ty, tx + 15, ty, COLOR_ACCENT);
    display.drawLine(tx, ty - 15, tx, ty + 15, COLOR_ACCENT);
    display.drawCircle(tx, ty, 8, COLOR_ACCENT);
    display.fillCircle(tx, ty, 3, COLOR_RED);
    
    // Cancel hint
    display.setTextColor(COLOR_DIM);
    display.setCursor(80, 210);
    display.print("Haller i 3s for att avbryta");
}



void finishCalibration() {
    // Calculate min/max from the 4 corner points
    // Points: 0=TL, 1=TR, 2=BR, 3=BL
    int leftAvg = (calPoints[0][0] + calPoints[3][0]) / 2;   // Left edge avg
    int rightAvg = (calPoints[1][0] + calPoints[2][0]) / 2;  // Right edge avg
    int topAvg = (calPoints[0][1] + calPoints[1][1]) / 2;    // Top edge avg
    int bottomAvg = (calPoints[2][1] + calPoints[3][1]) / 2; // Bottom edge avg
    
    touchCal.xMin = leftAvg;
    touchCal.xMax = rightAvg; 
    touchCal.yMin = topAvg;
    touchCal.yMax = bottomAvg;
    
    // Add some margin (the targets are 20px from edge)
    if (touchCal.xMax > touchCal.xMin && touchCal.yMax > touchCal.yMin) {
        int xMargin = (touchCal.xMax - touchCal.xMin) * 20 / 280;
        int yMargin = (touchCal.yMax - touchCal.yMin) * 20 / 200;
        
        touchCal.xMin -= xMargin;
        touchCal.xMax += xMargin;
        touchCal.yMin -= yMargin;
        touchCal.yMax += yMargin;
    } else {
        // Fallback to safe defaults if calibration failed
        touchCal.xMin = 300;
        touchCal.xMax = 3800;
        touchCal.yMin = 300;
        touchCal.yMax = 3800;
    }
    
    saveCalibration();
    
    Serial.printf("Calibration saved: x[%d-%d] y[%d-%d]\n", 
        touchCal.xMin, touchCal.xMax, touchCal.yMin, touchCal.yMax);
    
    display.fillScreen(COLOR_BG);
    display.setFont(&fonts::lgfxJapanGothic_12);
    display.setTextColor(COLOR_GREEN);
    display.setCursor(80, 110);
    display.print("KALIBRERING KLAR!");
    display.setCursor(90, 140);
    display.print("SPARAD!");
    
    // SHORTENED: Much faster completion (Mike's feedback)
    delay(100);  // Brief pause to show success message
    uint16_t tx, ty;
    // Quick touch release check (max 1 second)
    unsigned long touchReleaseStart = millis();
    while (display.getTouch(&tx, &ty) && (millis() - touchReleaseStart < 1000)) {
        delay(20);  // Faster checking
    }
    delay(100);  // Quick final pause
    
    // COMPLETE ARTIFACT CLEANUP
    forceCleanScreen();
    
    // Reset state and go to main screen
    calibrationStep = 0;
    currentScreen = SCREEN_SHL;
    touchActive = false;
    lastTouchTime = millis();
    scrollOffset = 0;
    
    // IMPORTANT: Reset touch state for responsive UI
    // Note: Screen will be redrawn automatically on next loop iteration
    touchPressed = false;
    touchDebounceTime = millis();
    lastTouchCheck = 0;
    
    // Force immediate display update + prevent immediate data fetch
    markDisplayDirty();
    lastFetch = millis(); // Reset data fetch timer to prevent immediate fetch
    
    Serial.println("Calibration complete - fast transition to SHL screen");
}

// Start smooth fade transition to new screen
void startFadeTransition(Screen newScreen) {
    if (newScreen != currentScreen && !isFading) {
        fadeStartTime = millis();
        fadingToScreen = newScreen;
        isFading = true;
        screenFadeAlpha = 1.0;
        markDisplayDirty();
    }
}

// Show clean positive feedback modal
void showModal(String message) {
    modalMessage = message;
    showPositiveModal = true;
    modalStartTime = millis();
    markDisplayDirty();
}

// Update modal state
void updateModal() {
    if (showPositiveModal) {
        if (millis() - modalStartTime > MODAL_DURATION) {
            showPositiveModal = false;
            markDisplayDirty();
        }
    }
}

// Draw clean minimal modal
void drawModal() {
    if (showPositiveModal) {
        // Subtle dark overlay
        display.fillRect(0, 0, 320, 240, 0x2104); // Very dark transparent
        
        // Clean centered modal card
        int modalWidth = 200;
        int modalHeight = 60;
        int x = (320 - modalWidth) / 2;
        int y = (240 - modalHeight) / 2;
        
        // Modal background with subtle shadow
        display.fillRoundRect(x + 2, y + 2, modalWidth, modalHeight, 12, 0x2104); // Shadow
        display.fillRoundRect(x, y, modalWidth, modalHeight, 12, COLOR_BG); // Main card
        display.drawRoundRect(x, y, modalWidth, modalHeight, 12, COLOR_ACCENT); // Border
        
        // Message text
        display.setFont(&fonts::lgfxJapanGothic_12);
        display.setTextColor(COLOR_TEXT);
        int textWidth = modalMessage.length() * 8; // Approximate
        int textX = x + (modalWidth - textWidth) / 2;
        display.setCursor(textX, y + modalHeight/2 - 6);
        display.print(modalMessage);
    }
}

// Update fade animation 
void updateFadeTransition() {
    if (isFading) {
        unsigned long elapsed = millis() - fadeStartTime;
        float progress = (float)elapsed / FADE_DURATION;
        
        if (progress >= 1.0) {
            // Fade complete
            isFading = false;
            screenFadeAlpha = 1.0;
            currentScreen = fadingToScreen;
            markDisplayDirty();
        } else {
            // Smooth fade curve
            screenFadeAlpha = 1.0 - (progress * 0.3); // Subtle fade, not too dramatic
            markDisplayDirty();
        }
    }
}

// Mark display for redraw (responsive UI)
void markDisplayDirty() {
    displayDirty = true;
}

void drawScreen() {
    if (currentScreen == SCREEN_CALIBRATE) {
        drawCalibrationScreen();
        return;
    }
    
    drawHeader();
    
    switch (currentScreen) {
        case SCREEN_SHL:
            drawTable(shlTeams, shlTeamCount, "SHL - Svenska Hockeyligan", COLOR_SHL);
            break;
        case SCREEN_HA:
            drawTable(haTeams, haTeamCount, "HockeyAllsvenskan", COLOR_HA);
            break;
        case SCREEN_DIV3:
            drawTable(div3Teams, div3TeamCount, "Division 3", 0x8410);
            break;
        case SCREEN_NEXT:
            drawMatches("upcoming", "Kommande matcher");
            break;
        case SCREEN_NEWS:
            drawNews();
            break;
        case SCREEN_NEWS_DETAIL:
            drawNewsDetail();
            break;
        case SCREEN_TEAM_INFO:
            drawTeamInfo();
            break;
        case SCREEN_SETTINGS:
            drawSettings();
            break;
        default:
            break;
    }
    
    drawOfflineBanner();
    drawSettingsIcon();
    
    // Draw modal on top if active
    drawModal();
}

// Smart display update - only when dirty flag is set
void updateDisplayIfNeeded() {
    if (!displayDirty) {
        return; // Skip redraw if nothing changed
    }
    
    displayDirty = false; // Clear flag
    drawScreen(); // Perform actual redraw
}

void handleCalibrationTouch() {
    static unsigned long touchStart = 0;
    static bool wasPressed = false;
    
    int16_t rx, ry;
    bool pressed = getRawTouch(rx, ry);
    
    if (pressed && !wasPressed) {
        touchStart = millis();
        wasPressed = true;
    }
    
    if (!pressed && wasPressed) {
        wasPressed = false;
        unsigned long duration = millis() - touchStart;
        
        if (duration > 3000) {
            // Long press = cancel
            currentScreen = SCREEN_SETTINGS;
            calibrationStep = 0;
            markDisplayDirty();
            return;
        }
        
        if (duration > 50 && duration < 1500) {
            // Valid tap - store the raw value
            calPoints[calibrationStep][0] = rx;
            calPoints[calibrationStep][1] = ry;
            Serial.printf("Cal point %d: %d,%d\n", calibrationStep, rx, ry);
            
            calibrationStep++;
            
            if (calibrationStep >= 4) {
                finishCalibration();
            } else {
                drawCalibrationScreen();
            }
        }
    }
}

// Process touch events and handle UI interactions  
void processTouchEvent(int x, int y) {
    // SCROLL ARROWS - Check FIRST before other touch handling
    if (x > 275) { // Right edge touch zone
        int maxItems = 0, visible = 0;
        
        // Determine max items and visible count based on current screen
        if (currentScreen == SCREEN_SHL) {
            maxItems = shlTeamCount;
            visible = VISIBLE_TEAMS;
        } else if (currentScreen == SCREEN_HA) {
            maxItems = haTeamCount;
            visible = VISIBLE_TEAMS;
        } else if (currentScreen == SCREEN_DIV3) {
            maxItems = div3TeamCount;
            visible = VISIBLE_TEAMS;
        } else if (currentScreen == SCREEN_NEXT) {
            maxItems = matchCount;
            visible = VISIBLE_MATCHES;
        } else if (currentScreen == SCREEN_NEWS) {
            maxItems = newsCount;
            visible = 7; // VISIBLE_NEWS (updated for better display)
        } else {
            return;
        }
        
        // Simple large scroll zones - split at middle
        // UP scroll: top half of right edge (skip tabs at very top)
        if (y > 50 && y < 120 && scrollOffset > 0) {
            scrollOffset--;
            markDisplayDirty();
            return;
        }
        
        // DOWN scroll: bottom half of right edge
        if (y >= 120 && y < 230 && scrollOffset + visible < maxItems) {
            scrollOffset++;
            markDisplayDirty();
            return;
        }
        
        return;
    }
    
    // Tab touch (top 28 pixels) - CRITICAL for navigation  
    if (y < 28 && currentScreen != SCREEN_SETTINGS) {
        int tab = x / 64; // 5 tabs now: SHL, HA, DIV3, NEXT, NEWS
        
        if (tab >= 0 && tab <= 4 && tab != currentScreen) {
            currentScreen = (Screen)tab;
            scrollOffset = 0;
            markDisplayDirty();
            return;
        }
    }
    
    // Settings: Calibrate button
    if (currentScreen == SCREEN_SETTINGS && y > 60 && y < 100 && x > 20 && x < 300) {
        currentScreen = SCREEN_CALIBRATE;
        calibrationStep = 0;
        markDisplayDirty();
        return;
    }
    
    // News click
    if (currentScreen == SCREEN_NEWS && x < 305 && y > 50 && y < 238) {
        int row = (y - 50) / 34;
        int newsIdx = scrollOffset + row;
        if (newsIdx >= 0 && newsIdx < newsCount) {
            selectedNewsIndex = newsIdx;
            currentScreen = SCREEN_NEWS_DETAIL;
            markDisplayDirty();
            return;
        }
    }
    
    // Team click in table views
    if ((currentScreen == SCREEN_SHL || currentScreen == SCREEN_HA || currentScreen == SCREEN_DIV3) && 
        x < 270 && y > 60 && y < 238) {
        int row = (y - 63) / 19;
        int teamIdx = scrollOffset + row;
        int maxTeams = (currentScreen == SCREEN_SHL) ? shlTeamCount : 
                       (currentScreen == SCREEN_HA) ? haTeamCount : div3TeamCount;
        
        if (teamIdx >= 0 && teamIdx < maxTeams) {
            selectedTeamIndex = teamIdx;
            selectedIsSHL = (currentScreen == SCREEN_SHL);
            previousScreen = currentScreen;
            currentScreen = SCREEN_TEAM_INFO;
            markDisplayDirty();
            return;
        }
    }
    
    // Back buttons in various screens
    if (x < 60 && y > 30 && y < 55) {
        if (currentScreen == SCREEN_TEAM_INFO) {
            currentScreen = previousScreen;
            scrollOffset = 0;
            markDisplayDirty();
        } else if (currentScreen == SCREEN_NEWS_DETAIL) {
            currentScreen = SCREEN_NEWS;
            markDisplayDirty();
        } else if (currentScreen == SCREEN_SETTINGS) {
            currentScreen = SCREEN_SHL;
            scrollOffset = 0;
            markDisplayDirty();
        }
        return;
    }
    
    // Default: mark dirty for any other touch
    markDisplayDirty();
}

// Responsive touch handler - rate-limited and debounced
void handleTouchResponsive() {
    // Rate-limited touch checking for better performance
    if (millis() - lastTouchCheck < TOUCH_CHECK_INTERVAL) {
        return;
    }
    lastTouchCheck = millis();
    
    int16_t x, y;
    bool currentTouch = display.getTouch(&x, &y);
    
    // Minimal touch debug - only when needed
    // (removed spam debug)
    
    // Debounced touch handling
    if (currentTouch && !touchPressed) {
        if (millis() - touchDebounceTime > TOUCH_DEBOUNCE) {
            touchPressed = true;
            touchDebounceTime = millis();
            
            // Process touch with direct mapping
            
            // SIMPLE FIXED MAPPING for Mike's hardware
            int screenX, screenY;
            
            // Mike's hardware: RAW 0-300, nollpunkt = övre högra
            // Simple direct mapping without complex conditions
            screenX = 319 - (x * 319 / 300);  // Invert X: x=0->319, x=300->0  
            screenY = (y * 239 / 300);        // Normal Y: y=0->0, y=300->239
            
            // Clamp to screen bounds
            if (screenX < 0) screenX = 0;
            if (screenX > 319) screenX = 319;
            if (screenY < 0) screenY = 0;  
            if (screenY > 239) screenY = 239;
            processTouchEvent(screenX, screenY);
        }
    } else if (!currentTouch && touchPressed) {
        touchPressed = false;
    }
}

// Legacy touch handler (rename to avoid conflicts)
void handleTouch() {
    if (currentScreen == SCREEN_CALIBRATE) {
        handleCalibrationTouch();
        return;
    }
    
    int x, y;
    bool touching = getCalibratedTouch(x, y);
    
    // Long press detection for settings (10 seconds)
    if (touching) {
        if (touchStartTime == 0) {
            touchStartTime = millis();
            longPressTriggered = false;
        } else if (!longPressTriggered && (millis() - touchStartTime > LONG_PRESS_TIME)) {
            // 10 second long press - go to settings
            longPressTriggered = true;
            touchStartTime = 0;
            currentScreen = SCREEN_SETTINGS;
            scrollOffset = 0;
            Serial.println("Long press -> Settings");
            markDisplayDirty();
            return;
        }
    } else {
        touchStartTime = 0;
        longPressTriggered = false;
    }
    
    if (touching && !touchActive && (millis() - lastTouchTime > TOUCH_DEBOUNCE)) {
        touchActive = true;
        lastTouchTime = millis();
        
        // Back button in team info
        if (currentScreen == SCREEN_TEAM_INFO && x < 60 && y > 30 && y < 55) {
            currentScreen = previousScreen;
            scrollOffset = 0;
            markDisplayDirty();
            return;
        }
        
        // Back button in news detail
        if (currentScreen == SCREEN_NEWS_DETAIL && x < 60 && y > 30 && y < 55) {
            currentScreen = SCREEN_NEWS;
            markDisplayDirty();
            return;
        }
        
        // Back button in settings
        if (currentScreen == SCREEN_SETTINGS && x < 60 && y > 30 && y < 55) {
            currentScreen = SCREEN_SHL;
            scrollOffset = 0;
            markDisplayDirty();
            return;
        }
        
        // Settings icon touch (bottom right corner)
        if (x > 285 && y > 215 && currentScreen != SCREEN_SETTINGS && currentScreen != SCREEN_CALIBRATE) {
            startFadeTransition(SCREEN_SETTINGS);
            scrollOffset = 0;
            showModal("Inställningar öppnade");
            return;
        }
        
        // Tab touch (top 28 pixels) - 5 tabs now
        if (y < 28 && currentScreen != SCREEN_SETTINGS) {
            int tab = x / 64;
            if (tab >= 0 && tab <= 4 && tab != currentScreen) {
                startFadeTransition((Screen)tab);
                scrollOffset = 0;
            }
            return;
        }
        
        // Settings: Calibrate button
        if (currentScreen == SCREEN_SETTINGS && y > 60 && y < 100 && x > 20 && x < 300) {
            currentScreen = SCREEN_CALIBRATE;
            calibrationStep = 0;
            markDisplayDirty();
            return;
        }
        
        // News click
        if (currentScreen == SCREEN_NEWS && x < 305 && y > 50 && y < 238) {
            int row = (y - 50) / 34;
            int newsIdx = scrollOffset + row;
            if (newsIdx >= 0 && newsIdx < newsCount) {
                selectedNewsIndex = newsIdx;
                currentScreen = SCREEN_NEWS_DETAIL;
                markDisplayDirty();
                return;
            }
        }
        
        // Team click in table views
        if ((currentScreen == SCREEN_SHL || currentScreen == SCREEN_HA || currentScreen == SCREEN_DIV3) && 
            x < 270 && y > 60 && y < 238) {
            int row = (y - 63) / 19;
            int teamIdx = scrollOffset + row;
            int maxTeams = (currentScreen == SCREEN_SHL) ? shlTeamCount : 
                           (currentScreen == SCREEN_HA) ? haTeamCount : div3TeamCount;
            
            if (teamIdx >= 0 && teamIdx < maxTeams) {
                selectedTeamIndex = teamIdx;
                selectedIsSHL = (currentScreen == SCREEN_SHL);
                previousScreen = currentScreen;
                currentScreen = SCREEN_TEAM_INFO;
                markDisplayDirty();
                return;
            }
        }
        
        // Scroll
        int maxItems, visible;
        if (currentScreen == SCREEN_NEXT) {
            maxItems = 40;
            visible = VISIBLE_MATCHES;
        } else if (currentScreen == SCREEN_NEWS) {
            maxItems = newsCount;
            visible = 7;
        } else if (currentScreen == SCREEN_SHL) {
            maxItems = shlTeamCount;
            visible = VISIBLE_TEAMS;
        } else if (currentScreen == SCREEN_HA) {
            maxItems = haTeamCount;
            visible = VISIBLE_TEAMS;
        } else if (currentScreen == SCREEN_DIV3) {
            maxItems = div3TeamCount;
            visible = VISIBLE_TEAMS;
        } else {
            return;
        }
        
        if (y < 100 && scrollOffset > 0) {
            scrollOffset--;
            markDisplayDirty();
        } else if (y > 160 && scrollOffset + visible < maxItems) {
            scrollOffset++;
            markDisplayDirty();
        }
    }
    
    if (!touching) {
        touchActive = false;
    }
}

void setup() {
    Serial.begin(115200);
    delay(300);
    
    Serial.printf("\n🏒 Hockey Panel v%s\n", FIRMWARE_VERSION);
    
    // Enable watchdog (30 second timeout)
    esp_task_wdt_init(30, true);
    esp_task_wdt_add(NULL);
    
    // Clear old calibration data and use new mapping
    clearCalibration();
    Serial.println("Old calibration cleared, using new direct mapping");
    
    display.init();
    display.setRotation(1);
    display.fillScreen(COLOR_BG);
    display.setBrightness(200);
    
    // Splash
    display.setTextColor(COLOR_TEXT);
    display.setTextSize(2);
    display.setCursor(50, 70);
    display.print("Hockey Panel");
    display.setTextSize(1);
    display.setCursor(110, 100);
    display.printf("v%s", FIRMWARE_VERSION);
    display.setCursor(60, 130);
    display.print("SHL + Allsvenskan");
    
    // Auto-calibrate if defaults are being used (likely after flash)
    Serial.printf("Touch calibration: %s [%d-%d, %d-%d]\n", 
                 touchCal.valid ? "Valid" : "Using defaults",
                 touchCal.xMin, touchCal.xMax, touchCal.yMin, touchCal.yMax);
    
    if (!touchCal.valid && touchCal.xMin == 300 && touchCal.xMax == 3800) {
        Serial.println("Default calibration detected - starting auto calibration");
        display.setTextColor(COLOR_ACCENT);
        display.setCursor(30, 160);
        display.print("Touch kalibrering startar...");
        delay(2000);
        currentScreen = SCREEN_CALIBRATE;
        calibrationStep = 0;
    }
    
    connectWiFi();
    
    ArduinoOTA.setHostname("HockeyPanel");
    ArduinoOTA.onStart([]() {
        display.fillScreen(COLOR_BG);
        display.setTextSize(2);
        display.setCursor(50, 100);
        display.print("OTA Update...");
    });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        int pct = progress * 100 / total;
        display.fillRect(50, 140, 220, 20, COLOR_BG);
        display.fillRect(50, 140, pct * 2.2, 20, COLOR_ACCENT);
        display.drawRect(50, 140, 220, 20, COLOR_TEXT);
        esp_task_wdt_reset();  // Feed watchdog during OTA
    });
    ArduinoOTA.onEnd([]() {
        display.setCursor(80, 180);
        display.print("Rebooting...");
    });
    ArduinoOTA.begin();
    
    // Always fetch data at startup
    display.setCursor(60, 190);
    display.print("Hamtar data...");
    fetchData();
    
    // Retry once if failed
    if (!connectionOK) {
        delay(2000);
        display.setCursor(60, 200);
        display.print("Forsoker igen...");
        fetchData();
    }
    delay(300);
    
    drawScreen();  // Initial screen draw
    markDisplayDirty(); // Mark for future updates  
    Serial.println("Ready!");
}

void loop() {
    esp_task_wdt_reset();  // Feed watchdog every loop
    
    ArduinoOTA.handle();
    
    // Check for serial calibration command
    if (Serial.available()) {
        String cmd = Serial.readStringUntil('\n');
        cmd.trim();
        if (cmd == "calibrate" || cmd == "cal") {
            Serial.println("Starting calibration via serial command...");
            currentScreen = SCREEN_CALIBRATE;
            calibrationStep = 0;
            markDisplayDirty();
            return;
        } else if (cmd == "status") {
            long rssi = WiFi.RSSI();
            Serial.printf("System Status:\n");
            Serial.printf("  Firmware: v%s\n", FIRMWARE_VERSION);
            Serial.printf("  WiFi: %s (RSSI: %d dBm)\n", WiFi.status() == WL_CONNECTED ? "Connected" : "Disconnected", rssi);
            Serial.printf("  IP: %s\n", WiFi.localIP().toString().c_str());
            Serial.printf("  Touch: %s [%d-%d, %d-%d]\n", touchCal.valid ? "Valid" : "Invalid",
                         touchCal.xMin, touchCal.xMax, touchCal.yMin, touchCal.yMax);
            Serial.printf("  Data loaded: %s\n", dataLoaded ? "Yes" : "No");
        } else if (cmd == "help") {
            Serial.println("Available commands:");
            Serial.println("  calibrate - Start touch calibration");
            Serial.println("  status    - Show system status");
            Serial.println("  help      - Show this help");
        }
    }
    
    // === RESPONSIVE UI FIXES ===
    
    // 🎯 CALIBRATION MODE: Only handle touch, skip all other operations
    if (currentScreen == SCREEN_CALIBRATE) {
        handleTouch(); // Use original touch handler for calibration
        updateDisplayIfNeeded(); // Only update display if needed
        delay(5);
        return; // Skip all network/data operations during calibration
    }
    
    // Normal operation (not calibrating):
    
    // 1. High priority: Responsive touch handling (50Hz)
    handleTouchResponsive();
    
    // 2. Medium priority: System maintenance
    checkWiFiConnection();  // Auto-reconnect WiFi
    
    // 3. Low priority: Smart display updates (only when dirty)
    updateFadeTransition();
    updateModal();
    updateDisplayIfNeeded();
    
    // 4. Lowest priority: Network data fetching (only when NOT calibrating)
    unsigned long interval;
    if (!connectionOK) {
        interval = FETCH_INTERVAL_ERROR;  // 15s retry on error
    } else if (liveMatch) {
        interval = FETCH_INTERVAL_LIVE;   // 30s during live
    } else {
        interval = FETCH_INTERVAL;        // 5 min normal
    }
    
    if (millis() - lastFetch > interval) {
        lastFetch = millis();
        Serial.println("Fetching data...");
        fetchData();
        // Mark display dirty instead of immediate redraw
        if (currentScreen != SCREEN_SETTINGS && currentScreen != SCREEN_TEAM_INFO) {
            markDisplayDirty();
        }
    }
    
    // Ultra-responsive 200Hz main loop
    delay(5);
}