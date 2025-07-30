# Non-Blocking ESP Game API

This updated version of the ESP Game API provides non-blocking HTTP operations, allowing your Arduino loop to continue running other important tasks while network requests are being processed.

## Key Features

- **Non-blocking HTTP requests**: Your main loop never stops for network operations
- **State machine design**: Easy to integrate into existing projects
- **Backward compatibility**: Old blocking methods still available
- **Automatic request management**: Built-in timeout and error handling
- **Memory efficient**: Minimal overhead for async operations

## Quick Start

### 1. Basic Setup

```cpp
#include "ESPGameAPI.h"

// Create API instance with timeout (optional, default 10000ms)
ESPGameAPI gameAPI("http://your-server.com", "BoardName", BOARD_GENERIC, 3000, 5000, 10000);

void setup() {
    // Set up your callbacks
    gameAPI.setProductionCallback([]() { return 25.0; });
    gameAPI.setConsumptionCallback([]() { return 15.0; });
    
    // Start async login
    gameAPI.loginAsync("username", "password");
}

void loop() {
    // This never blocks!
    gameAPI.update();
    
    // Your other code continues to run
    doImportantWork();
    delay(10);
}
```

### 2. Connection State Management

For more control, use a state machine approach:

```cpp
enum ConnectionState {
    STATE_LOGGING_IN,
    STATE_REGISTERING, 
    STATE_CONNECTED
};

ConnectionState state = STATE_LOGGING_IN;

void loop() {
    switch (state) {
        case STATE_LOGGING_IN:
            if (gameAPI.getRequestState() == REQ_IDLE) {
                if (gameAPI.isLoggedIn) {
                    gameAPI.registerBoardAsync();
                    state = STATE_REGISTERING;
                }
            }
            break;
            
        case STATE_REGISTERING:
            if (gameAPI.getRequestState() == REQ_IDLE) {
                if (gameAPI.isGameRegistered()) {
                    state = STATE_CONNECTED;
                }
            }
            break;
            
        case STATE_CONNECTED:
            gameAPI.update();  // Handles periodic polling and data submission
            break;
    }
    
    // Your other work continues regardless of network state
    doOtherWork();
}
```

## API Reference

### New Non-Blocking Methods

- `bool loginAsync(username, password)` - Start login process
- `bool registerBoardAsync()` - Start board registration
- `bool pollCoefficientsAsync()` - Start coefficient polling
- `bool submitPowerDataAsync(production, consumption)` - Submit power data
- `bool reportConnectedPowerPlantsAsync(plants)` - Report connected plants
- `bool reportConnectedConsumersAsync(consumers)` - Report connected consumers

### Request State Management

- `bool isRequestPending()` - Check if any request is in progress
- `RequestState getRequestState()` - Get current request state
  - `REQ_IDLE` - No request pending
  - `REQ_PENDING` - Request starting
  - `REQ_PROCESSING` - Request in progress  
  - `REQ_COMPLETED` - Request completed successfully
  - `REQ_ERROR` - Request failed

### Constructor Parameters

```cpp
ESPGameAPI(serverUrl, boardName, boardType, updateInterval, pollInterval, requestTimeout)
```

- `serverUrl` - Base URL of the game server
- `boardName` - Name for this board
- `boardType` - BOARD_SOLAR, BOARD_WIND, BOARD_BATTERY, or BOARD_GENERIC
- `updateInterval` - How often to submit data (default: 3000ms)
- `pollInterval` - How often to poll for coefficients (default: 5000ms) 
- `requestTimeout` - Max time to wait for any request (default: 10000ms)

## Migration from Blocking API

The old blocking methods are still available but deprecated:

| Old Blocking Method | New Non-Blocking Method |
|-------------------|------------------------|
| `login()` | `loginAsync()` |
| `registerBoard()` | `registerBoardAsync()` |
| `submitPowerData()` | `submitPowerDataAsync()` |
| `pollCoefficients()` | `pollCoefficientsAsync()` |

Simply replace the method calls and add state management to check when operations complete.

## Error Handling

The non-blocking API includes automatic timeout and error handling:

```cpp
// Check for timeouts and errors
if (gameAPI.getRequestState() == REQ_ERROR) {
    Serial.println("Request failed - will retry automatically");
}

// Or check connection status
if (!gameAPI.isFullyConnected()) {
    // Handle disconnection
}
```

## Performance Benefits

- **Never blocks**: Your loop continues running at full speed
- **Lower latency**: Immediate response to sensors and user input
- **Better reliability**: Timeouts prevent infinite hangs
- **Responsive UI**: LEDs, displays, and controls stay responsive

## Example Projects

See the `examples/NonBlockingExample/` folder for a complete working example that demonstrates:

- WiFi connection management
- Async login and registration
- Continuous sensor reading during network operations
- State machine design pattern
- Error handling and recovery

## Troubleshooting

**Q: My requests seem to timeout**  
A: Check your `requestTimeout` parameter in the constructor. Default is 10 seconds.

**Q: Can I make multiple requests at once?**  
A: No, the API processes one request at a time. Check `isRequestPending()` before starting new requests.

**Q: How do I know when a request completes?**  
A: Use `getRequestState()` or check the relevant status flags like `isLoggedIn` or `isGameRegistered()`.

**Q: What if I need the old blocking behavior?**  
A: The old methods like `login()` and `registerBoard()` are still available for backward compatibility.
