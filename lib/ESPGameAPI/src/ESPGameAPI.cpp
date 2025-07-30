#include "ESPGameAPI.h"

// Constructor
ESPGameAPI::ESPGameAPI(const String& serverUrl, const String& name, BoardType type, 
                       unsigned long updateIntervalMs, unsigned long pollIntervalMs,
                       unsigned long requestTimeoutMs) 
    : baseUrl(serverUrl), boardName(name), boardType(type), 
      isLoggedIn(false), isRegistered(false), lastUpdateTime(0), lastPollTime(0),
      updateInterval(updateIntervalMs), pollInterval(pollIntervalMs), gameActive(false),
      currentRequestState(REQ_IDLE), currentRequestType(REQ_NONE), requestStartTime(0),
      requestTimeout(requestTimeoutMs), pendingRequestData(nullptr), pendingRequestDataSize(0),
      pendingIsPost(false), responseSize(0) {
}

// Network byte order conversion functions
uint32_t ESPGameAPI::hostToNetworkLong(uint32_t hostlong) {
    return ((hostlong & 0xFF000000) >> 24) |
           ((hostlong & 0x00FF0000) >> 8)  |
           ((hostlong & 0x0000FF00) << 8)  |
           ((hostlong & 0x000000FF) << 24);
}

uint64_t ESPGameAPI::hostToNetworkLongLong(uint64_t hostlonglong) {
    return ((uint64_t)hostToNetworkLong((uint32_t)hostlonglong) << 32) | hostToNetworkLong((uint32_t)(hostlonglong >> 32));
}

uint32_t ESPGameAPI::networkToHostLong(uint32_t netlong) {
    return hostToNetworkLong(netlong); // Same operation for conversion back
}

uint64_t ESPGameAPI::networkToHostLongLong(uint64_t netlonglong) {
    return hostToNetworkLongLong(netlonglong); // Same operation for conversion back
}

uint16_t ESPGameAPI::networkToHostShort(uint16_t netshort) {
    return ((netshort & 0xFF00) >> 8) | ((netshort & 0x00FF) << 8);
}

// Helper function to convert board type to string
String ESPGameAPI::boardTypeToString(BoardType type) const {
    switch (type) {
        case BOARD_SOLAR: return "solar";
        case BOARD_WIND: return "wind";
        case BOARD_BATTERY: return "battery";
        case BOARD_GENERIC: 
        default: return "generic";
    }
}

// Authentication using JSON login endpoint
bool ESPGameAPI::login(const String& user, const String& pass) {
    username = user;
    password = pass;
    
    // Construct the full URL
    String loginUrl = baseUrl + "/coreapi/login";
    Serial.println("🔐 Attempting login to: " + loginUrl);
    Serial.println("👤 Username: " + username);
    
    http.begin(loginUrl);
    http.addHeader("Content-Type", "application/json");
    
    JsonDocument doc;
    doc["username"] = username;
    doc["password"] = password;
    
    String jsonString;
    serializeJson(doc, jsonString);
    Serial.println("📤 Sending JSON: " + jsonString);
    
    int httpCode = http.POST(jsonString);
    String response = http.getString();
    
    Serial.println("📥 HTTP Response Code: " + String(httpCode));
    Serial.println("📥 Response Body: " + response);
    
    if (httpCode == 200) {
        JsonDocument responseDoc;
        
        if (deserializeJson(responseDoc, response) == DeserializationError::Ok) {
            if (responseDoc["token"].is<String>()) {
                token = responseDoc["token"].as<String>();
                isLoggedIn = true;
                Serial.println("🔐 Successfully logged in");
                Serial.println("🎫 Token: " + token.substring(0, 20) + "...");
                http.end();
                return true;
            } else {
                Serial.println("❌ Token not found in response");
            }
        } else {
            Serial.println("❌ Failed to parse JSON response");
        }
    } else if (httpCode == 401) {
        Serial.println("❌ Invalid credentials (401)");
    } else if (httpCode == 404) {
        Serial.println("❌ Login endpoint not found (404) - Check server URL");
    } else if (httpCode == 400) {
        Serial.println("❌ Bad request (400) - Check JSON format");
    } else if (httpCode <= 0) {
        Serial.println("❌ Connection failed - Check network and server");
    } else {
        Serial.println("❌ Login failed with HTTP code: " + String(httpCode));
    }
    
    http.end();
    return false;
}

