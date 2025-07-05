#include <WiFi.h>
#include <time.h>
#include "ESPGameAPI.h"
#include "config.h"

// Create API instance using config values
ESPGameAPI gameAPI(SERVER_URL, BOARD_ID, BOARD_NAME, BOARD_TYPE);

// Simulation variables
bool gameRunning = false;
unsigned long lastPollTime = 0;
unsigned long lastDataSubmission = 0;

// Power generation patterns based on board type
float generatePower(bool isDayTime) {
    float basePower = 0.0;
    float variation = 0.0;
    
    switch (BOARD_TYPE) {
        case BOARD_SOLAR:
            if (!isDayTime) {
                return 0.0; // No solar power at night
            }
            basePower = SOLAR_BASE_POWER;
            variation = random(-100 * SOLAR_VARIATION, 101 * SOLAR_VARIATION) / 100.0;
            break;
            
        case BOARD_WIND:
            basePower = isDayTime ? WIND_BASE_POWER_DAY : WIND_BASE_POWER_NIGHT;
            variation = random(-100 * WIND_VARIATION, 101 * WIND_VARIATION) / 100.0;
            break;
            
        case BOARD_BATTERY:
            basePower = isDayTime ? BATTERY_DISCHARGE_DAY : BATTERY_DISCHARGE_NIGHT;
            variation = random(-10, 11) / 100.0; // ±10% variation for battery
            break;
            
        case BOARD_GENERIC:
        default:
            basePower = GENERIC_BASE_POWER;
            variation = random(-30, 31) / 100.0; // ±30% variation
            break;
    }
    
    return basePower * (1.0 + variation);
}

float generateConsumption(bool isDayTime) {
    float basePower = isDayTime ? CONSUMPTION_BASE_DAY : CONSUMPTION_BASE_NIGHT;
    float variation = random(-100 * CONSUMPTION_VARIATION, 101 * CONSUMPTION_VARIATION) / 100.0;
    return basePower * (1.0 + variation);
}

void setup() {
    // Initialize Serial with longer delay for ESP32-S3
    Serial.begin(115200);
    delay(3000); // Longer delay for ESP32-S3 USB CDC
    
    // Ensure Serial is ready
    while (!Serial && millis() < 5000) {
        delay(10);
    }
    
    Serial.println();
    Serial.println("🔌 ESP32 Game Board Simulator");
    Serial.println("==============================");
    Serial.println("Board: " + String(BOARD_NAME));
    Serial.println("Type: " + String(BOARD_TYPE == BOARD_SOLAR ? "Solar" : 
                                   BOARD_TYPE == BOARD_WIND ? "Wind" : 
                                   BOARD_TYPE == BOARD_BATTERY ? "Battery" : "Generic"));
    Serial.println("ID: " + String(BOARD_ID));
    Serial.println("==============================");
    
    // Initialize random seed with ESP32 hardware RNG
    randomSeed(esp_random());
    
    // Connect to WiFi
    Serial.println("📡 Connecting to WiFi: " + String(WIFI_SSID));
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    
    unsigned long wifiStartTime = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - wifiStartTime) < WIFI_TIMEOUT_MS) {
        delay(1000);
        Serial.print(".");
    }
    
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("\n❌ WiFi connection failed! Check credentials and try again.");
        Serial.println("SSID: " + String(WIFI_SSID));
        return;
    }
    
    Serial.println();
    Serial.println("✅ WiFi connected!");
    Serial.println("IP address: " + WiFi.localIP().toString());
    Serial.println("Signal strength: " + String(WiFi.RSSI()) + " dBm");
    
    // Configure time (needed for timestamps)
    configTime(0, 0, "pool.ntp.org");
    Serial.println("⏰ Configuring time...");
    
    struct tm timeinfo;
    int timeAttempts = 0;
    while (!getLocalTime(&timeinfo) && timeAttempts < 10) {
        delay(1000);
        Serial.print(".");
        timeAttempts++;
    }
    
    if (timeAttempts >= 10) {
        Serial.println("\n⚠️ Time configuration failed, but continuing...");
    } else {
        Serial.println("\n✅ Time configured!");
    }
    
    // Login to the game
    Serial.println("🔐 Logging in to server: " + String(SERVER_URL));
    if (gameAPI.login(API_USERNAME, API_PASSWORD)) {
        Serial.println("✅ Login successful!");
        
        // Register the board
        Serial.println("📋 Registering board...");
        if (gameAPI.registerBoard()) {
            Serial.println("✅ Board registered successfully!");
            if (ENABLE_DEBUG_PRINTS) {
                gameAPI.printStatus();
            }
        } else {
            Serial.println("❌ Board registration failed!");
            return;
        }
    } else {
        Serial.println("❌ Login failed!");
        Serial.println("Check username/password and server URL");
        return;
    }
    
    Serial.println("\n⏳ Waiting for game to start...");
    Serial.println("Use the lecturer dashboard to start the game");
    Serial.println("=====================================");
}

