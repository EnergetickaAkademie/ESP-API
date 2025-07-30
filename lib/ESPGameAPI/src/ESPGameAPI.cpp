#include "ESPGameAPI.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

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

// ───────────────────────────────────────────── login / register (unchanged)
//  … full original blocking implementations kept – omitted here for brevity …
#include <ArduinoJson.h>      // needed by original code

bool ESPGameAPI::login(const String& user,const String& pass){ /* original body */ }
bool ESPGameAPI::registerBoard(){ /* original body */ }

// ───────────────────────────────────────────── Sync HTTP helpers (original)
bool ESPGameAPI::makeHttpRequest(const String& ep,const uint8_t* d,size_t ds,
                                 uint8_t* r,size_t& rs){ /* original body */ }
bool ESPGameAPI::makeHttpGetRequest(const String& ep,uint8_t* r,size_t& rs){/*…*/}

// ───────────────────────────────────────────── Async plumbing
struct ESPGameAPI::AsyncRequestCtx {
    ESPGameAPI* api;
    bool isPost;
    String url;
    std::vector<uint8_t> payload;
    std::function<void(bool,const uint8_t*,size_t)> cb;
};

void ESPGameAPI::httpTask(void* pv) {
    AsyncRequestCtx* ctx = static_cast<AsyncRequestCtx*>(pv);
    HTTPClient http;
    http.begin(ctx->url);
    http.addHeader("Authorization", "Bearer " + ctx->api->token);

    bool ok = false;
    std::vector<uint8_t> resp;

    if(ctx->isPost) {
        http.addHeader("Content-Type", "application/octet-stream");
        int code = http.POST(ctx->payload.data(), ctx->payload.size());
        ok = (code == 200);
    } else {
        int code = http.GET();
        ok = (code == 200);
    }

    if(ok) {
        WiFiClient* s = http.getStreamPtr();
        size_t len = s->available();
        resp.resize(len);
        if(len) s->readBytes(resp.data(), len);
    }
    http.end();

    if(ctx->cb) ctx->cb(ok, resp.data(), resp.size());
    delete ctx;                                      // cleanup
    vTaskDelete(nullptr);
}

bool ESPGameAPI::makeHttpRequestAsync(const String& ep,const uint8_t* d,size_t ds,
              std::function<void(bool,const uint8_t*,size_t)> cb){
    if(!isLoggedIn) return false;
    auto *ctx = new AsyncRequestCtx;
    ctx->api     = this;
    ctx->isPost  = true;
    ctx->url     = baseUrl + ep;
    ctx->payload.assign(d, d+ds);
    ctx->cb      = cb;
    xTaskCreatePinnedToCore(httpTask,"apiPOST",8192,ctx,1,nullptr,1);
    return true;
}

bool ESPGameAPI::makeHttpGetRequestAsync(const String& ep,
              std::function<void(bool,const uint8_t*,size_t)> cb){
    if(!isLoggedIn) return false;
    auto *ctx = new AsyncRequestCtx;
    ctx->api    = this;
    ctx->isPost = false;
    ctx->url    = baseUrl + ep;
    ctx->cb     = cb;
    xTaskCreatePinnedToCore(httpTask,"apiGET",8192,ctx,1,nullptr,1);
    return true;
}

