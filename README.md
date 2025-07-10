# ESP32 Game API Library

A PlatformIO library for ESP32 boards to communicate with the WebControl game server using the optimized binary protocol.

## Features

- 🔐 **Authentication** - Automatic login with username/password
- 📋 **Board Registration** - Register board with game server
- ⚡ **Power Data Submission** - Submit generation and consumption data
- 📊 **Game Status Polling** - Monitor game state and rounds
- 🔄 **Binary Protocol** - Optimized ~65% bandwidth savings vs JSON
- 🛡️ **Error Handling** - Robust error detection and recovery
- 📱 **Memory Efficient** - Uses fixed-size binary packets
- 🏢 **Building Consumption Management** - Downloads and caches building power consumption tables

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

Edit `src/main.cpp` and update these constants:

```cpp
// WiFi credentials
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";

// Game server configuration
const String SERVER_URL = "http://your-server.com"; // Change to your server URL
const String USERNAME = "your_username";
const String PASSWORD = "your_password";

// Board configuration
const uint32_t BOARD_ID = 3001; // Unique board ID
const String BOARD_NAME = "ESP32 Solar Board";
const BoardType BOARD_TYPE = BOARD_SOLAR; // BOARD_SOLAR, BOARD_WIND, BOARD_BATTERY, BOARD_GENERIC
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
ESPGameAPI gameAPI("http://localhost", 3001, "My Board", BOARD_SOLAR);

void setup() {
    // Connect to WiFi first...
    
    // Login and register
    if (gameAPI.login("username", "password")) {
        if (gameAPI.registerBoard()) {
            Serial.println("Board ready!");
        }
    }
}

void loop() {
    // Poll game status
    uint64_t timestamp;
    uint16_t round;
    uint32_t score;
    float generation, consumption;
    uint8_t statusFlags;
    
    if (gameAPI.pollStatus(timestamp, round, score, generation, consumption, statusFlags)) {
        bool isGameActive = gameAPI.isGameActive(statusFlags);
        bool expectingData = gameAPI.isExpectingData(statusFlags);
        bool isDayTime = gameAPI.isRoundTypeDay(statusFlags);
        
        if (isGameActive && expectingData) {
            // Submit power data
            float gen = calculateGeneration(isDayTime);
            float cons = calculateConsumption(isDayTime);
            gameAPI.submitPowerData(gen, cons);
        }
    }
    
    delay(2000); // Poll every 2 seconds
}
```

## API Reference

### Constructor

```cpp
ESPGameAPI(const String& serverUrl, uint32_t id, const String& name, BoardType type)
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

### Game Operations

```cpp
bool submitPowerData(float generation, float consumption)
bool submitPowerData(float generation, float consumption, uint8_t flags)

bool pollStatus(uint64_t& timestamp, uint16_t& round, uint32_t& score, 
               float& generation, float& consumption, uint8_t& statusFlags)
```

### Status Helpers

```cpp
bool isGameActive(uint8_t statusFlags) const
bool isExpectingData(uint8_t statusFlags) const
bool isRoundTypeDay(uint8_t statusFlags) const
uint16_t getLastRound() const
bool isConnected() const
```

### Debug

```cpp
void printStatus() const
```

## Board Types

- `BOARD_SOLAR` - Solar panel simulation
- `BOARD_WIND` - Wind turbine simulation  
- `BOARD_BATTERY` - Battery storage simulation
- `BOARD_GENERIC` - Generic power source

## Protocol Details

This library implements the binary protocol described in `PROTOCOL_DESCRIPTION.md`:

- **Registration**: 53-byte fixed packets
- **Power Data**: 22-byte fixed packets
- **Polling**: 24-byte responses
- **Bandwidth Savings**: ~65% compared to JSON
- **Endianness**: Big-endian network byte order
- **Timestamps**: Unix timestamps (64-bit)
- **Power Values**: Stored as watts × 100 for precision

## Memory Usage

- **Total RAM usage**: < 400 bytes per operation
- **Fixed packet sizes**: Predictable memory consumption
- **No dynamic allocation**: Uses stack-based structures

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
- Verify username and password
- Check server URL is correct
- Ensure server is accessible

### Registration Failures
- Verify board ID is unique
- Check board name length (max 31 chars)
- Ensure board type is valid

### Data Submission Issues
- Confirm board is registered first
- Check if game is active
- Verify server is expecting data

## Serial Monitor Output

The example provides detailed logging:

```
🔌 ESP32 Game Board Simulator
📡 Connecting to WiFi...
✅ WiFi connected!
🔐 Logging in...
✅ Login successful!
📋 Registering board...
✅ Board registered successfully!
⏳ Waiting for game to start...
🎮 Game started!
🔄 New round detected: 1 (day)
⚡ Power data submitted - Gen: 45.2W, Cons: 28.7W
✅ Data submitted for round 1
```

## Documentation

- **[Quick Setup Guide](SETUP.md)** - Step-by-step setup instructions
- **[Protocol Description](PROTOCOL_DESCRIPTION.md)** - Binary protocol documentation
- **[Building Consumption Management](../BUILDING_CONSUMPTION_MANAGEMENT.md)** - Building power consumption table management
- **[Project Overview](PROJECT_OVERVIEW.md)** - Detailed feature comparison and examples

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