void loop() {
    // Check WiFi connection
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("❌ WiFi disconnected! Reconnecting...");
        WiFi.reconnect();
        delay(RECONNECT_DELAY_MS);
        return;
    }
    
    unsigned long currentTime = millis();
    
    // Poll server status regularly
    if (currentTime - lastPollTime >= POLL_INTERVAL_MS) {
        lastPollTime = currentTime;
        
        uint64_t timestamp;
        uint16_t round;
        uint32_t score;
        float generation, consumption;
        uint8_t statusFlags;
        
        if (gameAPI.pollStatus(timestamp, round, score, generation, consumption, statusFlags)) {
            bool isGameActive = gameAPI.isGameActive(statusFlags);
            bool expectingData = gameAPI.isExpectingData(statusFlags);
            bool isDayTime = gameAPI.isRoundTypeDay(statusFlags);
            
            // Check if game status changed
            if (isGameActive && !gameRunning) {
                gameRunning = true;
                Serial.println("🎮 Game started!");
                Serial.println("Round: " + String(round) + " (" + (isDayTime ? "day" : "night") + ")");
                Serial.println("Score: " + String(score));
            } else if (!isGameActive && gameRunning) {
                gameRunning = false;
                Serial.println("🏁 Game finished!");
                Serial.println("Final Score: " + String(score));
            }
            
            // Display current status
            if (gameRunning) {
                if (ENABLE_DEBUG_PRINTS) {
                    Serial.print("📊 Round " + String(round) + " (" + (isDayTime ? "day" : "night") + ") ");
                    Serial.print("Score: " + String(score) + " ");
                    if (expectingData) {
                        Serial.print("⏳ Expecting data ");
                    }
                    Serial.println();
                }
                
                // Submit data if the server is expecting it
                if (expectingData && (currentTime - lastDataSubmission >= DATA_SUBMIT_INTERVAL_MS)) {
                    float genPower = generatePower(isDayTime);
                    float consPower = generateConsumption(isDayTime);
                    
                    if (gameAPI.submitPowerData(genPower, consPower)) {
                        Serial.println("✅ Data submitted for round " + String(round) + 
                                     " - Gen: " + String(genPower, 1) + "W, Cons: " + String(consPower, 1) + "W");
                        lastDataSubmission = currentTime;
                    } else {
                        Serial.println("❌ Failed to submit data for round " + String(round));
                    }
                }
            } else {
                // Still waiting for game to start
                static unsigned long lastWaitMessage = 0;
                if (currentTime - lastWaitMessage >= WAIT_MESSAGE_INTERVAL_MS) {
                    Serial.println("⏳ Still waiting for game to start...");
                    lastWaitMessage = currentTime;
                }
            }
        } else {
            Serial.println("❌ Failed to poll server status");
        }
    }
    
    delay(100); // Small delay to prevent busy waiting
}
