#ifndef ESP_GAME_API_H
#define ESP_GAME_API_H

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <map>
#include <vector>
#include <functional>

// Protocol version
#define PROTOCOL_VERSION 0x01

// Special values for power data
#define POWER_NULL_VALUE 0x7FFFFFFF

// Data flags for power submission
#define FLAG_GENERATION_PRESENT 0x01
#define FLAG_CONSUMPTION_PRESENT 0x02

// Board types
enum BoardType {
    BOARD_SOLAR,
    BOARD_WIND,
    BOARD_BATTERY,
    BOARD_GENERIC
};

// Coefficient structures for production and consumption
struct ProductionCoefficient {
    uint8_t source_id;
    float coefficient;  // in watts
};

struct ConsumptionCoefficient {
    uint8_t building_id;
    float consumption;  // in watts
};

// Power plant entry for connected devices
struct ConnectedPowerPlant {
    uint32_t plant_id;
    float set_power;  // in watts
};

// Consumer entry for connected devices  
struct ConnectedConsumer {
    uint32_t consumer_id;
};

// Callback function types
typedef std::function<float()> PowerCallback;
typedef std::function<std::vector<ConnectedPowerPlant>()> PowerPlantsCallback;
typedef std::function<std::vector<ConnectedConsumer>()> ConsumersCallback;

// Binary protocol structures
struct __attribute__((packed)) PowerDataRequest {
    int32_t production;  // milliwatts, big-endian
    int32_t consumption; // milliwatts, big-endian
};

struct __attribute__((packed)) ProductionEntry {
    uint8_t source_id;
    int32_t coefficient; // milliwatts, big-endian
};

struct __attribute__((packed)) ConsumptionEntry {
    uint8_t building_id;
    int32_t consumption; // milliwatts, big-endian
};

struct __attribute__((packed)) PowerPlantEntry {
    uint32_t plant_id;    // big-endian
    int32_t set_power;    // milliwatts, big-endian
};

struct __attribute__((packed)) ConsumerEntry {
    uint32_t consumer_id; // big-endian
};

class ESPGameAPI {
private:
    String baseUrl;
    String username;
    String password;
    String token;
    String boardName;
    BoardType boardType;
    bool isLoggedIn;
    bool isRegistered;
    
    // Update timing
    unsigned long lastUpdateTime;
    unsigned long updateInterval;
    unsigned long lastPollTime;
    unsigned long pollInterval;
    
    // Callbacks for user-provided functions
    PowerCallback productionCallback;
    PowerCallback consumptionCallback;
    PowerPlantsCallback powerPlantsCallback;
    ConsumersCallback consumersCallback;
    
    // Current game state
    std::vector<ProductionCoefficient> productionCoefficients;
    std::vector<ConsumptionCoefficient> consumptionCoefficients;
    bool gameActive;
    
    HTTPClient http;
    
    // Network byte order conversion functions
    uint32_t hostToNetworkLong(uint32_t hostlong);
    uint64_t hostToNetworkLongLong(uint64_t hostlonglong);
    uint32_t networkToHostLong(uint32_t netlong);
    uint64_t networkToHostLongLong(uint64_t netlonglong);
    uint16_t networkToHostShort(uint16_t netshort);
    
    // Helper functions
    String boardTypeToString(BoardType type) const;
    bool makeHttpRequest(const String& endpoint, const uint8_t* data, size_t dataSize, uint8_t* response, size_t& responseSize);
    bool makeHttpGetRequest(const String& endpoint, uint8_t* response, size_t& responseSize);
    
    // Internal protocol functions
    bool parseProductionCoefficients(const uint8_t* data, size_t dataSize);
    bool parseConsumptionCoefficients(const uint8_t* data, size_t dataSize);
    
public:
    // Constructor
    ESPGameAPI(const String& serverUrl, const String& name, BoardType type, 
               unsigned long updateIntervalMs = 3000, unsigned long pollIntervalMs = 5000);
    
    // Authentication
    bool login(const String& user, const String& pass);
    
    // Board management
    bool registerBoard();
    bool isGameRegistered() const { return isRegistered; }
    bool isFullyConnected() const { return isConnected(); }  // Alias for clarity
    bool needsRegistration() const { return isLoggedIn && !isRegistered; }
    
    // Callback registration
    void setProductionCallback(PowerCallback callback) { productionCallback = callback; }
    void setConsumptionCallback(PowerCallback callback) { consumptionCallback = callback; }
    void setPowerPlantsCallback(PowerPlantsCallback callback) { powerPlantsCallback = callback; }
    void setConsumersCallback(ConsumersCallback callback) { consumersCallback = callback; }
    
    // Main update function - call this in Arduino loop()
    bool update();
    
    // Manual operations (can be called independently)
    bool pollCoefficients();
    bool getProductionValues();
    bool getConsumptionValues();
    bool submitPowerData(float production, float consumption);
    bool reportConnectedPowerPlants(const std::vector<ConnectedPowerPlant>& plants);
    bool reportConnectedConsumers(const std::vector<ConnectedConsumer>& consumers);
    
    // Getters for current game state
    const std::vector<ProductionCoefficient>& getProductionCoefficients() const { return productionCoefficients; }
    const std::vector<ConsumptionCoefficient>& getConsumptionCoefficients() const { return consumptionCoefficients; }
    bool isGameActive() const { return gameActive; }
    
    // Configuration
    void setUpdateInterval(unsigned long intervalMs) { updateInterval = intervalMs; }
    void setPollInterval(unsigned long intervalMs) { pollInterval = intervalMs; }
    
    // Network status
    bool isConnected() const { return WiFi.status() == WL_CONNECTED && isLoggedIn && isRegistered; }
    
    // Debug functions
    void printStatus() const;
    void printCoefficients() const;
};

#endif // ESP_GAME_API_H
