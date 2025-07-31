#include "ESPGameAPI.h"
#include <ArduinoJson.h>

// Forward declaration for embedded certificate bundle
extern "C" {
    extern const uint8_t x509_crt_bundle_start[] asm("_binary_cert_x509_crt_bundle_bin_start");
}

// ───────────────────────────────────────────── Dummy serial (see header)
#ifndef ESPGAMEAPI_ENABLE_SERIAL
  #define Serial DBG_SERIAL
#endif
// ─────────────────────────────────────────────

// constructor ------------------------------------------------------------------
ESPGameAPI::ESPGameAPI(const String& url, const String& name, BoardType type,
                       unsigned long upd, unsigned long poll)
    : baseUrl(url), boardName(name), boardType(type),
      isLoggedIn(false), isRegistered(false),
      lastUpdateTime(0), lastPollTime(0),
      updateInterval(upd), pollInterval(poll),
      gameActive(false),
      coeffsUpdated(false),
      requestPollInFlight(false), requestPostInFlight(false) {}

// ───────────────────────────────────────────── byte‑order helpers (unchanged)
uint32_t ESPGameAPI::hostToNetworkLong(uint32_t v) {
    return ((v & 0xFF000000) >> 24) | ((v & 0x00FF0000) >> 8) |
           ((v & 0x0000FF00) << 8)  | ((v & 0x000000FF) << 24);
}
uint64_t ESPGameAPI::hostToNetworkLongLong(uint64_t v) {
    return ((uint64_t)hostToNetworkLong((uint32_t)v) << 32) |
            hostToNetworkLong((uint32_t)(v >> 32));
}
uint32_t ESPGameAPI::networkToHostLong    (uint32_t v){ return hostToNetworkLong(v); }
uint64_t ESPGameAPI::networkToHostLongLong(uint64_t v){ return hostToNetworkLongLong(v);}
uint16_t ESPGameAPI::networkToHostShort   (uint16_t v){ return (v>>8)|(v<<8); }

// ───────────────────────────────────────────── boardType -> string
String ESPGameAPI::boardTypeToString(BoardType t) const {
    switch(t){ case BOARD_SOLAR: return "solar";
               case BOARD_WIND:  return "wind";
               case BOARD_BATTERY:return "battery";
               default:          return "generic";}
}

// ───────────────────────────────────────────── Certificate bundle initialization
void ESPGameAPI::initCertificateBundle() {
    arduino_esp_crt_bundle_set(x509_crt_bundle_start);
    Serial.println("🔒 Certificate bundle initialized");
}

// ───────────────────────────────────────────── login / register (unchanged)
//  … full original blocking implementations kept – omitted here for brevity …
#include <ArduinoJson.h>      // needed by original code