// Make HTTP request with binary data
bool ESPGameAPI::makeHttpRequest(const String& endpoint, const uint8_t* data, size_t dataSize, uint8_t* response, size_t& responseSize) {
    if (!isLoggedIn) {
        Serial.println("❌ Not logged in");
        return false;
    }
    
    String fullUrl = baseUrl + endpoint;
    Serial.println("📤 Making request to: " + fullUrl);
    Serial.println("📦 Data size: " + String(dataSize) + " bytes");
    
    http.begin(fullUrl);
    http.addHeader("Authorization", "Bearer " + token);
    http.addHeader("Content-Type", "application/octet-stream");
    
    int httpCode = http.POST((uint8_t*)data, dataSize);
    
    Serial.println("📥 HTTP Response Code: " + String(httpCode));
    
    if (httpCode == 200) {
        WiFiClient* stream = http.getStreamPtr();
        responseSize = stream->available();
        if (responseSize > 0 && response) {
            stream->readBytes(response, responseSize);
        }
        Serial.println("✅ Request successful, response size: " + String(responseSize));
        http.end();
        return true;
    } else {
        String errorResponse = http.getString();
        Serial.println("❌ HTTP request failed: " + String(httpCode));
        Serial.println("📄 Error response: " + errorResponse);
        http.end();
        return false;
    }
}

// Make HTTP GET request
bool ESPGameAPI::makeHttpGetRequest(const String& endpoint, uint8_t* response, size_t& responseSize) {
    if (!isLoggedIn) {
        Serial.println("❌ Not logged in");
        return false;
    }
    
    String fullUrl = baseUrl + endpoint;
    Serial.println("📤 Making GET request to: " + fullUrl);
    
    http.begin(fullUrl);
    http.addHeader("Authorization", "Bearer " + token);
    
    int httpCode = http.GET();
    
    Serial.println("📥 HTTP Response Code: " + String(httpCode));
    
    if (httpCode == 200) {
        WiFiClient* stream = http.getStreamPtr();
        responseSize = stream->available();
        if (responseSize > 0 && response) {
            stream->readBytes(response, responseSize);
        }
        Serial.println("✅ GET request successful, response size: " + String(responseSize));
        http.end();
        return true;
    } else {
        String errorResponse = http.getString();
        Serial.println("❌ HTTP GET failed: " + String(httpCode));
        Serial.println("📄 Error response: " + errorResponse);
        http.end();
        return false;
    }
}

// Non-blocking HTTP request methods
bool ESPGameAPI::startHttpRequest(const String& endpoint, const uint8_t* data, size_t dataSize, RequestType requestType) {
    if (currentRequestState != REQ_IDLE) {
        Serial.println("❌ Cannot start request: another request is pending");
        return false;
    }
    
    if (!isLoggedIn && requestType != REQ_LOGIN) {
        Serial.println("❌ Not logged in");
        return false;
    }
    
    // Store request parameters
    pendingEndpoint = endpoint;
    pendingIsPost = true;
    pendingRequestDataSize = dataSize;
    currentRequestType = requestType;
    
    // Copy data if provided
    if (data && dataSize > 0) {
        pendingRequestData = new uint8_t[dataSize];
        memcpy(pendingRequestData, data, dataSize);
    } else {
        pendingRequestData = nullptr;
        pendingRequestDataSize = 0;
    }
    
    String fullUrl = baseUrl + endpoint;
    Serial.println("📤 Starting async request to: " + fullUrl);
    
    http.begin(fullUrl);
    if (isLoggedIn) {
        http.addHeader("Authorization", "Bearer " + token);
    }
    
    if (requestType == REQ_LOGIN) {
        http.addHeader("Content-Type", "application/json");
    } else {
        http.addHeader("Content-Type", "application/octet-stream");
    }
    
    // Set timeout for the HTTP client
    http.setTimeout(5000);  // 5 second timeout per operation
    
    currentRequestState = REQ_PENDING;
    requestStartTime = millis();
    
    return true;
}

