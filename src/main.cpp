#include <WiFi.h>
#include <time.h>
#include "ESPGameAPI.h"
#include "config.h"

// Create API instance using config values (removed BOARD_ID parameter)
ESPGameAPI gameAPI(SERVER_URL, BOARD_NAME, BOARD_TYPE);

// Simulation variables
bool gameRunning = false;
unsigned long lastStatusPrint = 0;

// Connected devices simulation
std::vector<ConnectedPowerPlant> connectedPowerPlants;
std::vector<ConnectedConsumer> connectedConsumers;

// Callback functions for the API
float getProductionValue() {
    // Calculate production based on board type
    float basePower = 0.0;
    float variation = 0.0;
    
    // Simple day/night simulation based on time
    bool isDayTime = (millis() / 10000) % 2 == 0;  // Simple 10s day/night cycle
    
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

float getConsumptionValue() {
    // Simple day/night simulation based on time
    bool isDayTime = (millis() / 10000) % 2 == 0;  // Simple 10s day/night cycle
    
    float basePower = isDayTime ? CONSUMPTION_BASE_DAY : CONSUMPTION_BASE_NIGHT;
    float variation = random(-100 * CONSUMPTION_VARIATION, 101 * CONSUMPTION_VARIATION) / 100.0;
    return basePower * (1.0 + variation);
}

std::vector<ConnectedPowerPlant> getConnectedPowerPlants() {
    // Return current connected power plants
    // Update their set power values dynamically
    for (auto& plant : connectedPowerPlants) {
        plant.set_power = random(500, 2000) / 1000.0;  // Random power between 0.5-2.0W
    }
    return connectedPowerPlants;
}

std::vector<ConnectedConsumer> getConnectedConsumers() {
    // Return current connected consumers
    return connectedConsumers;
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
    Serial.println("🔌 ESP32 Game Board Simulator (New Binary Protocol)");
    Serial.println("===================================================");
    Serial.println("Board: " + String(BOARD_NAME));
    Serial.println("Type: " + String(BOARD_TYPE == BOARD_SOLAR ? "Solar" : 
                                   BOARD_TYPE == BOARD_WIND ? "Wind" : 
                                   BOARD_TYPE == BOARD_BATTERY ? "Battery" : "Generic"));
    Serial.println("===================================================");
    
    // Initialize random seed with ESP32 hardware RNG
    randomSeed(esp_random());
    
    // Setup simulated connected devices
    // Add some power plants
    connectedPowerPlants.push_back({1001, 1.5});  // Power plant ID 1001, 1.5W
    connectedPowerPlants.push_back({1002, 2.2});  // Power plant ID 1002, 2.2W
    if (BOARD_TYPE == BOARD_SOLAR || BOARD_TYPE == BOARD_WIND) {
        connectedPowerPlants.push_back({1003, 1.8});  // Additional plant for renewable sources
    }
    
    // Add some consumers
    connectedConsumers.push_back({2001});  // Consumer ID 2001
    connectedConsumers.push_back({2002});  // Consumer ID 2002
    connectedConsumers.push_back({2003});  // Consumer ID 2003
    
    // Setup callbacks
    gameAPI.setProductionCallback(getProductionValue);
    gameAPI.setConsumptionCallback(getConsumptionValue);
    gameAPI.setPowerPlantsCallback(getConnectedPowerPlants);
    gameAPI.setConsumersCallback(getConnectedConsumers);
    
    // Configure update intervals
    gameAPI.setUpdateInterval(3000);  // Update every 3 seconds
    gameAPI.setPollInterval(5000);    // Poll every 5 seconds
    
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
    
    // Configure time (for debugging purposes)
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
            Serial.println();
            
            // Print initial status
            gameAPI.printStatus();
            Serial.println();
        } else {
            Serial.println("❌ Board registration failed!");
            return;
        }
    } else {
        Serial.println("❌ Login failed!");
        Serial.println("Check username/password and server URL");
        return;
    }
    
    Serial.println("\n⏳ Starting automatic updates...");
    Serial.println("The board will now poll for game status and submit data automatically.");
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
    
    // Call the main update function - this handles all the polling and data submission
    bool updated = gameAPI.update();
    
    unsigned long currentTime = millis();
    
    // Check if game status changed
    bool currentGameStatus = gameAPI.isGameActive();
    if (currentGameStatus != gameRunning) {
        gameRunning = currentGameStatus;
        if (gameRunning) {
            Serial.println("🎮 Game started! Beginning automatic data submission...");
            gameAPI.printCoefficients();
        } else {
            Serial.println("🏁 Game finished or inactive.");
        }
    }
    
    // Print periodic status updates
    if (currentTime - lastStatusPrint >= STATUS_PRINT_INTERVAL_MS) {
        lastStatusPrint = currentTime;
        
        if (gameRunning) {
            Serial.println("📊 Game active - automatic updates running");
            Serial.println("   Production coefficients: " + String(gameAPI.getProductionCoefficients().size()));
            Serial.println("   Consumption coefficients: " + String(gameAPI.getConsumptionCoefficients().size()));
            
            // Show example of current calculated values
            if (gameAPI.getProductionCoefficients().size() > 0) {
                float currentProduction = getProductionValue();
                float currentConsumption = getConsumptionValue();
                Serial.println("   Current values: Gen=" + String(currentProduction, 1) + "W, Cons=" + String(currentConsumption, 1) + "W");
            }
        } else {
            Serial.println("⏳ Waiting for game to start...");
        }
    }
    
    delay(100); // Small delay to prevent busy waiting
}