bool ESPGameAPI::login(const String& user, const String& pass){ 
    username = user;
    password = pass;
    
    Serial.println("🔐 Attempting login for: " + username);
    
    JsonDocument doc;
    doc["username"] = username;
    doc["password"] = password;
    
    String jsonString;
    serializeJson(doc, jsonString);
    Serial.println("📤 Sending JSON: " + jsonString);
    
    // Use a blocking approach with AsyncRequest
    volatile bool requestComplete = false;
    volatile bool loginSuccess = false;
    
    AsyncRequest::fetch(
        AsyncRequest::Method::POST,
        std::string((baseUrl + "/coreapi/login").c_str()),
        std::string(jsonString.c_str()),
        { { "Content-Type", "application/json" } },
        [&](esp_err_t err, int status, std::string body) {
            Serial.printf("📥 Login HTTP %d\n", status);
            
            if (err != ESP_OK) {
                Serial.println("❌ Login request failed: " + String(esp_err_to_name(err)));
                requestComplete = true;
                return;
            }
            
            if (status == 200) {
                JsonDocument responseDoc;
                if (deserializeJson(responseDoc, body) == DeserializationError::Ok) {
                    if (responseDoc["token"].is<const char*>()) {
                        token = responseDoc["token"].as<const char*>();
                        isLoggedIn = true;
                        loginSuccess = true;
                        Serial.println("🔐 Successfully logged in");
                        Serial.println("🎫 Token: " + token.substring(0, 20) + "...");
                    } else {
                        Serial.println("❌ Token not found in response");
                    }
                } else {
                    Serial.println("❌ Failed to parse JSON response");
                }
            } else if (status == 401) {
                Serial.println("❌ Invalid credentials (401)");
            } else if (status == 404) {
                Serial.println("❌ Login endpoint not found (404) - Check server URL");
            } else {
                Serial.println("❌ Login failed with HTTP code: " + String(status));
            }
            requestComplete = true;
        });
    
    // Wait for completion (blocking)
    unsigned long startTime = millis();
    while (!requestComplete && (millis() - startTime) < 10000) {
        delay(10);
    }
    
    if (!requestComplete) {
        Serial.println("❌ Login request timeout");
        return false;
    }
    
    return loginSuccess;
}
bool ESPGameAPI::registerBoard(){ 
    if (!isLoggedIn) {
        Serial.println("❌ Cannot register: not logged in");
        return false;
    }
    
    Serial.println("📋 Attempting board registration...");
    Serial.println("🎫 Using token: " + token.substring(0, 20) + "...");
    
    // Use blocking approach with AsyncRequest
    volatile bool requestComplete = false;
    volatile bool registerSuccess = false;
    
    AsyncRequest::fetch(
        AsyncRequest::Method::POST,
        std::string((baseUrl + "/coreapi/register").c_str()),
        "",  // no body
        { { "Authorization", "Bearer " + std::string(token.c_str()) } },
        [&](esp_err_t err, int status, std::string body) {
            Serial.printf("📥 Register HTTP %d\n", status);
            
            if (err != ESP_OK) {
                Serial.println("❌ Registration request failed: " + String(esp_err_to_name(err)));
                requestComplete = true;
                return;
            }
            
            if (status == 200 && body.size() >= 2) {
                uint8_t successFlag = static_cast<uint8_t>(body[0]);
                uint8_t messageLength = static_cast<uint8_t>(body[1]);
                
                Serial.println("🚩 Success flag: " + String(successFlag));
                Serial.println("📏 Message length: " + String(messageLength));
                
                if (successFlag == 0x01) {
                    isRegistered = true;
                    registerSuccess = true;
                    Serial.println("📋 Successfully registered board: " + boardName);
                } else {
                    // Print error message if available
                    if (messageLength > 0 && body.size() >= (2 + messageLength)) {
                        String errorMsg = "";
                        for (int i = 0; i < messageLength && i < 64; i++) {
                            errorMsg += static_cast<char>(body[2 + i]);
                        }
                        Serial.println("❌ Registration failed: " + errorMsg);
                    } else {
                        Serial.println("❌ Registration failed: unknown error");
                    }
                }
            } else {
                Serial.println("❌ Registration response invalid or too short");
            }
            requestComplete = true;
        });
    
    // Wait for completion (blocking)
    unsigned long startTime = millis();
    while (!requestComplete && (millis() - startTime) < 10000) {
        delay(10);
    }
    
    if (!requestComplete) {
        Serial.println("❌ Registration request timeout");
        return false;
    }
    
    return registerSuccess;
}