bool ESPGameAPI::startHttpGetRequest(const String& endpoint, RequestType requestType) {
    if (currentRequestState != REQ_IDLE) {
        Serial.println("❌ Cannot start GET request: another request is pending");
        return false;
    }
    
    if (!isLoggedIn) {
        Serial.println("❌ Not logged in");
        return false;
    }
    
    pendingEndpoint = endpoint;
    pendingIsPost = false;
    pendingRequestData = nullptr;
    pendingRequestDataSize = 0;
    currentRequestType = requestType;
    
    String fullUrl = baseUrl + endpoint;
    Serial.println("📤 Starting async GET request to: " + fullUrl);
    
    http.begin(fullUrl);
    http.addHeader("Authorization", "Bearer " + token);
    http.setTimeout(5000);
    
    currentRequestState = REQ_PENDING;
    requestStartTime = millis();
    
    return true;
}

bool ESPGameAPI::processRequest() {
    if (currentRequestState == REQ_IDLE) {
        return true;  // Nothing to process
    }
    
    // Check for timeout
    if (millis() - requestStartTime > requestTimeout) {
        Serial.println("❌ Request timeout");
        abortRequest();
        return false;
    }
    
    if (currentRequestState == REQ_PENDING) {
        // Start the actual HTTP request
        int httpCode;
        if (pendingIsPost) {
            if (currentRequestType == REQ_LOGIN) {
                // For login, data is JSON string
                String jsonData = String((char*)pendingRequestData);
                httpCode = http.POST(jsonData);
            } else {
                // For other requests, data is binary
                httpCode = http.POST(pendingRequestData, pendingRequestDataSize);
            }
        } else {
            httpCode = http.GET();
        }
        
        if (httpCode > 0) {
            currentRequestState = REQ_PROCESSING;
            Serial.println("📥 HTTP Response Code: " + String(httpCode));
            
            if (httpCode == 200) {
                WiFiClient* stream = http.getStreamPtr();
                responseSize = stream->available();
                if (responseSize > 0) {
                    responseSize = min(responseSize, sizeof(responseBuffer));
                    stream->readBytes(responseBuffer, responseSize);
                }
                currentRequestState = REQ_COMPLETED;
                Serial.println("✅ Request successful, response size: " + String(responseSize));
            } else {
                String errorResponse = http.getString();
                Serial.println("❌ HTTP request failed: " + String(httpCode));
                Serial.println("📄 Error response: " + errorResponse);
                currentRequestState = REQ_ERROR;
            }
        } else if (httpCode == HTTPC_ERROR_CONNECTION_REFUSED || 
                   httpCode == HTTPC_ERROR_SEND_HEADER_FAILED ||
                   httpCode == HTTPC_ERROR_SEND_PAYLOAD_FAILED) {
            Serial.println("❌ HTTP connection error: " + String(httpCode));
            currentRequestState = REQ_ERROR;
        }
        // If httpCode is 0 or negative but not a definitive error, keep waiting
    }
    
    return currentRequestState != REQ_ERROR;
}

