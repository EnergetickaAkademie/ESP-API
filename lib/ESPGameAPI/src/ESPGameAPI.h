#ifndef ESP_GAME_API_H
#define ESP_GAME_API_H

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <vector>
#include <functional>

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

struct __attribute__((packed)) PowerDataRequest { int32_t production; int32_t consumption; };
struct __attribute__((packed)) ProductionEntry  { uint8_t source_id; int32_t coefficient; };
struct __attribute__((packed)) ConsumptionEntry { uint8_t building_id; int32_t consumption; };
struct __attribute__((packed)) PowerPlantEntry  { uint32_t plant_id;  int32_t set_power;  };
struct __attribute__((packed)) ConsumerEntry    { uint32_t consumer_id; };

// ───────────────────────────────────────────────────────────────────────────────
class ESPGameAPI {
private:
    // ---------- unchanged user‑visible state ----------
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

    HTTPClient http;         // kept for blocking helpers

    // ---------- async plumbing ----------
    struct AsyncRequestCtx;                      // fwd
    static void httpTask(void*);                 // FreeRTOS task entry
    bool makeHttpRequestAsync(const String&, const uint8_t*, size_t,
                              std::function<void(bool,const uint8_t*,size_t)> = nullptr);
    bool makeHttpGetRequestAsync(const String&,
                                 std::function<void(bool,const uint8_t*,size_t)>);

    volatile bool coeffsUpdated;                 // set from async callback
    bool requestPollInFlight, requestPostInFlight;

    // ---------- helpers ----------
    uint32_t hostToNetworkLong     (uint32_t);
    uint64_t hostToNetworkLongLong (uint64_t);
    uint32_t networkToHostLong     (uint32_t);
    uint64_t networkToHostLongLong (uint64_t);
    uint16_t networkToHostShort    (uint16_t);
    String   boardTypeToString(BoardType) const;

    // synchronous HTTP helpers (still used for login / registration)
    bool makeHttpRequest (const String&, const uint8_t*, size_t, uint8_t*, size_t&);
    bool makeHttpGetRequest(const String&, uint8_t*, size_t&);

    // parsing helpers
    bool parseProductionCoefficients (const uint8_t*, size_t);
    bool parseConsumptionCoefficients(const uint8_t*, size_t);

public:
    ESPGameAPI(const String&, const String&, BoardType,
               unsigned long updateIntervalMs = 3000,
               unsigned long pollIntervalMs   = 5000);

    // authentication
    bool login(const String&, const String&);
    bool registerBoard();
    bool isGameRegistered() const { return isRegistered; }
    bool isFullyConnected() const { return isConnected(); }

    // callbacks
    void setProductionCallback   (PowerCallback cb)       { productionCallback   = cb; }
    void setConsumptionCallback  (PowerCallback cb)       { consumptionCallback  = cb; }
    void setPowerPlantsCallback  (PowerPlantsCallback cb) { powerPlantsCallback  = cb; }
    void setConsumersCallback    (ConsumersCallback cb)   { consumersCallback    = cb; }

    // non‑blocking main loop helper
    bool update();     // call from loop()

    // manual ops – now all return immediately (fire‑and‑forget)
    bool pollCoefficients();
    bool getProductionValues();     // still synchronous
    bool getConsumptionValues();    // still synchronous
    bool submitPowerData(float production, float consumption);
    bool reportConnectedPowerPlants(const std::vector<ConnectedPowerPlant>&);
    bool reportConnectedConsumers (const std::vector<ConnectedConsumer>&);

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
