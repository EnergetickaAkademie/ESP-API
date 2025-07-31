# ESP32 Game API Library v2.0 - Async Edition

A PlatformIO library for ESP32 boards to communicate with the WebControl game server using the optimized binary protocol with **full async support** via AsyncRequest.

## 🆕 Version 2.0 - Complete Async Rewrite

**Breaking Changes**: This version has been completely rewritten to use the new AsyncRequest library for truly asynchronous HTTP operations.

### Key Features

- 🔐 **Authentication** - JWT-based authentication with username/password  
- 📋 **Board Registration** - Automatic board registration with server
- ⚡ **Async Power Data** - Submit generation and consumption data asynchronously
- 📊 **Async Coefficients** - Poll production and consumption coefficients asynchronously
- 🔄 **Binary Protocol** - Optimized ~65% bandwidth savings vs JSON
- 🛡️ **Error Handling** - Comprehensive error handling with callbacks
- 📱 **Memory Efficient** - Uses binary packets with milliwatt precision
- 🔌 **Device Management** - Report connected power plants and consumers asynchronously
- 🤖 **Callback-Driven** - All operations support custom callback functions
- ⏰ **Non-blocking** - Main loop remains responsive during all operations
- 🔒 **HTTPS Support** - Built-in certificate bundle for secure connections

## ⚠️ Migration from V1

If you're upgrading from the previous version:

### 1. Certificate Bundle Initialization (REQUIRED)
```cpp
void setup() {
    ESPGameAPI::initCertificateBundle(); // Must call this first!
    // ... rest of setup
}
```

### 2. All Data Methods Now Async
```cpp
// Old (V1) - blocking
bool result = api.submitPowerData(production, consumption);

// New (V2) - async with callback
api.submitPowerData(production, consumption, [](bool success, const std::string& error) {
    if (success) {
        Serial.println("✅ Data submitted!");
    } else {
        Serial.println("❌ Error: " + String(error.c_str()));
    }
});
```

### 3. New Callback Types
```cpp
using AsyncCallback         = std::function<void(bool success, const std::string& error)>;
using CoefficientsCallback  = std::function<void(bool success, const std::string& error)>;
using ProductionCallback    = std::function<void(bool success, const std::vector<ProductionCoefficient>& coeffs, const std::string& error)>;
using ConsumptionValCallback = std::function<void(bool success, const std::vector<ConsumptionCoefficient>& coeffs, const std::string& error)>;
```

## Hardware Requirements

- ESP32-S3-DevKitC-1 (or compatible ESP32 board)
- WiFi connection
- USB connection for programming and monitoring

## Software Requirements

- PlatformIO IDE
- ESP32 Arduino framework
- ArduinoJson 7.x (auto-installed)

## Installation

1. Clone this repository: `git clone <repo-url>`
2. Open in PlatformIO IDE
3. See `examples/AsyncExample/` for complete usage example
4. Update WiFi credentials and server settings
5. Build and upload to ESP32

## API Reference

### Setup and Authentication

```cpp
#include "ESPGameAPI.h"

// Create API instance (baseUrl, boardName, boardType, updateInterval, pollInterval)
ESPGameAPI api("http://192.168.50.201", "MyBoard", BOARD_SOLAR, 3000, 5000);

void setup() {
    // REQUIRED: Initialize certificate bundle before any requests
    ESPGameAPI::initCertificateBundle();
    
    // Connect to WiFi first
    WiFi.begin("SSID", "password");
    while (WiFi.status() != WL_CONNECTED) delay(500);
    
    // Login (synchronous)
    if (api.login("username", "password")) {
        // Register board (synchronous)
        if (api.registerBoard()) {
            Serial.println("✅ Ready!");
        }
    }
}
```

### Power Callbacks (for automatic operation)

```cpp
// Set production power callback
api.setProductionCallback([]() -> float {
    return getSolarPowerReading(); // Your sensor reading function
});

// Set consumption power callback
api.setConsumptionCallback([]() -> float {
    return getBuildingConsumption(); // Your consumption calculation
});

// Set connected power plants callback
api.setPowerPlantsCallback([]() -> std::vector<ConnectedPowerPlant> {
    std::vector<ConnectedPowerPlant> plants;
    ConnectedPowerPlant plant;
    plant.plant_id = 1001;
    plant.set_power = getCurrentPowerOutput();
    plants.push_back(plant);
    return plants;
});

// Set connected consumers callback
api.setConsumersCallback([]() -> std::vector<ConnectedConsumer> {
    std::vector<ConnectedConsumer> consumers;
    ConnectedConsumer consumer;
    consumer.consumer_id = 2001;
    consumers.push_back(consumer);
    return consumers;
});
```

### Automatic Operation

```cpp
void loop() {
    // This automatically handles polling and data submission based on intervals
    if (api.update()) {
        Serial.println("📈 Game coefficients updated!");
        // Use api.getProductionCoefficients() and api.getConsumptionCoefficients()
    }
    
    delay(100); // Small delay to prevent overwhelming
}
```

### Manual Async Operations

#### Poll Coefficients
```cpp
api.pollCoefficients([](bool success, const std::string& error) {
    if (success) {
        Serial.println("✅ Coefficients updated!");
        // Coefficients are now available via api.getProductionCoefficients()
    } else {
        Serial.println("❌ Error: " + String(error.c_str()));
    }
});
```

