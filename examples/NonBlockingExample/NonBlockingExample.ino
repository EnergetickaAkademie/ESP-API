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

// Create API instance with non-blocking support
// Constructor now accepts timeout parameter: ESPGameAPI(url, name, type, updateInterval, pollInterval, requestTimeout)
ESPGameAPI gameAPI(SERVER_URL, "ESP32 NonBlocking Board", BOARD_GENERIC, 3000, 5000, 10000);

// Connection state management
enum ConnectionState {
    STATE_DISCONNECTED,
    STATE_WIFI_CONNECTING,
    STATE_WIFI_CONNECTED,
    STATE_LOGGING_IN,
    STATE_REGISTERING,
    STATE_CONNECTED,
    STATE_ERROR
};

ConnectionState connectionState = STATE_DISCONNECTED;
unsigned long stateStartTime = 0;
unsigned long lastStatusPrint = 0;

// Example sensor callbacks for non-blocking operation
float getProductionValue() {
    // Simulate solar panel reading
    return 20.0 + random(-50, 100) / 10.0;
}

float getConsumptionValue() {
    // Simulate building consumption
    return 15.0 + random(-30, 50) / 10.0;
}

std::vector<ConnectedPowerPlant> getConnectedPowerPlants() {
    // Example: report two connected power plants
    std::vector<ConnectedPowerPlant> plants;
    plants.push_back({1001, 25.5});  // Plant ID 1001, 25.5W
    plants.push_back({1002, 18.3});  // Plant ID 1002, 18.3W
    return plants;
}

std::vector<ConnectedConsumer> getConnectedConsumers() {
    // Example: report one connected consumer
    std::vector<ConnectedConsumer> consumers;
    consumers.push_back({2001});  // Consumer ID 2001
    return consumers;
}

void setup() {
    Serial.begin(115200);
    delay(2000);
    
    Serial.println("🚀 ESP32 Non-Blocking Game API Example");
    Serial.println("=====================================");
    
    // Set up callbacks
    gameAPI.setProductionCallback(getProductionValue);
    gameAPI.setConsumptionCallback(getConsumptionValue);
    gameAPI.setPowerPlantsCallback(getConnectedPowerPlants);
    gameAPI.setConsumersCallback(getConnectedConsumers);
    
    // Start WiFi connection
    Serial.println("📡 Starting WiFi connection...");
    WiFi.begin(ssid, password);
    connectionState = STATE_WIFI_CONNECTING;
    stateStartTime = millis();
}

void loop() {
    unsigned long currentTime = millis();
    
    // Handle connection state machine (non-blocking!)
    handleConnectionStateMachine();
    
    // Once connected, let the API handle its operations (non-blocking!)
    if (connectionState == STATE_CONNECTED) {
        gameAPI.update();  // This is now non-blocking!
        
        // Check if there's a pending request and show status
        if (gameAPI.isRequestPending()) {
            // We can still do other work while request is pending
            doOtherWork();
        }
    }
    
    // Print status periodically
    if (currentTime - lastStatusPrint >= 5000) {
        lastStatusPrint = currentTime;
        printCurrentStatus();
    }
    
    // Simulate other important work that can't be blocked
    doOtherWork();
    
    // Small delay to prevent overwhelming the CPU
    delay(10);
}

