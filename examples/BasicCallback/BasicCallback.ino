/*
 * ESP32 Game API - Basic Callback Example
 * 
 * This example demonstrates how to use the ESP32 Game API with callbacks
 * for automatic data submission and device management.
 */

#include <WiFi.h>
#include "ESPGameAPI.h"

// WiFi Configuration
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";

// Server Configuration
const String serverUrl = "http://192.168.1.100";
const String username = "board1";
const String boardPassword = "board123";

// Create API instance
ESPGameAPI gameAPI(serverUrl, "Example Board", BOARD_SOLAR, 3000, 5000);

// Simulation state
float currentProduction = 0.0;
float currentConsumption = 0.0;
std::vector<ConnectedPowerPlant> myPowerPlants;
std::vector<ConnectedConsumer> myConsumers;

// Callback functions
float getProductionValue() {
    // Simulate solar power generation
    unsigned long time = millis();
    bool isDayTime = (time / 20000) % 2 == 0; // 20 second day/night cycle
    
    if (!isDayTime) {
        return 0.0; // No solar at night
    }
    
    // Simulate varying solar power during the day
    float baseProduction = 50.0;
    float variation = sin((time % 20000) * 2.0 * PI / 20000.0) * 0.3; // ±30% variation
    
    return baseProduction * (1.0 + variation);
}

float getConsumptionValue() {
    // Simulate varying consumption
    unsigned long time = millis();
    bool isDayTime = (time / 20000) % 2 == 0;
    
    float baseConsumption = isDayTime ? 25.0 : 35.0; // Higher consumption at night
    float variation = (random(-20, 21) / 100.0); // ±20% random variation
    
    return baseConsumption * (1.0 + variation);
}

std::vector<ConnectedPowerPlant> getConnectedPowerPlants() {
    // Update power plant set points
    for (auto& plant : myPowerPlants) {
        plant.set_power = random(1000, 3000) / 1000.0; // 1.0-3.0W random set power
    }
    return myPowerPlants;
}

std::vector<ConnectedConsumer> getConnectedConsumers() {
    return myConsumers;
}

void setup() {
    Serial.begin(115200);
    delay(2000);
    
    Serial.println("ESP32 Game API - Basic Callback Example");
    Serial.println("=======================================");
    
    // Initialize connected devices
    myPowerPlants.push_back({1001, 2.5}); // Power plant ID 1001
    myPowerPlants.push_back({1002, 1.8}); // Power plant ID 1002
    
    myConsumers.push_back({2001}); // Consumer ID 2001
    myConsumers.push_back({2002}); // Consumer ID 2002
    myConsumers.push_back({2003}); // Consumer ID 2003
    
    // Setup callbacks
    gameAPI.setProductionCallback(getProductionValue);
    gameAPI.setConsumptionCallback(getConsumptionValue);
    gameAPI.setPowerPlantsCallback(getConnectedPowerPlants);
    gameAPI.setConsumersCallback(getConnectedConsumers);
    
    // Connect to WiFi
    Serial.println("Connecting to WiFi...");
    WiFi.begin(ssid, password);
    
    while (WiFi.status() != WL_CONNECTED) {
        delay(1000);
        Serial.print(".");
    }
    
    Serial.println();
    Serial.println("WiFi connected!");
    Serial.println("IP: " + WiFi.localIP().toString());
    
    // Login and register
    Serial.println("Logging in...");
    if (gameAPI.login(username, boardPassword)) {
        Serial.println("Login successful!");
        
        Serial.println("Registering board...");
        if (gameAPI.registerBoard()) {
            Serial.println("Board registered successfully!");
            
            // Print initial status
            gameAPI.printStatus();
        } else {
            Serial.println("Board registration failed!");
            return;
        }
    } else {
        Serial.println("Login failed!");
        return;
    }
    
    Serial.println();
    Serial.println("Starting automatic updates...");
    Serial.println("==============================");
}

void loop() {
    // Main update - handles everything automatically
    bool updated = gameAPI.update();
    
    // Optional: Print periodic status
    static unsigned long lastStatus = 0;
    if (millis() - lastStatus > 10000) { // Every 10 seconds
        lastStatus = millis();
        
        if (gameAPI.isGameActive()) {
            Serial.println("Game active - coefficients: " + 
                         String(gameAPI.getProductionCoefficients().size()) + " prod, " +
                         String(gameAPI.getConsumptionCoefficients().size()) + " cons");
        } else {
            Serial.println("Waiting for game to start...");
        }
    }
    
    delay(100);
}