#### Submit Power Data
```cpp
float production = 750.5;   // Watts
float consumption = 320.2;  // Watts

api.submitPowerData(production, consumption, [](bool success, const std::string& error) {
    if (success) {
        Serial.println("✅ Power data submitted successfully!");
    } else {
        Serial.println("❌ Failed to submit: " + String(error.c_str()));
    }
});
```

#### Get Production Values
```cpp
api.getProductionValues([](bool success, const std::vector<ProductionCoefficient>& coeffs, const std::string& error) {
    if (success) {
        Serial.println("📊 Production coefficients (" + String(coeffs.size()) + "):");
        for (const auto& coeff : coeffs) {
            Serial.println("  Source " + String(coeff.source_id) + ": " + String(coeff.coefficient, 3) + "W");
        }
    } else {
        Serial.println("❌ Error: " + String(error.c_str()));
    }
});
```

#### Get Consumption Values
```cpp
api.getConsumptionValues([](bool success, const std::vector<ConsumptionCoefficient>& coeffs, const std::string& error) {
    if (success) {
        Serial.println("📊 Consumption coefficients (" + String(coeffs.size()) + "):");
        for (const auto& coeff : coeffs) {
            Serial.println("  Building " + String(coeff.building_id) + ": " + String(coeff.consumption, 3) + "W");
        }
    } else {
        Serial.println("❌ Error: " + String(error.c_str()));
    }
});
```

#### Report Connected Devices
```cpp
// Report power plants
std::vector<ConnectedPowerPlant> plants;
ConnectedPowerPlant plant;
plant.plant_id = 1001;
plant.set_power = 500.0;
plants.push_back(plant);

api.reportConnectedPowerPlants(plants, [](bool success, const std::string& error) {
    if (success) {
        Serial.println("✅ Power plants reported!");
    } else {
        Serial.println("❌ Error: " + String(error.c_str()));
    }
});

// Report consumers
std::vector<ConnectedConsumer> consumers;
ConnectedConsumer consumer;
consumer.consumer_id = 2001;
consumers.push_back(consumer);

api.reportConnectedConsumers(consumers, [](bool success, const std::string& error) {
    if (success) {
        Serial.println("✅ Consumers reported!");
    } else {
        Serial.println("❌ Error: " + String(error.c_str()));
    }
});
```

## Available Board Types

```cpp
enum BoardType {
    BOARD_SOLAR,    // Solar panel controller
    BOARD_WIND,     // Wind turbine controller  
    BOARD_BATTERY,  // Battery management system
    BOARD_GENERIC   // Generic power board
};
```

## Status and Debug Methods

```cpp
// Check connection status
if (api.isConnected()) {
    Serial.println("✅ Fully connected and registered");
}

// Print detailed status
api.printStatus();

// Print current coefficients
api.printCoefficients();

// Get individual status
bool loggedIn = api.isGameRegistered();
bool gameActive = api.isGameActive();
```

## Configuration

```cpp
// Set custom intervals (milliseconds)
api.setUpdateInterval(5000);  // How often to submit power data
api.setPollInterval(3000);    // How often to poll coefficients
```

## Error Handling

All async operations provide error information through callbacks:

```cpp
api.submitPowerData(100, 50, [](bool success, const std::string& error) {
    if (!success) {
        if (error.find("Network error") != std::string::npos) {
            Serial.println("🌐 Network issue - will retry automatically");
        } else if (error.find("HTTP error") != std::string::npos) {
            Serial.println("🔧 Server error - check server status");
        } else {
            Serial.println("❓ Unknown error: " + String(error.c_str()));
        }
    }
});
```

## Examples

Complete examples are available in the `examples/` directory:

- `AsyncExample/` - Full async usage demonstration
- `BasicCallback/` - Simple callback examples
- `NonBlockingExample/` - Non-blocking operation patterns

## Troubleshooting

### Certificate Bundle Issues
Make sure you call `ESPGameAPI::initCertificateBundle()` before any network operations.

### Network Timeouts
The library uses 7-second timeouts for HTTP requests. If you have poor network conditions, operations may fail with timeout errors.

### Memory Usage
Async operations use slightly more memory due to callback storage. Monitor available heap if you have memory constraints.

### Debug Output
Enable debug output by adding to your build flags:
```ini
build_flags = -DESPGAMEAPI_ENABLE_SERIAL
```

## Dependencies

- ESP32 Arduino framework
- ArduinoJson 7.x (automatically installed)
- AsyncRequest.hpp (included in lib/)
- WiFi library (built-in)

## License

This project is licensed under the MIT License - see the LICENSE file for details.

## Contributing

1. Fork the repository
2. Create your feature branch (`git checkout -b feature/AmazingFeature`)
3. Commit your changes (`git commit -m 'Add some AmazingFeature'`)
4. Push to the branch (`git push origin feature/AmazingFeature`)
5. Open a Pull Request

## Changelog

### v2.0.0 (Current)
- **BREAKING**: Complete rewrite using AsyncRequest
- **BREAKING**: All data operations now async with callbacks
- **BREAKING**: Requires certificate bundle initialization
- Added comprehensive error handling
- Added detailed callback support
- Improved memory efficiency
- Added HTTPS support with certificate bundle
- Non-blocking operations throughout

### v1.x.x (Legacy)
- Original HTTPClient-based implementation
- Blocking operations
- Basic error handling