void ESPGameAPI::completeRequest() {
    if (currentRequestState != REQ_COMPLETED) {
        return;
    }
    
    // Process the response based on request type
    switch (currentRequestType) {
        case REQ_LOGIN:
            if (responseSize > 0) {
                String response = String((char*)responseBuffer);
                JsonDocument responseDoc;
                
                if (deserializeJson(responseDoc, response) == DeserializationError::Ok) {
                    if (responseDoc["token"].is<String>()) {
                        token = responseDoc["token"].as<String>();
                        isLoggedIn = true;
                        Serial.println("🔐 Successfully logged in");
                        Serial.println("🎫 Token: " + token.substring(0, 20) + "...");
                    } else {
                        Serial.println("❌ Token not found in response");
                    }
                } else {
                    Serial.println("❌ Failed to parse JSON response");
                }
            }
            break;
            
        case REQ_REGISTER:
            if (responseSize >= 2) {
                uint8_t successFlag = responseBuffer[0];
                uint8_t messageLength = responseBuffer[1];
                
                if (successFlag == 0x01) {
                    isRegistered = true;
                    Serial.println("📋 Successfully registered board: " + boardName);
                } else {
                    if (messageLength > 0 && responseSize >= (2 + messageLength)) {
                        String errorMsg = "";
                        for (int i = 0; i < messageLength && i < 64; i++) {
                            errorMsg += (char)responseBuffer[2 + i];
                        }
                        Serial.println("❌ Registration failed: " + errorMsg);
                    } else {
                        Serial.println("❌ Registration failed: unknown error");
                    }
                }
            }
            break;
            
        case REQ_POLL_COEFFICIENTS:
            if (responseSize >= 2) {
                // Parse production coefficients
                uint8_t prodCount = responseBuffer[0];
                size_t offset = 1;
                
                if (responseSize >= (offset + prodCount * 5 + 1)) {
                    productionCoefficients.clear();
                    for (uint8_t i = 0; i < prodCount; i++) {
                        ProductionCoefficient coeff;
                        coeff.source_id = responseBuffer[offset];
                        int32_t coeffRaw = networkToHostLong(*(uint32_t*)(responseBuffer + offset + 1));
                        coeff.coefficient = (float)coeffRaw / 1000.0;
                        productionCoefficients.push_back(coeff);
                        offset += 5;
                    }
                    
                    // Parse consumption coefficients
                    uint8_t consCount = responseBuffer[offset];
                    offset++;
                    
                    if (responseSize >= (offset + consCount * 5)) {
                        consumptionCoefficients.clear();
                        for (uint8_t i = 0; i < consCount; i++) {
                            ConsumptionCoefficient coeff;
                            coeff.building_id = responseBuffer[offset];
                            int32_t consRaw = networkToHostLong(*(uint32_t*)(responseBuffer + offset + 1));
                            coeff.consumption = (float)consRaw / 1000.0;
                            consumptionCoefficients.push_back(coeff);
                            offset += 5;
                        }
                        
                        gameActive = true;
                        Serial.println("📊 Received " + String(prodCount) + " production and " + String(consCount) + " consumption coefficients");
                    }
                }
            } else if (responseSize == 0) {
                // Empty response means game is not active
                gameActive = false;
                productionCoefficients.clear();
                consumptionCoefficients.clear();
            }
            break;
            
        case REQ_SUBMIT_POWER:
            Serial.println("⚡ Power data submitted successfully");
            break;
            
        case REQ_REPORT_PLANTS:
            Serial.println("🔌 Power plants reported successfully");
            break;
            
        case REQ_REPORT_CONSUMERS:
            Serial.println("🏠 Consumers reported successfully");
            break;
            
        default:
            break;
    }
    
    // Reset state
    currentRequestState = REQ_IDLE;
    currentRequestType = REQ_NONE;
    
    // Clean up
    if (pendingRequestData) {
        delete[] pendingRequestData;
        pendingRequestData = nullptr;
    }
    pendingRequestDataSize = 0;
    
    http.end();
}

void ESPGameAPI::abortRequest() {
    Serial.println("🚫 Aborting request");
    
    currentRequestState = REQ_IDLE;
    currentRequestType = REQ_NONE;
    
    if (pendingRequestData) {
        delete[] pendingRequestData;
        pendingRequestData = nullptr;
    }
    pendingRequestDataSize = 0;
    
    http.end();
}

