/**
 * Hockey Panel - ESP32-2432S028 "Cheap Yellow Display"
 * v1.10.0 - Fixed calibration persistence + WiFi reconnect + Watchdog
 */

#include <Arduino.h>
#include <WiFi.h>
#include <ArduinoOTA.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <esp_task_wdt.h>
#include "display_config.hpp"

#define FIRMWARE_VERSION "1.10.0"

// WiFi
const char* WIFI_SSID = "IoT";
const char* WIFI_PASS = "IoTAccess123!";
const char* API_URL = "http://192.168.1.224:3080/api/all";

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
enum Screen { SCREEN_SHL, SCREEN_HA, SCREEN_NEXT, SCREEN_NEWS, SCREEN_NEWS_DETAIL, SCREEN_TEAM_INFO, SCREEN_SETTINGS, SCREEN_CALIBRATE };
Screen currentScreen = SCREEN_SHL;
Screen previousScreen = SCREEN_SHL;

// Preferences for storing calibration
Preferences prefs;

// Touch calibration data - RELAXED validation
struct TouchCalibration {
    int16_t xMin, xMax, yMin, yMax;
    bool valid;
} touchCal = {300, 3800, 300, 3800, false};

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

// Scroll
int scrollOffset = 0;
const int VISIBLE_TEAMS = 8;
const int VISIBLE_MATCHES = 4;

// Touch
bool touchActive = false;
unsigned long lastTouchTime = 0;
const unsigned long TOUCH_DEBOUNCE = 100;

// Long press for settings
unsigned long touchStartTime = 0;
bool longPressTriggered = false;
const unsigned long LONG_PRESS_TIME = 10000;  // 10 seconds

// Calibration state
int calibrationStep = 0;
int16_t calPoints[4][2];  // Raw touch values for 4 corners

// Data loaded flag
bool dataLoaded = false;

// Forward declarations
void drawScreen();
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
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 30) {
        delay(500);
        Serial.print(".");
        attempts++;
        esp_task_wdt_reset();  // Feed watchdog
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf(" Connected! IP: %s\n", WiFi.localIP().toString().c_str());
    } else {
        Serial.println(" FAILED");
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
    }
}

void parseTeams(JsonArray arr, Team* teams, int& count, int maxCount) {
    count = 0;
    for (JsonObject t : arr) {
        if (count >= maxCount) break;
        teams[count].name = t["name"].as<String>();
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

void fetchData() {
    esp_task_wdt_reset();  // Feed watchdog
    
    if (WiFi.status() != WL_CONNECTED) {
        connectionOK = false;
        return;
    }
    
    HTTPClient http;
    http.begin(API_URL);
    http.setTimeout(8000);  // Shorter timeout
    
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
            
            connectionOK = true;
            lastSuccessfulFetch = millis();
            dataLoaded = true;
            Serial.printf("Data OK: SHL %d, HA %d, News %d\n", shlTeamCount, haTeamCount, newsCount);
        }
    } else {
        Serial.printf("HTTP error: %d\n", code);
        connectionOK = false;
    }
    http.end();
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
    display.fillRect(0, 0, 320, 28, COLOR_HEADER);
    display.setFont(&fonts::Font0);
    display.setTextSize(1);
    
    // 4 tabs: SHL, HA, NASTA, NEWS
    const char* tabs[] = {"SHL", "HA", "NASTA", "NEWS"};
    uint16_t colors[] = {COLOR_SHL, COLOR_HA, COLOR_ACCENT, COLOR_ACCENT};
    int tabWidth = 72;
    
    for (int i = 0; i < 4; i++) {
        int x = i * tabWidth;
        bool active = (i == currentScreen) || 
                      (currentScreen == SCREEN_TEAM_INFO && i == (selectedIsSHL ? 0 : 1));
        
        if (active) {
            display.fillRect(x, 0, tabWidth, 28, colors[i]);
            display.setTextColor(COLOR_TEXT);
        } else {
            display.setTextColor(COLOR_DIM);
        }
        display.setCursor(x + 8, 9);
        display.print(tabs[i]);
    }
    
    // Status dot
    int sx = 305, sy = 14;
    if (isTimedOut()) {
        display.fillCircle(sx, sy, 7, COLOR_RED);
    } else if (connectionOK) {
        display.fillCircle(sx, sy, 7, COLOR_GREEN);
    } else {
        display.fillCircle(sx, sy, 7, COLOR_ACCENT);
    }
    display.drawCircle(sx, sy, 7, COLOR_TEXT);
}

