# ESP32 Game API Library

A PlatformIO library for ESP32 boards to communicate with the WebControl game server using the optimized binary protocol.

## Features

- 🔐 **Authentication** - JWT-based authentication with username/password
- 📋 **Board Registration** - Automatic board registration with server
- ⚡ **Power Data Submission** - Submit generation and consumption data via callbacks
- 📊 **Game Coefficients Polling** - Receive production and consumption coefficients
- 🔄 **Binary Protocol** - Optimized ~65% bandwidth savings vs JSON
- 🛡️ **Error Handling** - Robust error detection and recovery
- 📱 **Memory Efficient** - Uses binary packets with milliwatt precision
- 🔌 **Device Connection Management** - Report connected power plants and consumers
- 🤖 **Callback-Driven** - User-provided callbacks for dynamic data calculation
- ⏰ **Automatic Updates** - Periodic polling and data submission

## Hardware Requirements

- ESP32-S3-DevKitC-1 (or compatible ESP32 board)
- WiFi connection
- USB connection for programming and monitoring

## Software Requirements

- PlatformIO IDE
- ESP32 Arduino framework

## Installation

1. Clone this repository or download the source code
2. Open the project in PlatformIO IDE
3. Update WiFi credentials in `src/main.cpp`
4. Update server URL and authentication details
5. Build and upload to your ESP32 board

## Quick Start

### 1. Configure WiFi and Server

Edit `include/config.h` and update these constants:

```cpp
// WiFi credentials
#define WIFI_SSID "YOUR_WIFI_SSID"
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"

// Game server configuration
#define SERVER_URL "http://your-server.com"
#define API_USERNAME "board1"  // Your board username from server
#define API_PASSWORD "board123" // Your board password from server

// Board configuration
#define BOARD_NAME "ESP32 Solar Board"
#define BOARD_TYPE BOARD_SOLAR // BOARD_SOLAR, BOARD_WIND, BOARD_BATTERY, BOARD_GENERIC
```

### 2. Build and Upload

```bash
# Build the project
pio run

# Upload to ESP32
pio run --target upload

# Monitor serial output
pio device monitor
```

### 3. Usage Example

```cpp
#include "ESPGameAPI.h"

// Create API instance
ESPGameAPI gameAPI("http://localhost", "My Solar Board", BOARD_SOLAR);

// Callback functions
float getProductionValue() {
    // Your logic to calculate current production
    return 25.5; // watts
}

float getConsumptionValue() {
    // Your logic to calculate current consumption  
    return 12.3; // watts
}

std::vector<ConnectedPowerPlant> getConnectedPowerPlants() {
    std::vector<ConnectedPowerPlant> plants;
    plants.push_back({1001, 30.0}); // Plant ID 1001, set to 30W
    plants.push_back({1002, 45.0}); // Plant ID 1002, set to 45W
    return plants;
}

std::vector<ConnectedConsumer> getConnectedConsumers() {
    std::vector<ConnectedConsumer> consumers;
    consumers.push_back({2001}); // Consumer ID 2001
    consumers.push_back({2002}); // Consumer ID 2002
    return consumers;
}

void setup() {
    // Connect to WiFi first...
    
    // Setup callbacks
    gameAPI.setProductionCallback(getProductionValue);
    gameAPI.setConsumptionCallback(getConsumptionValue);
    gameAPI.setPowerPlantsCallback(getConnectedPowerPlants);
    gameAPI.setConsumersCallback(getConnectedConsumers);
    
    // Configure intervals
    gameAPI.setUpdateInterval(3000);  // Update every 3 seconds
    gameAPI.setPollInterval(5000);    // Poll every 5 seconds
    
    // Login and register
    if (gameAPI.login("board1", "board123")) {
        if (gameAPI.registerBoard()) {
            Serial.println("Board ready!");
        }
    }
}

void loop() {
    // Single call handles everything automatically
    gameAPI.update();
    
    delay(100);
}
```

## API Reference

### Constructor

```cpp
ESPGameAPI(const String& serverUrl, const String& name, BoardType type, 
           unsigned long updateIntervalMs = 3000, unsigned long pollIntervalMs = 5000)
```

### Authentication Methods

```cpp
bool login(const String& user, const String& pass)
```

### Board Management

```cpp
bool registerBoard()
bool isGameRegistered() const
```

### Callback Registration

```cpp
void setProductionCallback(PowerCallback callback)
void setConsumptionCallback(PowerCallback callback)  
void setPowerPlantsCallback(PowerPlantsCallback callback)
void setConsumersCallback(ConsumersCallback callback)
```

### Main Update Function

```cpp
bool update()  // Call this in Arduino loop() - handles everything automatically
```

### Manual Operations

