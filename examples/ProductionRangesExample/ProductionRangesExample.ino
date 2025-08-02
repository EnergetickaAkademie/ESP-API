#include <WiFi.h>
#include <ESPGameAPI.h>

// WiFi credentials
const char* ssid = "your_wifi_ssid";
const char* password = "your_wifi_password";

// Game API configuration
const String SERVER_URL = "https://your-server.com";
const String BOARD_USERNAME = "your_board_username";
const String BOARD_PASSWORD = "your_board_password";

// Create API instance
ESPGameAPI gameAPI(SERVER_URL, "solar_panel_01", BOARD_SOLAR);

void setup() {
    Serial.begin(115200);
    
    // Initialize certificate bundle
    ESPGameAPI::initCertificateBundle();
    
    // Connect to WiFi
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(1000);
        Serial.println("Connecting to WiFi...");
    }
    Serial.println("WiFi connected!");
    
    // Login and register with the game server
    if (gameAPI.login(BOARD_USERNAME, BOARD_PASSWORD)) {
        Serial.println("Login successful!");
        
        if (gameAPI.registerBoard()) {
            Serial.println("Board registration successful!");
        } else {
            Serial.println("Board registration failed!");
        }
    } else {
        Serial.println("Login failed!");
    }
    
    // Set callbacks for power data
    gameAPI.setProductionCallback([]() -> float {
        // Your code to read current production (in Watts)
        return 150.5; // Example: 150.5W production
    });
    
    gameAPI.setConsumptionCallback([]() -> float {
        // Your code to read current consumption (in Watts)
        return 0.0; // Solar panels don't consume power
    });
}

void loop() {
    // Update the API (handles polling and data submission)
    if (gameAPI.update()) {
        Serial.println("Game coefficients updated!");
        gameAPI.printCoefficients();
    }
    
    // Example: Get production ranges every 30 seconds
    static unsigned long lastRangeCheck = 0;
    if (millis() - lastRangeCheck > 30000) {
        lastRangeCheck = millis();
        
        gameAPI.getProductionRanges([](bool success, const std::vector<ProductionRange>& ranges, const std::string& error) {
            if (success) {
                Serial.println("=== Production Ranges ===");
                for (const auto& range : ranges) {
                    Serial.printf("Source %d: %.1fW - %.1fW\n", 
                                  range.source_id, range.min_power, range.max_power);
                }
                Serial.println("========================");
            } else {
                Serial.println("Failed to get production ranges: " + String(error.c_str()));
            }
        });
    }
    
    delay(1000);
}