void handleConnectionStateMachine() {
    unsigned long currentTime = millis();
    
    switch (connectionState) {
        case STATE_DISCONNECTED:
            // This state is only entered from setup()
            break;
            
        case STATE_WIFI_CONNECTING:
            if (WiFi.status() == WL_CONNECTED) {
                Serial.println("✅ WiFi connected!");
                Serial.println("🌐 IP Address: " + WiFi.localIP().toString());
                
                // Configure time
                configTime(0, 0, "pool.ntp.org");
                connectionState = STATE_WIFI_CONNECTED;
                stateStartTime = currentTime;
            } else if (currentTime - stateStartTime > 30000) {
                Serial.println("❌ WiFi connection timeout");
                connectionState = STATE_ERROR;
            }
            break;
            
        case STATE_WIFI_CONNECTED:
            // Wait a moment for time sync, then start login
            if (currentTime - stateStartTime > 2000) {
                Serial.println("🔐 Starting login...");
                if (gameAPI.loginAsync(USERNAME, PASSWORD)) {
                    connectionState = STATE_LOGGING_IN;
                    stateStartTime = currentTime;
                } else {
                    Serial.println("❌ Failed to start login");
                    connectionState = STATE_ERROR;
                }
            }
            break;
            
        case STATE_LOGGING_IN:
            // Check if login completed
            if (gameAPI.getRequestState() == REQ_IDLE) {
                if (gameAPI.isLoggedIn) {
                    Serial.println("✅ Login successful!");
                    Serial.println("📋 Starting registration...");
                    if (gameAPI.registerBoardAsync()) {
                        connectionState = STATE_REGISTERING;
                        stateStartTime = currentTime;
                    } else {
                        Serial.println("❌ Failed to start registration");
                        connectionState = STATE_ERROR;
                    }
                } else {
                    Serial.println("❌ Login failed");
                    connectionState = STATE_ERROR;
                }
            } else if (currentTime - stateStartTime > 15000) {
                Serial.println("❌ Login timeout");
                connectionState = STATE_ERROR;
            }
            break;
            
        case STATE_REGISTERING:
            // Check if registration completed
            if (gameAPI.getRequestState() == REQ_IDLE) {
                if (gameAPI.isGameRegistered()) {
                    Serial.println("✅ Registration successful!");
                    Serial.println("🎮 Connected to game!");
                    connectionState = STATE_CONNECTED;
                    gameAPI.printStatus();
                } else {
                    Serial.println("❌ Registration failed");
                    connectionState = STATE_ERROR;
                }
            } else if (currentTime - stateStartTime > 15000) {
                Serial.println("❌ Registration timeout");
                connectionState = STATE_ERROR;
            }
            break;
            
        case STATE_CONNECTED:
            // Check if we lost connection
            if (!gameAPI.isFullyConnected()) {
                Serial.println("❌ Lost connection to game");
                connectionState = STATE_ERROR;
            }
            break;
            
        case STATE_ERROR:
            // Wait a bit then retry
            if (currentTime - stateStartTime > 10000) {
                Serial.println("🔄 Retrying connection...");
                if (WiFi.status() == WL_CONNECTED) {
                    connectionState = STATE_WIFI_CONNECTED;
                } else {
                    WiFi.begin(ssid, password);
                    connectionState = STATE_WIFI_CONNECTING;
                }
                stateStartTime = currentTime;
            }
            break;
    }
}

void printCurrentStatus() {
    Serial.println("\n=== Status Report ===");
    Serial.println("Connection State: " + getConnectionStateString());
    Serial.println("WiFi: " + String(WiFi.status() == WL_CONNECTED ? "Connected" : "Disconnected"));
    Serial.println("Game API State: " + String(gameAPI.isFullyConnected() ? "Connected" : "Disconnected"));
    Serial.println("Request Pending: " + String(gameAPI.isRequestPending() ? "Yes" : "No"));
    Serial.println("Game Active: " + String(gameAPI.isGameActive() ? "Yes" : "No"));
    Serial.println("Free Heap: " + String(ESP.getFreeHeap()) + " bytes");
    Serial.println("===================\n");
    
    if (gameAPI.isGameActive()) {
        gameAPI.printCoefficients();
    }
}

String getConnectionStateString() {
    switch (connectionState) {
        case STATE_DISCONNECTED: return "Disconnected";
        case STATE_WIFI_CONNECTING: return "WiFi Connecting";
        case STATE_WIFI_CONNECTED: return "WiFi Connected";
        case STATE_LOGGING_IN: return "Logging In";
        case STATE_REGISTERING: return "Registering";
        case STATE_CONNECTED: return "Connected";
        case STATE_ERROR: return "Error";
        default: return "Unknown";
    }
}

void doOtherWork() {
    // Simulate other important tasks that can't be blocked
    // This could be reading sensors, updating displays, handling user input, etc.
    
    static unsigned long lastWork = 0;
    static int workCounter = 0;
    
    if (millis() - lastWork >= 1000) {
        lastWork = millis();
        workCounter++;
        
        // Example: Update an LED or display
        // digitalWrite(LED_PIN, workCounter % 2);
        
        // Example: Read sensors
        // float temperature = readTemperature();
        // float humidity = readHumidity();
        
        // This work continues regardless of network operations!
        if (workCounter % 10 == 0) {
            Serial.println("⚙️  Other work counter: " + String(workCounter) + " (continues during network ops)");
        }
    }
}