// Register board with the new binary protocol (empty body, board ID from JWT)
bool ESPGameAPI::registerBoard() {
    if (!isLoggedIn) {
        Serial.println("❌ Cannot register: not logged in");
        return false;
    }
    
    Serial.println("📋 Attempting board registration...");
    Serial.println("🎫 Using token: " + token.substring(0, 20) + "...");
    
    uint8_t responseBuffer[100];
    size_t responseSize = 0;
    
    bool success = makeHttpRequest("/coreapi/register", nullptr, 0, responseBuffer, responseSize);
    
    Serial.println("📥 Registration response size: " + String(responseSize));
    
    if (success) {
        if (responseSize >= 2) {
            uint8_t successFlag = responseBuffer[0];
            uint8_t messageLength = responseBuffer[1];
            
            Serial.println("🚩 Success flag: " + String(successFlag));
            Serial.println("📏 Message length: " + String(messageLength));
            
            if (successFlag == 0x01) {
                isRegistered = true;
                Serial.println("📋 Successfully registered board: " + boardName);
                return true;
            } else {
                // Print error message if available
                if (messageLength > 0 && responseSize >= (2 + messageLength)) {
                    String errorMsg = "";
                    for (int i = 0; i < messageLength && i < 64; i++) {
                        errorMsg += (char)responseBuffer[2 + i];
                    }
                    Serial.println("❌ Registration failed: " + errorMsg);
                } else {
                    Serial.println("❌ Registration failed: unknown error");
                }
            }
        } else {
            Serial.println("❌ Registration response too short");
        }
    } else {
        Serial.println("❌ Registration request failed");
    }
    
    return false;
}

// Non-blocking async methods
bool ESPGameAPI::loginAsync(const String& user, const String& pass) {
    username = user;
    password = pass;
    
    JsonDocument doc;
    doc["username"] = username;
    doc["password"] = password;
    
    String jsonString;
    serializeJson(doc, jsonString);
    
    Serial.println("🔐 Starting async login for: " + username);
    Serial.println("📤 Sending JSON: " + jsonString);
    
    return startHttpRequest("/coreapi/login", (uint8_t*)jsonString.c_str(), jsonString.length(), REQ_LOGIN);
}

bool ESPGameAPI::registerBoardAsync() {
    if (!isLoggedIn) {
        Serial.println("❌ Cannot register: not logged in");
        return false;
    }
    
    Serial.println("📋 Starting async board registration...");
    return startHttpRequest("/coreapi/register", nullptr, 0, REQ_REGISTER);
}

bool ESPGameAPI::pollCoefficientsAsync() {
    if (!isRegistered) {
        Serial.println("❌ Cannot poll: board not registered");
        return false;
    }
    
    return startHttpGetRequest("/coreapi/poll_binary", REQ_POLL_COEFFICIENTS);
}

bool ESPGameAPI::submitPowerDataAsync(float production, float consumption) {
    if (!isRegistered) {
        Serial.println("❌ Cannot submit data: board not registered");
        return false;
    }
    
    PowerDataRequest req;
    req.production = hostToNetworkLong((int32_t)(production * 1000));
    req.consumption = hostToNetworkLong((int32_t)(consumption * 1000));
    
    return startHttpRequest("/coreapi/post_vals", (uint8_t*)&req, sizeof(req), REQ_SUBMIT_POWER);
}

bool ESPGameAPI::reportConnectedPowerPlantsAsync(const std::vector<ConnectedPowerPlant>& plants) {
    if (!isRegistered) {
        Serial.println("❌ Cannot report power plants: board not registered");
        return false;
    }
    
    size_t dataSize = 1 + plants.size() * 8;
    uint8_t* data = new uint8_t[dataSize];
    
    data[0] = (uint8_t)plants.size();
    size_t offset = 1;
    
    for (const auto& plant : plants) {
        *(uint32_t*)(data + offset) = hostToNetworkLong(plant.plant_id);
        *(int32_t*)(data + offset + 4) = hostToNetworkLong((int32_t)(plant.set_power * 1000));
        offset += 8;
    }
    
    bool result = startHttpRequest("/coreapi/prod_connected", data, dataSize, REQ_REPORT_PLANTS);
    delete[] data;
    return result;
}

bool ESPGameAPI::reportConnectedConsumersAsync(const std::vector<ConnectedConsumer>& consumers) {
    if (!isRegistered) {
        Serial.println("❌ Cannot report consumers: board not registered");
        return false;
    }
    
    size_t dataSize = 1 + consumers.size() * 4;
    uint8_t* data = new uint8_t[dataSize];
    
    data[0] = (uint8_t)consumers.size();
    size_t offset = 1;
    
    for (const auto& consumer : consumers) {
        *(uint32_t*)(data + offset) = hostToNetworkLong(consumer.consumer_id);
        offset += 4;
    }
    
    bool result = startHttpRequest("/coreapi/cons_connected", data, dataSize, REQ_REPORT_CONSUMERS);
    delete[] data;
    return result;
}

