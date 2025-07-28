#ifndef CONFIG_H
#define CONFIG_H

// =================================
// CONFIGURATION FILE
// Update these values for your setup
// =================================

// WiFi Configuration
#define WIFI_SSID "PotkaniNora"
#define WIFI_PASSWORD "PrimaryPapikTarget"
#define WIFI_TIMEOUT_MS 30000

// Server Configuration  
#define SERVER_URL "http://192.168.2.131"
#define API_USERNAME "board1"
#define API_PASSWORD "board123"

// Board Configuration (BOARD_ID removed - now extracted from JWT token)
#define BOARD_NAME "ESP32-S3-DevKitC-1"
#define BOARD_TYPE BOARD_SOLAR  // BOARD_SOLAR, BOARD_WIND, BOARD_BATTERY, BOARD_GENERIC

// Timing Configuration
#define POLL_INTERVAL_MS 5000        // How often to poll server status (increased)
#define DATA_SUBMIT_INTERVAL_MS 3000 // How often to submit data when expected
#define RECONNECT_DELAY_MS 5000      // Delay before WiFi reconnection attempt
#define STATUS_PRINT_INTERVAL_MS 15000 // How often to print status updates

// Power Generation Configuration
#define SOLAR_BASE_POWER 45.0        // Base solar power in watts
#define SOLAR_VARIATION 0.20         // ±20% variation
#define WIND_BASE_POWER_DAY 25.0     // Wind power during day
#define WIND_BASE_POWER_NIGHT 30.0   // Wind power during night
#define WIND_VARIATION 0.50          // ±50% variation (high variability)
#define BATTERY_DISCHARGE_DAY 20.0   // Battery discharge during day
#define BATTERY_DISCHARGE_NIGHT 5.0  // Battery discharge during night
#define GENERIC_BASE_POWER 20.0      // Generic power generation

// Power Consumption Configuration
#define CONSUMPTION_BASE_DAY 25.0    // Base consumption during day
#define CONSUMPTION_BASE_NIGHT 35.0  // Base consumption during night
#define CONSUMPTION_VARIATION 0.15   // ±15% variation

// Debug Configuration
#define ENABLE_DEBUG_PRINTS true
#define WAIT_MESSAGE_INTERVAL_MS 10000  // Show waiting message every 10 seconds

#endif // CONFIG_H