void drawOfflineBanner() {
    if (isTimedOut() && currentScreen != SCREEN_SETTINGS && currentScreen != SCREEN_CALIBRATE) {
        display.fillRect(0, 222, 320, 18, COLOR_RED);
        display.setFont(&fonts::Font0);
        display.setTextColor(COLOR_TEXT);
        display.setCursor(10, 226);
        unsigned long mins = lastSuccessfulFetch > 0 ? (millis() - lastSuccessfulFetch) / 60000 : 0;
        display.printf("OFFLINE - %lu min", mins);
    }
}

void drawTable(Team* teams, int count, const char* title, uint16_t accent) {
    display.fillRect(0, 28, 320, 194, COLOR_BG);
    display.setFont(&fonts::lgfxJapanGothic_12);
    
    display.fillRect(0, 28, 320, 16, accent);
    display.setTextColor(COLOR_TEXT);
    display.setCursor(5, 30);
    display.print(title);
    
    display.setTextColor(COLOR_ACCENT);
    display.setCursor(5, 47);
    display.print("#");
    display.setCursor(22, 47);
    display.print("LAG");
    display.setCursor(175, 47);
    display.print("S");
    display.setCursor(200, 47);
    display.print("+/-");
    display.setCursor(240, 47);
    display.print("P");
    
    display.drawLine(0, 60, 270, 60, COLOR_HEADER);
    
    if (scrollOffset > 0) {
        display.fillTriangle(285, 70, 290, 65, 295, 70, COLOR_ACCENT);
    }
    if (scrollOffset + VISIBLE_TEAMS < count) {
        display.fillTriangle(285, 210, 290, 215, 295, 210, COLOR_ACCENT);
    }
    
    int end = min(scrollOffset + VISIBLE_TEAMS, count);
    for (int i = scrollOffset; i < end; i++) {
        int row = i - scrollOffset;
        int y = 63 + row * 19;
        
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
    
    display.setFont(&fonts::Font0);
    display.setTextColor(COLOR_DIM);
    display.setCursor(275, 140);
    display.printf("%d/%d", min(scrollOffset + 1, max(1, count - VISIBLE_TEAMS + 1)), 
                   max(1, count - VISIBLE_TEAMS + 1));
}

void drawMatches(const char* filter, const char* title) {
    display.fillRect(0, 28, 320, 194, COLOR_BG);
    display.setFont(&fonts::lgfxJapanGothic_12);
    
    display.fillRect(0, 28, 320, 16, COLOR_ACCENT);
    display.setTextColor(COLOR_TEXT);
    display.setCursor(5, 30);
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
    
    if (scrollOffset > 0) {
        display.fillTriangle(295, 50, 300, 45, 305, 50, COLOR_ACCENT);
    }
    if (scrollOffset + VISIBLE_MATCHES < filtered) {
        display.fillTriangle(295, 210, 300, 215, 305, 210, COLOR_ACCENT);
    }
    
    int end = min(scrollOffset + VISIBLE_MATCHES, filtered);
    for (int i = scrollOffset; i < end; i++) {
        int row = i - scrollOffset;
        int y = 50 + row * 42;
        Match* m = &allMatches[indices[i]];
        
        display.fillRoundRect(5, y, 280, 38, 4, COLOR_HEADER);
        
        display.fillRoundRect(8, y + 3, 22, 12, 2, m->isSHL ? COLOR_SHL : COLOR_HA);
        display.setFont(&fonts::Font0);
        display.setTextColor(COLOR_TEXT);
        display.setCursor(10, y + 5);
        display.print(m->isSHL ? "SHL" : "HA");
        
        display.setFont(&fonts::lgfxJapanGothic_12);
        display.setCursor(35, y + 3);
        display.print(shortName(m->homeTeam, 12));
        display.setCursor(35, y + 20);
        display.print(shortName(m->awayTeam, 12));
        
        display.setFont(&fonts::Font0);
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
    display.fillRect(0, 28, 320, 194, COLOR_BG);
    display.setFont(&fonts::lgfxJapanGothic_12);
    
    display.fillRect(0, 28, 320, 16, COLOR_ACCENT);
    display.setTextColor(COLOR_TEXT);
    display.setCursor(5, 30);
    display.print("Senaste nyheterna");
    
    if (newsCount == 0) {
        display.setTextColor(COLOR_DIM);
        display.setCursor(80, 120);
        display.print("Inga nyheter just nu");
        return;
    }
    
    // Scroll
    const int VISIBLE_NEWS = 5;
    if (scrollOffset > 0) {
        display.fillTriangle(295, 50, 300, 45, 305, 50, COLOR_ACCENT);
    }
    if (scrollOffset + VISIBLE_NEWS < newsCount) {
        display.fillTriangle(295, 210, 300, 215, 305, 210, COLOR_ACCENT);
    }
    
    int end = min(scrollOffset + VISIBLE_NEWS, newsCount);
    for (int i = scrollOffset; i < end; i++) {
        int row = i - scrollOffset;
        int y = 50 + row * 34;
        
        // News card
        display.fillRoundRect(5, y, 305, 30, 4, COLOR_HEADER);
        
        // League badge
        bool isSHL = allNews[i].league == "SHL";
        display.fillRoundRect(8, y + 3, 28, 12, 2, isSHL ? COLOR_SHL : COLOR_HA);
        display.setFont(&fonts::Font0);
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
    
    display.setFont(&fonts::Font0);
    display.setTextColor(COLOR_DIM);
    display.setCursor(280, 140);
    display.printf("%d/%d", min(scrollOffset + 1, max(1, newsCount - VISIBLE_NEWS + 1)), 
                   max(1, newsCount - VISIBLE_NEWS + 1));
}

void drawNewsDetail() {
    if (selectedNewsIndex < 0 || selectedNewsIndex >= newsCount) {
        currentScreen = SCREEN_NEWS;
        drawScreen();
        return;
    }
    
    NewsItem* news = &allNews[selectedNewsIndex];
    
    display.fillRect(0, 28, 320, 212, COLOR_BG);
    
    // Back button
    display.fillRoundRect(5, 32, 50, 20, 3, COLOR_HEADER);
    display.setFont(&fonts::Font0);
    display.setTextColor(COLOR_TEXT);
    display.setCursor(12, 38);
    display.print("< BACK");
    
    // League badge
    bool isSHL = news->league == "SHL";
    display.fillRoundRect(65, 32, 35, 20, 3, isSHL ? COLOR_SHL : COLOR_HA);
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
    while (summary.length() > 0 && lineCount < maxLines && y < 220) {
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
    Team* team = selectedIsSHL ? &shlTeams[selectedTeamIndex] : &haTeams[selectedTeamIndex];
    
    display.fillRect(0, 28, 320, 194, COLOR_BG);
    
    display.fillRoundRect(5, 32, 50, 20, 3, COLOR_HEADER);
    display.setFont(&fonts::Font0);
    display.setTextColor(COLOR_TEXT);
    display.setCursor(12, 38);
    display.print("< BACK");
    
    uint16_t teamColor = getTeamColor(team->name);
    display.fillRect(0, 55, 320, 35, teamColor);
    display.setFont(&fonts::lgfxJapanGothic_12);
    display.setTextColor(COLOR_TEXT);
    display.setCursor(10, 62);
    display.printf("#%d %s", team->position, team->name.c_str());
    display.setCursor(10, 78);
    display.print(selectedIsSHL ? "SHL" : "HockeyAllsvenskan");
    
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
    display.fillRoundRect(5, 32, 50, 20, 3, COLOR_HEADER);
    display.setFont(&fonts::Font0);
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
    display.fillRoundRect(20, y, 280, 40, 5, COLOR_ACCENT);
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
    display.setFont(&fonts::Font0);
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
    
    display.setFont(&fonts::Font0);
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
    touchCal.xMin = (calPoints[0][0] + calPoints[3][0]) / 2;  // Left edge avg
    touchCal.xMax = (calPoints[1][0] + calPoints[2][0]) / 2;  // Right edge avg
    touchCal.yMin = (calPoints[0][1] + calPoints[1][1]) / 2;  // Top edge avg
    touchCal.yMax = (calPoints[2][1] + calPoints[3][1]) / 2;  // Bottom edge avg
    
    // Add some margin (the targets are 20px from edge)
    int xMargin = (touchCal.xMax - touchCal.xMin) * 20 / 280;
    int yMargin = (touchCal.yMax - touchCal.yMin) * 20 / 200;
    touchCal.xMin -= xMargin;
    touchCal.xMax += xMargin;
    touchCal.yMin -= yMargin;
    touchCal.yMax += yMargin;
    
    saveCalibration();
    
    Serial.printf("Calibration complete: x[%d-%d] y[%d-%d]\n", 
        touchCal.xMin, touchCal.xMax, touchCal.yMin, touchCal.yMax);
    
    display.fillScreen(COLOR_BG);
    display.setFont(&fonts::lgfxJapanGothic_12);
    display.setTextColor(COLOR_GREEN);
    display.setCursor(80, 110);
    display.print("KALIBRERING KLAR!");
    display.setCursor(90, 140);
    display.print("SPARAD!");
    
    // Wait for touch release
    delay(500);
    uint16_t tx, ty;
    while (display.getTouch(&tx, &ty)) {
        delay(50);  // Wait until finger lifted
    }
    delay(500);
    
    // Reset state and go to main screen
    calibrationStep = 0;
    currentScreen = SCREEN_SHL;
    touchActive = false;
    lastTouchTime = millis();
    
    Serial.println("Calibration complete, going to SHL screen");
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
            drawScreen();
            return;
        }
        
        if (duration > 50 && duration < 1500) {
            // Valid tap - store the raw value
            calPoints[calibrationStep][0] = rx;
            calPoints[calibrationStep][1] = ry;
            Serial.printf("Cal point %d: %d, %d\n", calibrationStep, rx, ry);
            
            calibrationStep++;
            
            if (calibrationStep >= 4) {
                finishCalibration();
            } else {
                drawCalibrationScreen();
            }
        }
    }
}

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
            drawScreen();
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
            drawScreen();
            return;
        }
        
        // Back button in news detail
        if (currentScreen == SCREEN_NEWS_DETAIL && x < 60 && y > 30 && y < 55) {
            currentScreen = SCREEN_NEWS;
            drawScreen();
            return;
        }
        
        // Back button in settings
        if (currentScreen == SCREEN_SETTINGS && x < 60 && y > 30 && y < 55) {
            currentScreen = SCREEN_SHL;
            scrollOffset = 0;
            drawScreen();
            return;
        }
        
        // Tab touch (top 28 pixels) - 4 tabs now
        if (y < 28 && currentScreen != SCREEN_SETTINGS) {
            int tab = x / 72;
            if (tab >= 0 && tab <= 3 && tab != currentScreen) {
                currentScreen = (Screen)tab;
                scrollOffset = 0;
                drawScreen();
            }
            return;
        }
        
        // Settings: Calibrate button
        if (currentScreen == SCREEN_SETTINGS && y > 60 && y < 100 && x > 20 && x < 300) {
            currentScreen = SCREEN_CALIBRATE;
            calibrationStep = 0;
            drawScreen();
            return;
        }
        
        // News click
        if (currentScreen == SCREEN_NEWS && x < 305 && y > 50 && y < 220) {
            int row = (y - 50) / 34;
            int newsIdx = scrollOffset + row;
            if (newsIdx >= 0 && newsIdx < newsCount) {
                selectedNewsIndex = newsIdx;
                currentScreen = SCREEN_NEWS_DETAIL;
                drawScreen();
                return;
            }
        }
        
        // Team click in table views
        if ((currentScreen == SCREEN_SHL || currentScreen == SCREEN_HA) && 
            x < 270 && y > 60 && y < 220) {
            int row = (y - 63) / 19;
            int teamIdx = scrollOffset + row;
            int maxTeams = (currentScreen == SCREEN_SHL) ? shlTeamCount : haTeamCount;
            
            if (teamIdx >= 0 && teamIdx < maxTeams) {
                selectedTeamIndex = teamIdx;
                selectedIsSHL = (currentScreen == SCREEN_SHL);
                previousScreen = currentScreen;
                currentScreen = SCREEN_TEAM_INFO;
                drawScreen();
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
            visible = 5;
        } else if (currentScreen == SCREEN_SHL) {
            maxItems = shlTeamCount;
            visible = VISIBLE_TEAMS;
        } else if (currentScreen == SCREEN_HA) {
            maxItems = haTeamCount;
            visible = VISIBLE_TEAMS;
        } else {
            return;
        }
        
        if (y < 100 && scrollOffset > 0) {
            scrollOffset--;
            drawScreen();
        } else if (y > 160 && scrollOffset + visible < maxItems) {
            scrollOffset++;
            drawScreen();
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
    
    // Load touch calibration
    loadCalibration();
    
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
    
    // Only force calibration if NVS is completely empty/corrupt
    if (!touchCal.valid && touchCal.xMin == 300 && touchCal.xMax == 3800) {
        display.setTextColor(COLOR_ACCENT);
        display.setCursor(20, 160);
        display.print("Forsta start - kalibrering");
        display.setCursor(60, 180);
        display.print("kravs...");
        delay(2000);
        currentScreen = SCREEN_CALIBRATE;
        calibrationStep = 0;
    } else {
        // Try to use existing calibration even if marked invalid
        if (!touchCal.valid) {
            Serial.println("Using stored values despite invalid flag");
            touchCal.valid = true;  // Trust stored values
        }
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
    
    drawScreen();
    Serial.println("Ready!");
}

void loop() {
    esp_task_wdt_reset();  // Feed watchdog every loop
    
    ArduinoOTA.handle();
    handleTouch();
    checkWiFiConnection();  // Auto-reconnect WiFi
    
    // Shorter retry on error, normal interval otherwise
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
        if (currentScreen != SCREEN_SETTINGS && currentScreen != SCREEN_CALIBRATE && 
            currentScreen != SCREEN_TEAM_INFO) {
            drawScreen();
        }
    }
    
    delay(5);
}