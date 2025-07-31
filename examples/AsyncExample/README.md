# ESP Game API Async Example

This example demonstrates how to use the new AsyncRequest-based ESPGameAPI library.

## Key Features

- **Fully Async Operations**: All API calls (except login/register) are now asynchronous using the AsyncRequest library
- **Callback Support**: All async operations support callback functions for handling responses
- **Certificate Bundle**: Automatically handles HTTPS certificates
- **Non-blocking**: The main loop remains responsive during API operations

## Important Changes from Previous Version

### 1. Certificate Bundle Initialization
You MUST call `ESPGameAPI::initCertificateBundle()` in your setup() function:

```cpp
void setup() {
    // REQUIRED: Initialize certificate bundle before making any requests
    ESPGameAPI::initCertificateBundle();
    
    // ... rest of setup
}
```

### 2. Async API Methods with Callbacks

All data operations are now async with optional callbacks:

```cpp
// Poll coefficients with callback
api.pollCoefficients([](bool success, const std::string& error) {
    if (success) {
        Serial.println("✅ Coefficients updated!");
    } else {
        Serial.println("❌ Error: " + String(error.c_str()));
    }
});

// Submit power data with callback
api.submitPowerData(production, consumption, [](bool success, const std::string& error) {
    if (success) {
        Serial.println("✅ Data submitted!");
    } else {
        Serial.println("❌ Error: " + String(error.c_str()));
    }
});

// Get production values with callback
api.getProductionValues([](bool success, const std::vector<ProductionCoefficient>& coeffs, const std::string& error) {
    if (success) {
        Serial.println("📊 Received " + String(coeffs.size()) + " production coefficients");
        for (const auto& coeff : coeffs) {
            Serial.println("  Source " + String(coeff.source_id) + ": " + String(coeff.coefficient) + "W");
        }
    } else {
        Serial.println("❌ Error: " + String(error.c_str()));
    }
});
```

### 3. Automatic Operation in update()

The `update()` method still works automatically in the background:

```cpp
void loop() {
    // This handles automatic polling and data submission
    if (api.update()) {
        Serial.println("📈 Coefficients updated!");
    }
}
```

## Available Async Methods

| Method | Callback Type | Description |
|--------|---------------|-------------|
| `pollCoefficients(callback)` | `CoefficientsCallback` | Get game coefficients |
| `submitPowerData(prod, cons, callback)` | `AsyncCallback` | Submit power data |
| `reportConnectedPowerPlants(plants, callback)` | `AsyncCallback` | Report connected plants |
| `reportConnectedConsumers(consumers, callback)` | `AsyncCallback` | Report connected consumers |
| `getProductionValues(callback)` | `ProductionCallback` | Get production coefficients |
| `getConsumptionValues(callback)` | `ConsumptionValCallback` | Get consumption coefficients |

## Callback Types

```cpp
using AsyncCallback         = std::function<void(bool success, const std::string& error)>;
using CoefficientsCallback  = std::function<void(bool success, const std::string& error)>;
using ProductionCallback    = std::function<void(bool success, const std::vector<ProductionCoefficient>& coeffs, const std::string& error)>;
using ConsumptionValCallback = std::function<void(bool success, const std::vector<ConsumptionCoefficient>& coeffs, const std::string& error)>;
```

## Setup

1. Update your WiFi credentials in `main.cpp`
2. Update the API base URL and board credentials
3. Build and upload to your ESP32-S3

## Dependencies

- ESP32-S3 board
- AsyncRequest.hpp library (included)
- ArduinoJson 7.x
- Certificate bundle (embedded automatically)

## Notes

- Login and register operations are still synchronous but use AsyncRequest internally
- All async operations are fire-and-forget if no callback is provided
- The library automatically handles request timeouts and error conditions
- Debug output can be enabled with `ESPGAMEAPI_ENABLE_SERIAL` build flag
