# ESP32 Game API Library - Project Overview

## 🎯 What This Creates

A complete PlatformIO library for ESP32 boards to participate in your WebControl game using the optimized binary protocol. The project simulates a single board (similar to your Python script) but designed specifically for the ESP32-S3-DevKitC-1.

## 📁 Project Structure

```
ESP-API/
├── README.md                    # Comprehensive documentation
├── SETUP.md                     # Quick setup guide
├── PROTOCOL_DESCRIPTION.md      # Your original protocol docs
├── platformio.ini               # PlatformIO configuration
├── include/
│   └── config.h                 # Easy configuration file
├── src/
│   └── main.cpp                 # Main example (similar to your Python script)
├── lib/
│   └── ESPGameAPI/              # The core library
│       ├── library.properties
│       └── src/
│           ├── ESPGameAPI.h     # Library header
│           └── ESPGameAPI.cpp   # Library implementation
├── examples/
│   └── BasicTest/
│       └── BasicTest.ino        # Simple test example
└── .vscode/
    └── tasks.json              # VS Code build tasks
```

## 🚀 Key Features

### Library Features (ESPGameAPI class)
- ✅ **Binary Protocol Implementation** - Full support for your protocol
- ✅ **Authentication** - Automatic login with username/password  
- ✅ **Board Registration** - Registers with unique board ID and type
- ✅ **Power Data Submission** - Submits generation/consumption data
- ✅ **Game Status Polling** - Monitors rounds and game state
- ✅ **Memory Efficient** - Uses fixed-size binary packets (~400 bytes total)
- ✅ **Error Handling** - Robust network and protocol error handling

### Example Features (main.cpp)
- ✅ **WiFi Management** - Automatic connection and reconnection
- ✅ **Power Simulation** - Realistic patterns based on board type
- ✅ **Game Loop** - Waits for game start, responds to rounds
- ✅ **Visual Feedback** - Rich serial output with emojis and status

## 🎮 Board Simulation Behavior

Just like your Python script, the ESP32 will:

1. **Connect & Authenticate** - Login to server and register board
2. **Wait for Game Start** - Poll until game becomes active  
3. **Respond to Rounds** - Submit power data when server expects it
4. **Power Patterns** - Generate realistic solar/wind/battery/generic patterns
5. **Real-time Status** - Show round changes, scores, and data submissions

## 🔧 Easy Configuration

Everything is configured in `include/config.h`:

```cpp
// WiFi Configuration
#define WIFI_SSID "YOUR_WIFI_SSID"
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"

// Server Configuration  
#define SERVER_URL "http://localhost"
#define API_USERNAME "test_board"
#define API_PASSWORD "board123"

// Board Configuration
#define BOARD_ID 3001
#define BOARD_NAME "ESP32-S3-DevKitC-1"
#define BOARD_TYPE BOARD_SOLAR  // SOLAR, WIND, BATTERY, GENERIC
```

## 🛠️ How to Use

### Quick Start
1. **Update Config**: Edit `include/config.h` with your settings
2. **Build & Upload**: Use VS Code PlatformIO or command line
3. **Monitor**: Watch serial output for connection and game status
4. **Start Game**: Use lecturer dashboard to start the game
5. **Watch**: Board automatically responds to rounds

### VS Code Tasks (Ctrl+Shift+P → "Tasks: Run Task")
- **PlatformIO: Build** - Compile the project
- **PlatformIO: Upload** - Flash to ESP32
- **PlatformIO: Monitor** - View serial output
- **PlatformIO: Upload and Monitor** - Flash and monitor

### Command Line
```bash
# Build
pio run

# Upload to board
pio run --target upload

# Monitor serial output  
pio device monitor

# Combined upload and monitor
pio run --target upload && pio device monitor
```

## 📊 Expected Serial Output

```
🔌 ESP32 Game Board Simulator
==============================
Board: ESP32-S3-DevKitC-1
Type: Solar
ID: 3001
==============================
📡 Connecting to WiFi: YOUR_WIFI_SSID
✅ WiFi connected!
IP address: 192.168.1.105
⏰ Configuring time...
✅ Time configured!
🔐 Logging in to server: http://localhost
✅ Login successful!
📋 Registering board...
✅ Board registered successfully!

⏳ Waiting for game to start...
Use the lecturer dashboard to start the game
=====================================

🎮 Game started!
🔄 New round detected: 1 (day)
✅ Data submitted for round 1 - Gen: 43.2W, Cons: 28.1W
🔄 New round detected: 2 (night)  
✅ Data submitted for round 2 - Gen: 0.0W, Cons: 31.7W
🏁 Game finished!
Final Score: 1250
```

## 🔄 Comparison with Python Script

| Feature | Python Script | ESP32 Library |
|---------|---------------|---------------|
| **Protocol** | JSON endpoints | Binary protocol |
| **Bandwidth** | ~285 bytes | ~99 bytes (65% savings) |
| **Boards** | Multiple simultaneous | Single board per ESP32 |
| **Hardware** | Computer required | Standalone ESP32 |
| **Power** | AC powered | USB/battery powered |
| **Real-time** | High accuracy | Good accuracy |
| **Setup** | Python environment | PlatformIO upload |

## 🎯 Use Cases

- **Demo/Testing** - Single board simulation for development
- **Portable Demo** - Battery-powered ESP32 for presentations  
- **Distributed Setup** - Multiple ESP32s for realistic network
- **Student Projects** - Individual boards for each student
- **Protocol Testing** - Validate binary protocol implementation

## 🚀 Next Steps

1. **Test Setup**: Flash one ESP32 and verify it works with your server
2. **Scale Up**: Flash multiple ESP32s with different board IDs/types
3. **Customize**: Modify power patterns in `main.cpp` for different behaviors
4. **Extend**: Add sensors, displays, or physical controls to the ESP32
5. **Deploy**: Use for actual game sessions with students

The library provides the same functionality as your Python simulator but optimized for ESP32 hardware with significant bandwidth savings through the binary protocol! 🎉