// Submit power data using new binary protocol (8 bytes: production + consumption)
bool ESPGameAPI::submitPowerData(float production, float consumption) {
    if (!isRegistered) {
        Serial.println("❌ Cannot submit data: board not registered");
        return false;
    }
    
    PowerDataRequest req;
    // Convert watts to milliwatts as signed integers
    req.production = hostToNetworkLong((int32_t)(production * 1000));
    req.consumption = hostToNetworkLong((int32_t)(consumption * 1000));
    
    uint8_t responseBuffer[10];
    size_t responseSize = 0;
    
    bool success = makeHttpRequest("/coreapi/post_vals", (uint8_t*)&req, sizeof(req), responseBuffer, responseSize);
    
    if (success) {
        Serial.println("⚡ Power data submitted - Gen: " + String(production, 1) + "W, Cons: " + String(consumption, 1) + "W");
        return true;
    }
    
    return false;
}

// Poll coefficients using new binary protocol
bool ESPGameAPI::pollCoefficients() {
    if (!isRegistered) {
        Serial.println("❌ Cannot poll: board not registered");
        return false;
    }
    
    uint8_t responseBuffer[1000];  // Large buffer for coefficients
    size_t responseSize = 0;
    
    bool success = makeHttpGetRequest("/coreapi/poll_binary", responseBuffer, responseSize);
    
    if (success && responseSize >= 2) {
        // Parse production coefficients
        uint8_t prodCount = responseBuffer[0];
        size_t offset = 1;
        
        if (responseSize < (offset + prodCount * 5 + 1)) {
            Serial.println("❌ Invalid coefficient response size");
            return false;
        }
        
        productionCoefficients.clear();
        for (uint8_t i = 0; i < prodCount; i++) {
            ProductionCoefficient coeff;
            coeff.source_id = responseBuffer[offset];
            int32_t coeffRaw = networkToHostLong(*(uint32_t*)(responseBuffer + offset + 1));
            coeff.coefficient = (float)coeffRaw / 1000.0;  // Convert mW to W
            productionCoefficients.push_back(coeff);
            offset += 5;
        }
        
        // Parse consumption coefficients
        uint8_t consCount = responseBuffer[offset];
        offset++;
        
        if (responseSize < (offset + consCount * 5)) {
            Serial.println("❌ Invalid consumption coefficient response size");
            return false;
        }
        
        consumptionCoefficients.clear();
        for (uint8_t i = 0; i < consCount; i++) {
            ConsumptionCoefficient coeff;
            coeff.building_id = responseBuffer[offset];
            int32_t consRaw = networkToHostLong(*(uint32_t*)(responseBuffer + offset + 1));
            coeff.consumption = (float)consRaw / 1000.0;  // Convert mW to W
            consumptionCoefficients.push_back(coeff);
            offset += 5;
        }
        
        gameActive = true;  // If we get coefficients, game is active
        Serial.println("📊 Received " + String(prodCount) + " production and " + String(consCount) + " consumption coefficients");
        return true;
    } else if (success && responseSize == 0) {
        // Empty response means game is not active
        gameActive = false;
        productionCoefficients.clear();
        consumptionCoefficients.clear();
        return true;
    }
    
    return false;
}

// Get production values (separate endpoint)
bool ESPGameAPI::getProductionValues() {
    if (!isRegistered) {
        Serial.println("❌ Cannot get production values: board not registered");
        return false;
    }
    
    uint8_t responseBuffer[500];
    size_t responseSize = 0;
    
    bool success = makeHttpGetRequest("/coreapi/prod_vals", responseBuffer, responseSize);
    
    if (success && responseSize >= 1) {
        return parseProductionCoefficients(responseBuffer, responseSize);
    }
    
    return false;
}

// Get consumption values (separate endpoint)
bool ESPGameAPI::getConsumptionValues() {
    if (!isRegistered) {
        Serial.println("❌ Cannot get consumption values: board not registered");
        return false;
    }
    
    uint8_t responseBuffer[500];
    size_t responseSize = 0;
    
    bool success = makeHttpGetRequest("/coreapi/cons_vals", responseBuffer, responseSize);
    
    if (success && responseSize >= 1) {
        return parseConsumptionCoefficients(responseBuffer, responseSize);
    }
    
    return false;
}

