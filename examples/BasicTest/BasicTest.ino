#include <WiFi.h>
#include <time.h>
#include "ESPGameAPI.h"

// WiFi credentials - UPDATE THESE!
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";

// Server configuration - UPDATE THESE!
const String SERVER_URL = "http://localhost";
const String USERNAME = "test_board";
const String PASSWORD = "board123";

// Create API instance
ESPGameAPI gameAPI(SERVER_URL, 4001, "ESP32 Test Board", BOARD_GENERIC);

void setup() {
    Serial.begin(115200);
    delay(2000);
    
    Serial.println("🧪 ESP32 Game API Test");
    Serial.println("======================");
    
    // Connect to WiFi
    Serial.println("📡 Connecting to WiFi...");
    WiFi.begin(ssid, password);
    
    while (WiFi.status() != WL_CONNECTED) {
        delay(1000);
        Serial.print(".");
    }
    
    Serial.println();
    Serial.println("✅ WiFi connected!");
    
    // Configure time
    configTime(0, 0, "pool.ntp.org");
    struct tm timeinfo;
    while (!getLocalTime(&timeinfo)) {
        delay(1000);
        Serial.print(".");
    }
    Serial.println("✅ Time configured!");
    
    // Test login
    Serial.println("\n🔐 Testing login...");
    if (gameAPI.login(USERNAME, PASSWORD)) {
        Serial.println("✅ Login successful!");
        
        // Test registration
        Serial.println("📋 Testing registration...");
        if (gameAPI.registerBoard()) {
            Serial.println("✅ Registration successful!");
            
            // Print status
            gameAPI.printStatus();
            
            // Test power data submission
            Serial.println("\n⚡ Testing power data submission...");
            if (gameAPI.submitPowerData(25.5, 18.2)) {
                Serial.println("✅ Power data submission successful!");
            } else {
                Serial.println("❌ Power data submission failed");
            }
            
            // Test polling
            Serial.println("\n📊 Testing status polling...");
            uint64_t timestamp;
            uint16_t round;
            uint32_t score;
            float generation, consumption;
            uint8_t statusFlags;
            
            if (gameAPI.pollStatus(timestamp, round, score, generation, consumption, statusFlags)) {
                Serial.println("✅ Status polling successful!");
                Serial.println("Round: " + String(round));
                Serial.println("Score: " + String(score));
                Serial.println("Game Active: " + String(gameAPI.isGameActive(statusFlags) ? "Yes" : "No"));
                Serial.println("Expecting Data: " + String(gameAPI.isExpectingData(statusFlags) ? "Yes" : "No"));
                Serial.println("Round Type: " + String(gameAPI.isRoundTypeDay(statusFlags) ? "Day" : "Night"));
            } else {
                Serial.println("❌ Status polling failed");
            }
            
        } else {
            Serial.println("❌ Registration failed");
        }
    } else {
        Serial.println("❌ Login failed");
    }
    
    Serial.println("\n🏁 Test complete!");
}

void loop() {
    // Simple heartbeat
    static unsigned long lastHeartbeat = 0;
    if (millis() - lastHeartbeat >= 10000) {
        lastHeartbeat = millis();
        Serial.println("💓 Heartbeat - " + String(millis() / 1000) + "s");
        
        // Quick status check
        if (WiFi.status() == WL_CONNECTED && gameAPI.isConnected()) {
            Serial.println("📶 Connection: OK");
        } else {
            Serial.println("❌ Connection: Failed");
        }
    }
    
    delay(1000);
}