// ───────────────────────────────────────────── Response parsing helpers
void ESPGameAPI::parsePollResponse(const uint8_t* data, size_t len) {
    if (len == 0) {
        // Game paused
        gameActive = false;
        productionCoefficients.clear();
        consumptionCoefficients.clear();
        Serial.println("🎮 Game paused - coefficients cleared");
        return;
    }
    
    if (len < 2) {
        Serial.println("❌ Poll response too short");
        return;
    }
    
    size_t offset = 0;
    uint8_t prodCount = data[offset++];
    
    // Check if we have enough data for production coefficients
    if (len < offset + prodCount * 5 + 1) {
        Serial.println("❌ Malformed poll response - insufficient data for production coefficients");
        return;
    }
    
    // Parse production coefficients
    productionCoefficients.clear();
    for (uint8_t i = 0; i < prodCount; i++) {
        ProductionCoefficient coeff;
        coeff.source_id = data[offset];
        coeff.coefficient = static_cast<float>(networkToHostLong(*reinterpret_cast<const uint32_t*>(data + offset + 1))) / 1000.0f;
        productionCoefficients.push_back(coeff);
        offset += 5;
    }
    
    uint8_t consCount = data[offset++];
    
    // Check if we have enough data for consumption coefficients
    if (len < offset + consCount * 5) {
        Serial.println("❌ Malformed poll response - insufficient data for consumption coefficients");
        return;
    }
    
    // Parse consumption coefficients
    consumptionCoefficients.clear();
    for (uint8_t i = 0; i < consCount; i++) {
        ConsumptionCoefficient coeff;
        coeff.building_id = data[offset];
        coeff.consumption = static_cast<float>(networkToHostLong(*reinterpret_cast<const uint32_t*>(data + offset + 1))) / 1000.0f;
        consumptionCoefficients.push_back(coeff);
        offset += 5;
    }
    
    gameActive = true;
    Serial.println("🎮 Game active - parsed " + String(prodCount) + " production and " + String(consCount) + " consumption coefficients");
}

bool ESPGameAPI::parseProductionCoefficients(const uint8_t* data, size_t len) {
    if (len < 1) return false;
    
    uint8_t count = data[0];
    size_t offset = 1;
    
    if (len < offset + count * 5) return false;
    
    productionCoefficients.clear();
    for (uint8_t i = 0; i < count; i++) {
        ProductionCoefficient coeff;
        coeff.source_id = data[offset];
        coeff.coefficient = static_cast<float>(networkToHostLong(*reinterpret_cast<const uint32_t*>(data + offset + 1))) / 1000.0f;
        productionCoefficients.push_back(coeff);
        offset += 5;
    }
    
    return true;
}

bool ESPGameAPI::parseConsumptionCoefficients(const uint8_t* data, size_t len) {
    if (len < 1) return false;
    
    uint8_t count = data[0];
    size_t offset = 1;
    
    if (len < offset + count * 5) return false;
    
    consumptionCoefficients.clear();
    for (uint8_t i = 0; i < count; i++) {
        ConsumptionCoefficient coeff;
        coeff.building_id = data[offset];
        coeff.consumption = static_cast<float>(networkToHostLong(*reinterpret_cast<const uint32_t*>(data + offset + 1))) / 1000.0f;
        consumptionCoefficients.push_back(coeff);
        offset += 5;
    }
    
    return true;
}

// ───────────────────────────────────────────── Async API operations
void ESPGameAPI::pollCoefficients(CoefficientsCallback callback) {
    if (!isRegistered) {
        if (callback) callback(false, "Board not registered");
        return;
    }
    
    requestPollInFlight = true;
    
    AsyncRequest::fetch(
        AsyncRequest::Method::GET,
        std::string((baseUrl + "/coreapi/poll_binary").c_str()),
        "",
        { { "Authorization", "Bearer " + std::string(token.c_str()) } },
        [this, callback](esp_err_t err, int status, std::string body) {
            requestPollInFlight = false;
            
            if (err != ESP_OK) {
                Serial.println("❌ Poll coefficients failed: " + String(esp_err_to_name(err)));
                if (callback) callback(false, "Network error: " + std::string(esp_err_to_name(err)));
                return;
            }
            
            if (status == 200) {
                parsePollResponse(reinterpret_cast<const uint8_t*>(body.data()), body.size());
                coeffsUpdated = true;
                Serial.println("✅ Coefficients updated successfully");
                if (callback) callback(true, "");
            } else {
                Serial.println("❌ Poll coefficients HTTP error: " + String(status));
                if (callback) callback(false, "HTTP error: " + std::to_string(status));
            }
        });
}