// Report connected power plants
bool ESPGameAPI::reportConnectedPowerPlants(const std::vector<ConnectedPowerPlant>& plants) {
    if (!isRegistered) {
        Serial.println("❌ Cannot report power plants: board not registered");
        return false;
    }
    
    size_t dataSize = 1 + plants.size() * 8;  // count + (id+power)*count
    uint8_t* data = new uint8_t[dataSize];
    
    data[0] = (uint8_t)plants.size();
    size_t offset = 1;
    
    for (const auto& plant : plants) {
        *(uint32_t*)(data + offset) = hostToNetworkLong(plant.plant_id);
        *(int32_t*)(data + offset + 4) = hostToNetworkLong((int32_t)(plant.set_power * 1000));  // Convert W to mW
        offset += 8;
    }
    
    uint8_t responseBuffer[10];
    size_t responseSize = 0;
    
    bool success = makeHttpRequest("/coreapi/prod_connected", data, dataSize, responseBuffer, responseSize);
    delete[] data;
    
    if (success) {
        Serial.println("🔌 Reported " + String(plants.size()) + " connected power plants");
        return true;
    }
    
    return false;
}

// Report connected consumers
bool ESPGameAPI::reportConnectedConsumers(const std::vector<ConnectedConsumer>& consumers) {
    if (!isRegistered) {
        Serial.println("❌ Cannot report consumers: board not registered");
        return false;
    }
    
    size_t dataSize = 1 + consumers.size() * 4;  // count + id*count
    uint8_t* data = new uint8_t[dataSize];
    
    data[0] = (uint8_t)consumers.size();
    size_t offset = 1;
    
    for (const auto& consumer : consumers) {
        *(uint32_t*)(data + offset) = hostToNetworkLong(consumer.consumer_id);
        offset += 4;
    }
    
    uint8_t responseBuffer[10];
    size_t responseSize = 0;
    
    bool success = makeHttpRequest("/coreapi/cons_connected", data, dataSize, responseBuffer, responseSize);
    delete[] data;
    
    if (success) {
        Serial.println("🏠 Reported " + String(consumers.size()) + " connected consumers");
        return true;
    }
    
    return false;
}

// Main update function - call this in Arduino loop() - now non-blocking!
bool ESPGameAPI::update() {
    if (!isConnected()) {
        return false;
    }
    
    // Always process any pending requests first
    bool requestProcessed = false;
    if (currentRequestState != REQ_IDLE) {
        if (processRequest()) {
            if (currentRequestState == REQ_COMPLETED) {
                completeRequest();
                requestProcessed = true;
            }
        } else {
            // Request failed or timed out
            abortRequest();
        }
    }
    
    // Don't start new requests if one is already pending
    if (currentRequestState != REQ_IDLE) {
        return requestProcessed;
    }
    
    unsigned long currentTime = millis();
    bool updated = false;
    
    // Poll coefficients at poll interval
    if (currentTime - lastPollTime >= pollInterval) {
        lastPollTime = currentTime;
        if (pollCoefficientsAsync()) {
            // Request started, will be processed in next update() call
            return true;
        }
    }
    
    // Submit data at update interval if game is active and callbacks are set
    if (gameActive && (currentTime - lastUpdateTime >= updateInterval)) {
        lastUpdateTime = currentTime;
        
        // Report connected devices if callbacks are set
        if (powerPlantsCallback) {
            std::vector<ConnectedPowerPlant> plants = powerPlantsCallback();
            if (reportConnectedPowerPlantsAsync(plants)) {
                return true;  // Request started
            }
        }
        
        if (consumersCallback) {
            std::vector<ConnectedConsumer> consumers = consumersCallback();
            if (reportConnectedConsumersAsync(consumers)) {
                return true;  // Request started
            }
        }
        
        // Submit power data if callbacks are set
        if (productionCallback && consumptionCallback) {
            float production = productionCallback();
            float consumption = consumptionCallback();
            if (submitPowerDataAsync(production, consumption)) {
                return true;  // Request started
            }
        }
    }
    
    return requestProcessed;
}