```cpp
bool pollCoefficients()  // Get current game coefficients
bool getProductionValues()  // Get production coefficient values
bool getConsumptionValues()  // Get consumption coefficient values
bool submitPowerData(float production, float consumption)
bool reportConnectedPowerPlants(const std::vector<ConnectedPowerPlant>& plants)
bool reportConnectedConsumers(const std::vector<ConnectedConsumer>& consumers)
```

### Game State Access

```cpp
const std::vector<ProductionCoefficient>& getProductionCoefficients() const
const std::vector<ConsumptionCoefficient>& getConsumptionCoefficients() const
bool isGameActive() const
```

### Configuration

```cpp
void setUpdateInterval(unsigned long intervalMs)
void setPollInterval(unsigned long intervalMs)
```

### Status and Debug

```cpp
bool isConnected() const
void printStatus() const
void printCoefficients() const
```

## Board Types

- `BOARD_SOLAR` - Solar panel simulation
- `BOARD_WIND` - Wind turbine simulation  
- `BOARD_BATTERY` - Battery storage simulation
- `BOARD_GENERIC` - Generic power source

## Callback Function Types

```cpp
typedef std::function<float()> PowerCallback;
typedef std::function<std::vector<ConnectedPowerPlant>()> PowerPlantsCallback;
typedef std::function<std::vector<ConnectedConsumer>()> ConsumersCallback;
```

## Data Structures

```cpp
struct ProductionCoefficient {
    uint8_t source_id;
    float coefficient;  // in watts
};

struct ConsumptionCoefficient {
    uint8_t building_id;
    float consumption;  // in watts
};

struct ConnectedPowerPlant {
    uint32_t plant_id;
    float set_power;  // in watts
};

struct ConnectedConsumer {
    uint32_t consumer_id;
};
```

## Protocol Details

This library implements the binary protocol described in `ESP32_BINARY_PROTOCOL.md`:

- **Authentication**: JWT token-based authentication
- **Registration**: Empty body registration (board ID from JWT)
- **Power Data**: 8-byte packets (production + consumption in milliwatts)
- **Polling**: Variable-size coefficient responses  
- **Device Reports**: Connected power plants and consumers
- **Bandwidth Savings**: ~65% compared to JSON
- **Endianness**: Big-endian network byte order
- **Power Values**: Stored as milliwatts (watts × 1000) for precision

## Memory Usage

- **Total RAM usage**: < 500 bytes per operation (including coefficient storage)
- **Binary packet sizes**: 8 bytes for power data, variable for coefficients
- **Dynamic allocation**: Only for temporary buffers and coefficient vectors
- **Callback overhead**: Minimal (function pointers)

## Error Handling

The library includes comprehensive error handling:

- Network connectivity checks
- HTTP status code validation
- Protocol version verification
- Timeout handling
- Automatic reconnection

## Troubleshooting

### WiFi Connection Issues
```cpp
// Check WiFi status
if (WiFi.status() != WL_CONNECTED) {
    WiFi.reconnect();
}
```

### Authentication Problems
- Verify username and password in config.h
- Check server URL is correct
- Ensure server is accessible
- Board username should match JWT format (e.g., "board1", "board2")

### Registration Failures
- Verify board name length (reasonable length)
- Check board type is valid
- Ensure login was successful first

### Callback Issues
- Ensure all required callbacks are set before calling update()
- Check callback functions return valid data
- Verify callback functions don't block for too long

### Data Submission Issues
- Confirm board is registered first
- Check if game is active via isGameActive()
- Ensure callbacks return reasonable power values

## Serial Monitor Output

The example provides detailed logging:

```
🔌 ESP32 Game Board Simulator (New Binary Protocol)
===================================================
Board: ESP32-S3-DevKitC-1
Type: Solar
===================================================
📡 Connecting to WiFi...
✅ WiFi connected!
🔐 Logging in...
✅ Login successful!
📋 Registering board...
✅ Board registered successfully!
⏳ Starting automatic updates...
📊 Received 3 production and 5 consumption coefficients
🎮 Game started! Beginning automatic data submission...
� Reported 3 connected power plants
🏠 Reported 3 connected consumers
⚡ Power data submitted - Gen: 45.2W, Cons: 28.7W
📊 Game active - automatic updates running
```

## Documentation

- **[Binary Protocol Integration](BINARY_PROTOCOL_INTEGRATION.md)** - Details of the binary protocol implementation
- **[Quick Setup Guide](SETUP.md)** - Step-by-step setup instructions
- **[Project Overview](PROJECT_OVERVIEW.md)** - Detailed feature comparison and examples
- **[ESP32 Binary Protocol](../WebControl/ESP32_BINARY_PROTOCOL.md)** - Complete protocol specification

## Contributing

1. Fork the repository
2. Create a feature branch
3. Make your changes
4. Test thoroughly
5. Submit a pull request

## License

See LICENSE file for details.

## Support

For issues and questions:
1. Check the troubleshooting section
2. Review the protocol documentation
3. Open an issue on GitHub