void ESPGameAPI::submitPowerData(float production, float consumption, AsyncCallback callback) {
    if (!isRegistered) {
        if (callback) callback(false, "Board not registered");
        return;
    }
    
    PowerDataRequest request;
    request.production = hostToNetworkLong(static_cast<int32_t>(production * 1000));
    request.consumption = hostToNetworkLong(static_cast<int32_t>(consumption * 1000));
    
    requestPostInFlight = true;
    
    std::string payload(reinterpret_cast<const char*>(&request), sizeof(request));
    
    AsyncRequest::fetch(
        AsyncRequest::Method::POST,
        std::string((baseUrl + "/coreapi/post_vals").c_str()),
        payload,
        { 
            { "Authorization", "Bearer " + std::string(token.c_str()) },
            { "Content-Type", "application/octet-stream" }
        },
        [this, callback](esp_err_t err, int status, std::string body) {
            requestPostInFlight = false;
            
            if (err != ESP_OK) {
                Serial.println("❌ Submit power data failed: " + String(esp_err_to_name(err)));
                if (callback) callback(false, "Network error: " + std::string(esp_err_to_name(err)));
                return;
            }
            
            if (status == 200) {
                Serial.println("✅ Power data submitted successfully");
                if (callback) callback(true, "");
            } else {
                Serial.println("❌ Submit power data HTTP error: " + String(status));
                if (callback) callback(false, "HTTP error: " + std::to_string(status));
            }
        });
}

void ESPGameAPI::reportConnectedPowerPlants(const std::vector<ConnectedPowerPlant>& plants, AsyncCallback callback) {
    if (!isRegistered) {
        if (callback) callback(false, "Board not registered");
        return;
    }
    
    std::vector<uint8_t> data(1 + plants.size() * 8);
    data[0] = static_cast<uint8_t>(plants.size());
    size_t offset = 1;
    
    for (const auto& plant : plants) {
        *reinterpret_cast<uint32_t*>(&data[offset]) = hostToNetworkLong(plant.plant_id);
        *reinterpret_cast<int32_t*>(&data[offset + 4]) = hostToNetworkLong(static_cast<int32_t>(plant.set_power * 1000));
        offset += 8;
    }
    
    std::string payload(reinterpret_cast<const char*>(data.data()), data.size());
    
    AsyncRequest::fetch(
        AsyncRequest::Method::POST,
        std::string((baseUrl + "/coreapi/prod_connected").c_str()),
        payload,
        { 
            { "Authorization", "Bearer " + std::string(token.c_str()) },
            { "Content-Type", "application/octet-stream" }
        },
        [callback](esp_err_t err, int status, std::string body) {
            if (err != ESP_OK) {
                Serial.println("❌ Report power plants failed: " + String(esp_err_to_name(err)));
                if (callback) callback(false, "Network error: " + std::string(esp_err_to_name(err)));
                return;
            }
            
            if (status == 200) {
                Serial.println("✅ Power plants reported successfully");
                if (callback) callback(true, "");
            } else {
                Serial.println("❌ Report power plants HTTP error: " + String(status));
                if (callback) callback(false, "HTTP error: " + std::to_string(status));
            }
        });
}

void ESPGameAPI::reportConnectedConsumers(const std::vector<ConnectedConsumer>& consumers, AsyncCallback callback) {
    if (!isRegistered) {
        if (callback) callback(false, "Board not registered");
        return;
    }
    
    std::vector<uint8_t> data(1 + consumers.size() * 4);
    data[0] = static_cast<uint8_t>(consumers.size());
    size_t offset = 1;
    
    for (const auto& consumer : consumers) {
        *reinterpret_cast<uint32_t*>(&data[offset]) = hostToNetworkLong(consumer.consumer_id);
        offset += 4;
    }
    
    std::string payload(reinterpret_cast<const char*>(data.data()), data.size());
    
    AsyncRequest::fetch(
        AsyncRequest::Method::POST,
        std::string((baseUrl + "/coreapi/cons_connected").c_str()),
        payload,
        { 
            { "Authorization", "Bearer " + std::string(token.c_str()) },
            { "Content-Type", "application/octet-stream" }
        },
        [callback](esp_err_t err, int status, std::string body) {
            if (err != ESP_OK) {
                Serial.println("❌ Report consumers failed: " + String(esp_err_to_name(err)));
                if (callback) callback(false, "Network error: " + std::string(esp_err_to_name(err)));
                return;
            }
            
            if (status == 200) {
                Serial.println("✅ Consumers reported successfully");
                if (callback) callback(true, "");
            } else {
                Serial.println("❌ Report consumers HTTP error: " + String(status));
                if (callback) callback(false, "HTTP error: " + std::to_string(status));
            }
        });
}