// Helper function to parse production coefficients from binary data
bool ESPGameAPI::parseProductionCoefficients(const uint8_t* data, size_t dataSize) {
    if (dataSize < 1) return false;
    
    uint8_t count = data[0];
    size_t expectedSize = 1 + count * 5;
    
    if (dataSize != expectedSize) {
        Serial.println("❌ Invalid production coefficients size");
        return false;
    }
    
    productionCoefficients.clear();
    size_t offset = 1;
    
    for (uint8_t i = 0; i < count; i++) {
        ProductionCoefficient coeff;
        coeff.source_id = data[offset];
        int32_t coeffRaw = networkToHostLong(*(uint32_t*)(data + offset + 1));
        coeff.coefficient = (float)coeffRaw / 1000.0;  // Convert mW to W
        productionCoefficients.push_back(coeff);
        offset += 5;
    }
    
    Serial.println("� Parsed " + String(count) + " production coefficients");
    return true;
}

// Helper function to parse consumption coefficients from binary data
bool ESPGameAPI::parseConsumptionCoefficients(const uint8_t* data, size_t dataSize) {
    if (dataSize < 1) return false;
    
    uint8_t count = data[0];
    size_t expectedSize = 1 + count * 5;
    
    if (dataSize != expectedSize) {
        Serial.println("❌ Invalid consumption coefficients size");
        return false;
    }
    
    consumptionCoefficients.clear();
    size_t offset = 1;
    
    for (uint8_t i = 0; i < count; i++) {
        ConsumptionCoefficient coeff;
        coeff.building_id = data[offset];
        int32_t consRaw = networkToHostLong(*(uint32_t*)(data + offset + 1));
        coeff.consumption = (float)consRaw / 1000.0;  // Convert mW to W
        consumptionCoefficients.push_back(coeff);
        offset += 5;
    }
    
    Serial.println("🏠 Parsed " + String(count) + " consumption coefficients");
    return true;
}

// Debug function
void ESPGameAPI::printStatus() const {
    Serial.println();
    Serial.println("=== ESP Game API Status ===");
    Serial.println("Board Name: " + boardName);
    Serial.println("Board Type: " + boardTypeToString(boardType));
    Serial.println("Logged In: " + String(isLoggedIn ? "Yes" : "No"));
    Serial.println("Registered: " + String(isRegistered ? "Yes" : "No"));
    Serial.println("Game Active: " + String(gameActive ? "Yes" : "No"));
    Serial.println("WiFi Connected: " + String(WiFi.status() == WL_CONNECTED ? "Yes" : "No"));
    Serial.println("Update Interval: " + String(updateInterval) + "ms");
    Serial.println("Poll Interval: " + String(pollInterval) + "ms");
    Serial.println("Production Coefficients: " + String(productionCoefficients.size()));
    Serial.println("Consumption Coefficients: " + String(consumptionCoefficients.size()));
    Serial.println("Callbacks Set:");
    Serial.println("  Production: " + String(productionCallback ? "Yes" : "No"));
    Serial.println("  Consumption: " + String(consumptionCallback ? "Yes" : "No"));
    Serial.println("  Power Plants: " + String(powerPlantsCallback ? "Yes" : "No"));
    Serial.println("  Consumers: " + String(consumersCallback ? "Yes" : "No"));
    Serial.println("===========================");
}

void ESPGameAPI::printCoefficients() const {
    Serial.println();
    Serial.println("=== Game Coefficients ===");
    
    Serial.println("Production Coefficients (" + String(productionCoefficients.size()) + "):");
    for (const auto& coeff : productionCoefficients) {
        Serial.println("  Source " + String(coeff.source_id) + ": " + String(coeff.coefficient, 3) + "W");
    }
    
    Serial.println("Consumption Coefficients (" + String(consumptionCoefficients.size()) + "):");
    for (const auto& coeff : consumptionCoefficients) {
        Serial.println("  Building " + String(coeff.building_id) + ": " + String(coeff.consumption, 3) + "W");
    }
    
    Serial.println("========================");
}
