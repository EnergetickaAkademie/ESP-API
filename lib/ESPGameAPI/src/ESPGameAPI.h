#ifndef ESP_GAME_API_H
#define ESP_GAME_API_H

#include <Arduino.h>
#include <WiFi.h>
#include <vector>
#include <functional>
#include "AsyncRequest.hpp"

// Forward declaration for certificate bundle
extern "C" {
    #include "esp_crt_bundle.h"
}

// ───────────────────────────────── Debug (prints off by default)
#ifdef ESPGAMEAPI_ENABLE_SERIAL
  #define DBG_SERIAL Serial
#else
  // tiny dummy object that swallows every call
  struct _DbgSerial_ {
    template<class... A> void print  (A...) {}
    template<class... A> void println(A...) {}
    template<class... A> void printf (const char*, A...) {}
  };
  static _DbgSerial_ DBG_SERIAL;
#endif
// ─────────────────────────────────

// Protocol version
#define PROTOCOL_VERSION 0x01
#define POWER_NULL_VALUE 0x7FFFFFFF       // special power value
#define FLAG_GENERATION_PRESENT 0x01
#define FLAG_CONSUMPTION_PRESENT 0x02

// Board types
enum BoardType { BOARD_SOLAR, BOARD_WIND, BOARD_BATTERY, BOARD_GENERIC };

// Structures (unchanged) -------------------------------------------------------
struct ProductionCoefficient  { uint8_t source_id;   float coefficient;  };
struct ConsumptionCoefficient { uint8_t building_id; float consumption; };
struct ConnectedPowerPlant    { uint32_t plant_id;   float set_power;   };
struct ConnectedConsumer      { uint32_t consumer_id; };

using PowerCallback       = std::function<float()>;
using PowerPlantsCallback = std::function<std::vector<ConnectedPowerPlant>()>;
using ConsumersCallback   = std::function<std::vector<ConnectedConsumer>()>;

// Async callback types for endpoints
using AsyncCallback         = std::function<void(bool success, const std::string& error)>;
using CoefficientsCallback  = std::function<void(bool success, const std::string& error)>;
using ProductionCallback    = std::function<void(bool success, const std::vector<ProductionCoefficient>& coeffs, const std::string& error)>;
using ConsumptionValCallback = std::function<void(bool success, const std::vector<ConsumptionCoefficient>& coeffs, const std::string& error)>;

struct __attribute__((packed)) PowerDataRequest { int32_t production; int32_t consumption; };
struct __attribute__((packed)) ProductionEntry  { uint8_t source_id; int32_t coefficient; };
struct __attribute__((packed)) ConsumptionEntry { uint8_t building_id; int32_t consumption; };
struct __attribute__((packed)) PowerPlantEntry  { uint32_t plant_id;  int32_t set_power;  };
struct __attribute__((packed)) ConsumerEntry    { uint32_t consumer_id; };

// ───────────────────────────────────────────────────────────────────────────────
class ESPGameAPI {
private:
    // ---------- user‑visible state ----------
    String baseUrl, username, password, token, boardName;
    BoardType boardType;
    bool isLoggedIn, isRegistered;

    unsigned long lastUpdateTime, updateInterval;
    unsigned long lastPollTime,   pollInterval;

    PowerCallback       productionCallback;
    PowerCallback       consumptionCallback;
    PowerPlantsCallback powerPlantsCallback;
    ConsumersCallback   consumersCallback;

    std::vector<ProductionCoefficient>  productionCoefficients;
    std::vector<ConsumptionCoefficient> consumptionCoefficients;
    bool gameActive;

    volatile bool coeffsUpdated;                 // set from async callback
    bool requestPollInFlight, requestPostInFlight;

    // ---------- helpers ----------
    uint32_t hostToNetworkLong     (uint32_t);
    uint64_t hostToNetworkLongLong (uint64_t);
    uint32_t networkToHostLong     (uint32_t);
    uint64_t networkToHostLongLong (uint64_t);
    uint16_t networkToHostShort    (uint16_t);
    String   boardTypeToString(BoardType) const;

    // parsing helpers
    bool parseProductionCoefficients (const uint8_t*, size_t);
    bool parseConsumptionCoefficients(const uint8_t*, size_t);
    void parsePollResponse(const uint8_t* data, size_t len);

public:
    ESPGameAPI(const String&, const String&, BoardType,
               unsigned long updateIntervalMs = 3000,
               unsigned long pollIntervalMs   = 5000);

    // Initialize certificate bundle (call in setup())
    static void initCertificateBundle();

    // authentication (synchronous)
    bool login(const String&, const String&);
    bool registerBoard();
    bool isGameRegistered() const { return isRegistered; }
    bool isFullyConnected() const { return isConnected(); }

    // callbacks for power data
    void setProductionCallback   (PowerCallback cb)       { productionCallback   = cb; }
    void setConsumptionCallback  (PowerCallback cb)       { consumptionCallback  = cb; }
    void setPowerPlantsCallback  (PowerPlantsCallback cb) { powerPlantsCallback  = cb; }
    void setConsumersCallback    (ConsumersCallback cb)   { consumersCallback    = cb; }

    // non‑blocking main loop helper
    bool update();     // call from loop()

    // Async API operations
    void pollCoefficients(CoefficientsCallback callback = nullptr);
    void getProductionValues(ProductionCallback callback);
    void getConsumptionValues(ConsumptionValCallback callback);
    void submitPowerData(float production, float consumption, AsyncCallback callback = nullptr);
    void reportConnectedPowerPlants(const std::vector<ConnectedPowerPlant>&, AsyncCallback callback = nullptr);
    void reportConnectedConsumers(const std::vector<ConnectedConsumer>&, AsyncCallback callback = nullptr);

    // getters
    const std::vector<ProductionCoefficient>&  getProductionCoefficients()  const { return productionCoefficients;  }
    const std::vector<ConsumptionCoefficient>& getConsumptionCoefficients() const { return consumptionCoefficients; }
    bool  isGameActive() const { return gameActive; }

    // config
    void setUpdateInterval(unsigned long ms) { updateInterval = ms; }
    void setPollInterval  (unsigned long ms) { pollInterval   = ms; }

    // network
    bool isConnected() const { return WiFi.status() == WL_CONNECTED && isLoggedIn && isRegistered; }

    // debug helpers (only emit if ESPGAMEAPI_ENABLE_SERIAL defined)
    void printStatus() const;
    void printCoefficients() const;
};

#endif // ESP_GAME_API_H