void ESPGameAPI::getProductionValues(ProductionCallback callback) {
    if (!isRegistered) {
        if (callback) callback(false, {}, "Board not registered");
        return;
    }
    
    AsyncRequest::fetch(
        AsyncRequest::Method::GET,
        std::string((baseUrl + "/coreapi/prod_vals").c_str()),
        "",
        { { "Authorization", "Bearer " + std::string(token.c_str()) } },
        [this, callback](esp_err_t err, int status, std::string body) {
            if (err != ESP_OK) {
                Serial.println("❌ Get production values failed: " + String(esp_err_to_name(err)));
                if (callback) callback(false, {}, "Network error: " + std::string(esp_err_to_name(err)));
                return;
            }
            
            if (status == 200) {
                std::vector<ProductionCoefficient> coeffs;
                if (parseProductionCoefficients(reinterpret_cast<const uint8_t*>(body.data()), body.size())) {
                    coeffs = productionCoefficients;
                    Serial.println("✅ Production values retrieved successfully");
                    if (callback) callback(true, coeffs, "");
                } else {
                    Serial.println("❌ Failed to parse production values");
                    if (callback) callback(false, {}, "Failed to parse response");
                }
            } else {
                Serial.println("❌ Get production values HTTP error: " + String(status));
                if (callback) callback(false, {}, "HTTP error: " + std::to_string(status));
            }
        });
}

void ESPGameAPI::getConsumptionValues(ConsumptionValCallback callback) {
    if (!isRegistered) {
        if (callback) callback(false, {}, "Board not registered");
        return;
    }
    
    AsyncRequest::fetch(
        AsyncRequest::Method::GET,
        std::string((baseUrl + "/coreapi/cons_vals").c_str()),
        "",
        { { "Authorization", "Bearer " + std::string(token.c_str()) } },
        [this, callback](esp_err_t err, int status, std::string body) {
            if (err != ESP_OK) {
                Serial.println("❌ Get consumption values failed: " + String(esp_err_to_name(err)));
                if (callback) callback(false, {}, "Network error: " + std::string(esp_err_to_name(err)));
                return;
            }
            
            if (status == 200) {
                std::vector<ConsumptionCoefficient> coeffs;
                if (parseConsumptionCoefficients(reinterpret_cast<const uint8_t*>(body.data()), body.size())) {
                    coeffs = consumptionCoefficients;
                    Serial.println("✅ Consumption values retrieved successfully");
                    if (callback) callback(true, coeffs, "");
                } else {
                    Serial.println("❌ Failed to parse consumption values");
                    if (callback) callback(false, {}, "Failed to parse response");
                }
            } else {
                Serial.println("❌ Get consumption values HTTP error: " + String(status));
                if (callback) callback(false, {}, "HTTP error: " + std::to_string(status));
            }
        });
}
// ───────────────────────────────────────────── Non‑blocking loop helper
bool ESPGameAPI::update(){
    if(!isConnected()) return false;

    unsigned long now = millis();
    
    // Schedule coefficient poll
    if(!requestPollInFlight && now - lastPollTime >= pollInterval){
        lastPollTime = now;
        pollCoefficients();  // fire‑and‑forget with internal callback
    }
    
    // Schedule power data submission
    if(gameActive && !requestPostInFlight && now - lastUpdateTime >= updateInterval){
        lastUpdateTime = now;

        // Report connected power plants if callback is set
        if(powerPlantsCallback) {
            reportConnectedPowerPlants(powerPlantsCallback());
        }
        
        // Report connected consumers if callback is set
        if(consumersCallback) {
            reportConnectedConsumers(consumersCallback());
        }

        // Submit power data if both callbacks are set
        if(productionCallback && consumptionCallback) {
            submitPowerData(productionCallback(), consumptionCallback());
        }
    }

    bool ret = coeffsUpdated;
    coeffsUpdated = false;        // one‑shot flag
    return ret;
}

// ───────────────────────────────────────────── Debug helpers (silenced unless enabled)
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