// ───────────────────────────────────────────── Async variants of main ops
bool ESPGameAPI::pollCoefficients(){
    if(!isRegistered) return false;
    requestPollInFlight = true;
    return makeHttpGetRequestAsync("/coreapi/poll_binary",
        [this](bool ok,const uint8_t* buf,size_t len){
            if(ok){
                if(len==0){             // game paused
                    gameActive = false;
                    productionCoefficients.clear();
                    consumptionCoefficients.clear();
                }else if(len>=2){
                    uint8_t pc = buf[0]; size_t off=1;
                    if(len < off+pc*5+1) return;      // malformed
                    productionCoefficients.clear();
                    for(uint8_t i=0;i<pc;i++){
                        ProductionCoefficient c;
                        c.source_id = buf[off];
                        c.coefficient = (float)networkToHostLong(
                                            *(uint32_t*)(buf+off+1))/1000.0f;
                        productionCoefficients.push_back(c);
                        off+=5;
                    }
                    uint8_t cc = buf[off++];
                    if(len < off+cc*5) return;
                    consumptionCoefficients.clear();
                    for(uint8_t i=0;i<cc;i++){
                        ConsumptionCoefficient c;
                        c.building_id = buf[off];
                        c.consumption = (float)networkToHostLong(
                                            *(uint32_t*)(buf+off+1))/1000.0f;
                        consumptionCoefficients.push_back(c);
                        off+=5;
                    }
                    gameActive = true;
                }
                coeffsUpdated = true;
            }
            requestPollInFlight = false;
        });
}

bool ESPGameAPI::submitPowerData(float prod,float cons){
    if(!isRegistered) return false;
    PowerDataRequest r;
    r.production  = hostToNetworkLong((int32_t)(prod*1000));
    r.consumption = hostToNetworkLong((int32_t)(cons*1000));

    requestPostInFlight = true;
    return makeHttpRequestAsync("/coreapi/post_vals",
        (uint8_t*)&r,sizeof(r),
        [this](bool, const uint8_t*, size_t){ requestPostInFlight = false; });
}

bool ESPGameAPI::reportConnectedPowerPlants(const std::vector<ConnectedPowerPlant>& p){
    if(!isRegistered) return false;
    std::vector<uint8_t> data(1 + p.size()*8);
    data[0] = p.size();
    size_t off = 1;
    for(const auto& pl: p){
        *(uint32_t*)&data[off]   = hostToNetworkLong(pl.plant_id);
        *(int32_t *)&data[off+4] = hostToNetworkLong((int32_t)(pl.set_power*1000));
        off += 8;
    }
    return makeHttpRequestAsync("/coreapi/prod_connected",
                                data.data(), data.size());
}

bool ESPGameAPI::reportConnectedConsumers(const std::vector<ConnectedConsumer>& c){
    if(!isRegistered) return false;
    std::vector<uint8_t> data(1 + c.size()*4);
    data[0] = c.size();
    size_t off = 1;
    for(const auto& cn: c){
        *(uint32_t*)&data[off] = hostToNetworkLong(cn.consumer_id);
        off += 4;
    }
    return makeHttpRequestAsync("/coreapi/cons_connected",
                                data.data(), data.size());
}

// getProductionValues / getConsumptionValues keep the original blocking code
bool ESPGameAPI::getProductionValues(){ /* unchanged */ }
bool ESPGameAPI::getConsumptionValues(){ /* unchanged */ }

// ───────────────────────────────────────────── Non‑blocking loop helper
bool ESPGameAPI::update(){
    if(!isConnected()) return false;

    unsigned long now = millis();
    // schedule coefficient poll
    if(!requestPollInFlight && now - lastPollTime >= pollInterval){
        lastPollTime = now;
        pollCoefficients();           // fire‑and‑forget
    }
    // schedule power post
    if(gameActive && !requestPostInFlight && now - lastUpdateTime >= updateInterval){
        lastUpdateTime = now;

        if(powerPlantsCallback)   reportConnectedPowerPlants(powerPlantsCallback());
        if(consumersCallback)     reportConnectedConsumers(consumersCallback());

        if(productionCallback && consumptionCallback)
            submitPowerData(productionCallback(), consumptionCallback());
    }

    bool ret = coeffsUpdated;
    coeffsUpdated = false;        // one‑shot flag
    return ret;
}

// ───────────────────────────────────────────── Debug helpers (silenced unless enabled)
void ESPGameAPI::printStatus() const {/* original body uses Serial – left intact */}
void ESPGameAPI::printCoefficients() const {/* original body */}
