#include <Arduino.h>
#include <WiFi.h>
#include "ESPGameAPI.h"

// WiFi credentials
const char* WIFI_SSID = "YourWiFiSSID";
const char* WIFI_PASSWORD = "YourWiFiPassword";

// API configuration
const String API_BASE_URL = "http://192.168.50.201";
const String BOARD_USERNAME = "board1";
const String BOARD_PASSWORD = "board123";

// Create API instance
ESPGameAPI api(API_BASE_URL, "MyBoard", BOARD_SOLAR, 3000, 5000);

// Example power generation function
float getProductionPower() {
    // Simulate solar panel power based on time of day
    unsigned long currentTime = millis();
    float sineWave = sin(currentTime / 10000.0) + 1.0; // 0-2 range
    return sineWave * 500.0; // 0-1000W range
}

// Example power consumption function
float getConsumptionPower() {
    // Simulate building consumption
    return 200.0 + random(0, 100); // 200-300W range
}

// Example connected power plants function
std::vector<ConnectedPowerPlant> getConnectedPowerPlants() {
    std::vector<ConnectedPowerPlant> plants;
    
    // Example: one solar panel
    ConnectedPowerPlant plant;
    plant.plant_id = 1001;
    plant.set_power = getProductionPower();
    plants.push_back(plant);
    
    return plants;
}

// Example connected consumers function
std::vector<ConnectedConsumer> getConnectedConsumers() {
    std::vector<ConnectedConsumer> consumers;
    
    // Example: one building
    ConnectedConsumer consumer;
    consumer.consumer_id = 2001;
    consumers.push_back(consumer);
    
    return consumers;
}

void setup() {
    Serial.begin(115200);
    Serial.println("🚀 Starting ESP Game API Async Example");
    
    // Initialize certificate bundle (REQUIRED!)
    ESPGameAPI::initCertificateBundle();
    
    // Connect to WiFi
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    Serial.print("📶 Connecting to WiFi");
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println();
    Serial.println("✅ WiFi connected!");
    Serial.println("🌐 IP address: " + WiFi.localIP().toString());
    
    // Set up power callbacks
    api.setProductionCallback(getProductionPower);
    api.setConsumptionCallback(getConsumptionPower);
    api.setPowerPlantsCallback(getConnectedPowerPlants);
    api.setConsumersCallback(getConnectedConsumers);
    
    // Login (synchronous)
    Serial.println("🔐 Logging in...");
    if (api.login(BOARD_USERNAME, BOARD_PASSWORD)) {
        Serial.println("✅ Login successful!");
        
        // Register board (synchronous)
        Serial.println("📋 Registering board...");
        if (api.registerBoard()) {
            Serial.println("✅ Board registration successful!");
            api.printStatus();
        } else {
            Serial.println("❌ Board registration failed!");
        }
    } else {
        Serial.println("❌ Login failed!");
    }
    
    // Example of manual async operations with callbacks
    
    // Get production values with callback
    api.getProductionValues([](bool success, const std::vector<ProductionCoefficient>& coeffs, const std::string& error) {
        if (success) {
            Serial.println("📊 Production coefficients received:");
            for (const auto& coeff : coeffs) {
                Serial.println("  Source " + String(coeff.source_id) + ": " + String(coeff.coefficient, 3) + "W");
            }
        } else {
            Serial.println("❌ Failed to get production values: " + String(error.c_str()));
        }
    });
    
    // Get consumption values with callback
    api.getConsumptionValues([](bool success, const std::vector<ConsumptionCoefficient>& coeffs, const std::string& error) {
        if (success) {
            Serial.println("📊 Consumption coefficients received:");
            for (const auto& coeff : coeffs) {
                Serial.println("  Building " + String(coeff.building_id) + ": " + String(coeff.consumption, 3) + "W");
            }
        } else {
            Serial.println("❌ Failed to get consumption values: " + String(error.c_str()));
        }
    });
    
    // Poll coefficients with callback
    api.pollCoefficients([](bool success, const std::string& error) {
        if (success) {
            Serial.println("✅ Coefficients polling successful!");
        } else {
            Serial.println("❌ Coefficients polling failed: " + String(error.c_str()));
        }
    });
    
    Serial.println("🔄 Setup complete - starting main loop");
}

void loop() {
    // The main update loop - this handles automatic polling and data submission
    if (api.update()) {
        Serial.println("📈 Coefficients updated!");
        api.printCoefficients();
    }
    
    // Add some manual operations with callbacks every 30 seconds
    static unsigned long lastManualOperation = 0;
    if (millis() - lastManualOperation > 30000) {
        lastManualOperation = millis();
        
        Serial.println("🔧 Performing manual operations...");
        
        // Manual power data submission with callback
        api.submitPowerData(getProductionPower(), getConsumptionPower(), [](bool success, const std::string& error) {
            if (success) {
                Serial.println("✅ Manual power data submitted!");
            } else {
                Serial.println("❌ Manual power data submission failed: " + String(error.c_str()));
            }
        });
        
        // Manual power plants report with callback
        api.reportConnectedPowerPlants(getConnectedPowerPlants(), [](bool success, const std::string& error) {
            if (success) {
                Serial.println("✅ Manual power plants reported!");
            } else {
                Serial.println("❌ Manual power plants report failed: " + String(error.c_str()));
            }
        });
    }
    
    delay(100); // Small delay to prevent overwhelming the system
}
