# ESP32 Binary Protocol Integration

This document describes how the ESP32 Game API library implements the binary protocol for communication with the WebControl CoreAPI.

## Overview

The ESP-API has been completely updated to support the new binary protocol described in `ESP32_BINARY_PROTOCOL.md`. The main changes include:

- **JWT-based authentication** - Board ID extracted from JWT token
- **Binary data endpoints** - All data exchange uses binary protocol
- **Callback-driven architecture** - User provides callbacks for dynamic data
- **Automatic updates** - Single `update()` call handles all communication
- **Coefficient management** - Automatic retrieval and storage of game coefficients

## New Binary Endpoints

### 1. Authentication (JSON)
```
POST /coreapi/login
Content-Type: application/json
Body: {"username": "board1", "password": "board123"}
Response: {"token": "JWT_TOKEN", ...}
```

### 2. Board Registration (Binary)
```
POST /coreapi/register
Authorization: Bearer JWT_TOKEN
Content-Type: application/octet-stream
Body: Empty (board ID from JWT)
Response: [success_flag][message_length][message]
```

### 3. Power Data Submission (Binary)
```
POST /coreapi/post_vals
Authorization: Bearer JWT_TOKEN
Content-Type: application/octet-stream
Body: [production_mw][consumption_mw] (8 bytes total)
```

### 4. Coefficients Polling (Binary)
```
GET /coreapi/poll_binary
Authorization: Bearer JWT_TOKEN
Response: [prod_count][prod_entries...][cons_count][cons_entries...]
```

### 5. Connected Power Plants Report (Binary)
```
POST /coreapi/prod_connected
Authorization: Bearer JWT_TOKEN
Content-Type: application/octet-stream
Body: [count][plant_id][set_power]... (1 + count*8 bytes)
```

### 6. Connected Consumers Report (Binary)
```
POST /coreapi/cons_connected
Authorization: Bearer JWT_TOKEN
Content-Type: application/octet-stream
Body: [count][consumer_id]... (1 + count*4 bytes)
```

## Callback Architecture

The new API uses callbacks to allow users to provide dynamic data:

```cpp
// Power calculation callbacks
typedef std::function<float()> PowerCallback;
gameAPI.setProductionCallback([]() { return calculateProduction(); });
gameAPI.setConsumptionCallback([]() { return calculateConsumption(); });

// Device connection callbacks
typedef std::function<std::vector<ConnectedPowerPlant>()> PowerPlantsCallback;
typedef std::function<std::vector<ConnectedConsumer>()> ConsumersCallback;
gameAPI.setPowerPlantsCallback(getConnectedPowerPlants);
gameAPI.setConsumersCallback(getConnectedConsumers);
```

## Automatic Update Cycle

The `update()` function handles the complete communication cycle:

1. **Poll coefficients** (every `pollInterval` ms)
   - GET `/coreapi/poll_binary`
   - Parse production and consumption coefficients
   - Update game active status

2. **Submit data** (every `updateInterval` ms, if game active)
   - Call user callbacks to get current values
   - POST connected devices via `/coreapi/prod_connected` and `/coreapi/cons_connected`
   - POST power data via `/coreapi/post_vals`

## Data Format Details

### Power Values
- All power values stored as **milliwatts** (watts × 1000)
- Transmitted in **big-endian** format
- 32-bit signed integers (int32_t)

### Device IDs
- Power plant IDs: 32-bit unsigned integers
- Consumer IDs: 32-bit unsigned integers
- Source IDs: 8-bit unsigned integers
- Building IDs: 8-bit unsigned integers

### Binary Structures

```cpp
// Power data packet (8 bytes)
struct PowerDataRequest {
    int32_t production;  // milliwatts, big-endian
    int32_t consumption; // milliwatts, big-endian
};

// Production coefficient entry (5 bytes)
struct ProductionEntry {
    uint8_t source_id;
    int32_t coefficient; // milliwatts, big-endian
};

// Consumption coefficient entry (5 bytes)  
struct ConsumptionEntry {
    uint8_t building_id;
    int32_t consumption; // milliwatts, big-endian
};
```

## Memory Management

The library uses dynamic allocation for:
- Coefficient storage (vectors)
- Temporary binary data buffers
- Connected device lists

Static memory usage is minimal (~100 bytes base overhead).

## Error Handling

The binary protocol includes robust error handling:

- **HTTP status codes** - 200 for success, 4xx/5xx for errors
- **Response validation** - Size and format checks
- **Authentication refresh** - Re-login on 401 responses
- **Network resilience** - Automatic reconnection attempts

## Integration Example

```cpp
#include "ESPGameAPI.h"

ESPGameAPI gameAPI("http://server", "My Board", BOARD_SOLAR);

// Setup callbacks
gameAPI.setProductionCallback([]() {
    return calculateSolarProduction(); // Your logic here
});

gameAPI.setConsumptionCallback([]() {
    return calculateConsumption(); // Your logic here
});

gameAPI.setPowerPlantsCallback([]() {
    std::vector<ConnectedPowerPlant> plants;
    plants.push_back({1001, getCurrentSetPower(1001)});
    return plants;
});

void setup() {
    // WiFi connection...
    
    if (gameAPI.login("board1", "password")) {
        if (gameAPI.registerBoard()) {
            Serial.println("Ready!");
        }
    }
}

void loop() {
    // Single call handles everything
    gameAPI.update();
    delay(100);
}
```

## Migration from Old API

Key changes when migrating from the old protocol:

1. **Remove board ID** - Now extracted from JWT username
2. **Add callbacks** - Replace manual polling with callback functions  
3. **Use update()** - Replace manual `pollStatus()` + `submitPowerData()` calls
4. **Binary data** - All endpoints now use binary instead of JSON (except login)
5. **Device management** - Add power plant and consumer reporting

## Performance Benefits

- **Bandwidth**: ~65% reduction vs JSON
- **Memory**: Lower RAM usage with binary packets
- **CPU**: Reduced parsing overhead
- **Reliability**: Better error detection and recovery

## Network Byte Order

All multi-byte values use **big-endian** (network byte order):

```cpp
// Convert to network byte order before transmission
uint32_t networkValue = hostToNetworkLong(hostValue);

// Convert from network byte order after reception  
uint32_t hostValue = networkToHostLong(networkValue);
```

This ensures compatibility across different architectures.
