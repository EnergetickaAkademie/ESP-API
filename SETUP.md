# Quick Setup Guide

## Step 1: Hardware Setup
1. Connect your ESP32-S3-DevKitC-1 to your computer via USB
2. Ensure the board is powered and recognized by your system

## Step 2: Software Setup
1. Install PlatformIO IDE (VS Code extension recommended)
2. Clone or download this repository
3. Open the project in PlatformIO

## Step 3: Configuration
1. Edit `include/config.h` and update:
   - `WIFI_SSID` - Your WiFi network name
   - `WIFI_PASSWORD` - Your WiFi password
   - `SERVER_URL` - Your game server URL (e.g., "http://192.168.1.100")
   - `API_USERNAME` - Your game username
   - `API_PASSWORD` - Your game password
   - `BOARD_ID` - Unique ID for this board (e.g., 3001)
   - `BOARD_NAME` - Descriptive name for your board
   - `BOARD_TYPE` - Type of board (BOARD_SOLAR, BOARD_WIND, BOARD_BATTERY, BOARD_GENERIC)

## Step 4: Build and Upload
1. In VS Code with PlatformIO:
   - Press `Ctrl+Shift+P` and run "Tasks: Run Task"
   - Select "PlatformIO: Upload and Monitor"
   
   Or use the command line:
   ```bash
   pio run --target upload
   pio device monitor
   ```

## Step 5: Monitor Output
Watch the serial monitor for output like:
```
🔌 ESP32 Game Board Simulator
📡 Connecting to WiFi...
✅ WiFi connected!
🔐 Logging in...
✅ Login successful!
📋 Registering board...
✅ Board registered successfully!
⏳ Waiting for game to start...
```

## Troubleshooting

### WiFi Connection Issues
- Double-check SSID and password in config.h
- Ensure your WiFi network is 2.4GHz (ESP32 doesn't support 5GHz)
- Check signal strength near your router

### Login/Registration Issues
- Verify server URL is correct and accessible
- Check username and password
- Ensure the server is running and reachable from your network

### Upload Issues
- Make sure the correct board is selected in platformio.ini
- Try pressing the BOOT button while uploading
- Check USB cable and drivers

### Serial Monitor Not Working
- Verify the correct COM port is selected
- Check baud rate is set to 115200
- Try reconnecting the USB cable

## Power Patterns

The library simulates different power generation patterns based on board type:

- **Solar**: High generation during day (45W base), zero at night
- **Wind**: Variable generation (25-30W base), higher variability
- **Battery**: Discharge pattern (20W day, 5W night)
- **Generic**: Consistent generation with moderate variation (20W base)

Consumption patterns vary between 25-35W with time-of-day variations.

## Next Steps

1. Start the game from the lecturer dashboard
2. Watch your board automatically respond to round changes
3. Monitor power generation and consumption data
4. View scores and game progress in real-time

## Configuration Examples

### Solar Farm Configuration
```cpp
#define BOARD_ID 3001
#define BOARD_NAME "Solar Farm Alpha"
#define BOARD_TYPE BOARD_SOLAR
```

### Wind Turbine Configuration
```cpp
#define BOARD_ID 3002
#define BOARD_NAME "Wind Turbine Beta"
#define BOARD_TYPE BOARD_WIND
```

### Battery Storage Configuration
```cpp
#define BOARD_ID 3003
#define BOARD_NAME "Battery Storage Gamma"
#define BOARD_TYPE BOARD_BATTERY
```

## Multiple Boards

To run multiple boards:
1. Use different BOARD_ID values for each board
2. Flash different ESP32 boards with different configurations
3. Each board will operate independently

## Support

- Check the main README.md for detailed API documentation
- Review PROTOCOL_DESCRIPTION.md for protocol details
- Open GitHub issues for bugs or questions
