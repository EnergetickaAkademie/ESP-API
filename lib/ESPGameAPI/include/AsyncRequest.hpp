// Clean rewritten AsyncRequest (C++11 compatible)
// Enhanced: persistent per-worker HTTPClient + socket reuse for speed (inspired by experimental main.cpp)
#pragma once
#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <esp_err.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <vector>
#include <string>
#include <functional>
#include <algorithm>
#include <stdlib.h>

#ifndef ASYNCREQUEST_QUEUE_LEN
#define ASYNCREQUEST_QUEUE_LEN 12
#endif
#ifndef ASYNCREQUEST_WORKER_STACK
#define ASYNCREQUEST_WORKER_STACK 6144
#endif
#ifndef ASYNCREQUEST_IDLE_TIMEOUT_MS
#define ASYNCREQUEST_IDLE_TIMEOUT_MS 15000   // increased from 1500ms -> ~10s
#endif
#ifndef ASYNCREQUEST_BODY_CAP_BYTES
#define ASYNCREQUEST_BODY_CAP_BYTES 65536
#endif
#ifndef ASYNCREQUEST_CONNECT_TIMEOUT_MS
#define ASYNCREQUEST_CONNECT_TIMEOUT_MS 15000 // increased from 1500ms -> ~10s
#endif
#ifndef ASYNCREQUEST_FORCE_CLOSE
#define ASYNCREQUEST_FORCE_CLOSE 0   // 1 = disable keep-alive reuse
#endif
#ifndef ASYNCREQUEST_DEBUG
#define ASYNCREQUEST_DEBUG 1         // set 0 to silence
#endif

#if ASYNCREQUEST_DEBUG
  #define AR_LOGf(...)  Serial.printf(__VA_ARGS__)
#else
  #define AR_LOGf(...)  do{}while(0)
#endif

class AsyncRequest {
public:
  enum class Method { GET, POST };
  typedef std::function<void(esp_err_t,int,std::string)> DoneCB;

  static void configure(uint8_t maxWorkers = 1, bool allowInsecureTLS = true) {
    if (started_) return;
    if (maxWorkers == 0) maxWorkers = 1;
    maxWorkers_ = maxWorkers;
    insecureTLS_ = allowInsecureTLS;
  }

  // Backwards compatibility wrapper (legacy signature). Ignores most params now.
  static bool begin(uint8_t maxWorkers, uint8_t /*queueLenIgnored*/, uint32_t /*stackIgnored*/, UBaseType_t /*prioIgnored*/, BaseType_t /*coreIgnored*/) {
    configure(maxWorkers, true);
    return true; // always succeeds with current lightweight implementation
  }

  static void fetch(Method method,
                    const std::string &url,
                    std::string payload,
                    const std::vector<std::pair<std::string,std::string>> &headers,
                    DoneCB cb) {
    init_();
    if (!queue_) {
      if (cb) cb(ESP_FAIL,-1,"no_queue");
      return;
    }
    Request *r = new Request{method,url,std::move(payload),headers,cb, millis()};
    if (ASYNCREQUEST_DEBUG) {
      UBaseType_t used = uxQueueMessagesWaiting(queue_);
      UBaseType_t avail = uxQueueSpacesAvailable(queue_);
      AR_LOGf("[AsyncRequest] -> enqueue %s %s q=%u/%u\n",
              method==Method::GET?"GET":"POST", url.c_str(), used, used+avail);
    }
    if (xQueueSend(queue_, &r, 0) != pdTRUE) {
      AR_LOGf("[AsyncRequest] DROP queue_full %s %s\n", method==Method::GET?"GET":"POST", url.c_str());
      if (cb) cb(ESP_FAIL,-1,"queue_full");
      delete r;
    }
  }

private:
  struct Request {
    Method method;
    std::string url;
    std::string payload;
    std::vector<std::pair<std::string,std::string>> headers;
    DoneCB cb;
    uint32_t t_enq;
  };

  struct Origin { bool https; std::string host; uint16_t port; };
  struct WorkerCtx {
    HTTPClient http;          // persistent HTTPClient
    WiFiClient       *plain;  // owned when active
    WiFiClientSecure *secure; // owned when active
    bool hasOrigin;
    Origin origin;
    std::string originKey;    // scheme:host:port
    WorkerCtx(): plain(NULL), secure(NULL), hasOrigin(false) {
      http.setReuse(true);
    }
    ~WorkerCtx(){ resetClients(); }
    void resetClients(){ if(secure){ delete secure; secure=NULL;} if(plain){ delete plain; plain=NULL;} hasOrigin=false; originKey.clear(); }
  };

  static QueueHandle_t queue_;
  static bool started_;
  static uint8_t maxWorkers_;
  static bool insecureTLS_;
  static volatile uint32_t activeWorkers_;

  static void init_() {
    if (started_) return;
    queue_ = xQueueCreate(ASYNCREQUEST_QUEUE_LEN, sizeof(Request*));
    if (!queue_) return;
    for (uint8_t i=0;i<maxWorkers_;++i) {
      char name[12]; snprintf(name,sizeof(name),"reqW%u", i);
      xTaskCreatePinnedToCore(worker_, name, ASYNCREQUEST_WORKER_STACK, NULL, tskIDLE_PRIORITY+1, NULL, 1);
    }
    started_ = true;
  }

  static bool parseOrigin_(const std::string &url, Origin &o, std::string &key) {
    size_t posScheme = url.find("://");
    std::string scheme = posScheme!=std::string::npos ? url.substr(0,posScheme) : "";
    o.https = (scheme == "https" || scheme == "HTTPS");
    size_t hostStart = posScheme!=std::string::npos ? posScheme+3 : 0;
    size_t pathStart = url.find('/', hostStart);
    std::string hostport = pathStart!=std::string::npos ? url.substr(hostStart, pathStart-hostStart) : url.substr(hostStart);
    size_t colon = hostport.find(':');
    if (colon!=std::string::npos) {
      o.host = hostport.substr(0,colon);
      o.port = (uint16_t)atoi(hostport.c_str()+colon+1);
    } else {
      o.host = hostport;
      o.port = o.https ? 443 : 80;
    }
    if (o.host.empty()) return false;
    key.clear();
    key.append(o.https?"s:":"h:").append(o.host).append(":");
    char num[8]; snprintf(num,sizeof(num),"%u", (unsigned)o.port); key.append(num);
    return true;
  }

  static void worker_(void *arg) {
    (void)arg;
    WorkerCtx ctx;
  #if ASYNCREQUEST_FORCE_CLOSE
    ctx.http.useHTTP10(true);
  #else
    ctx.http.useHTTP10(false);
  #endif
    for(;;){
      Request *req=NULL;
      if (xQueueReceive(queue_, &req, portMAX_DELAY) != pdTRUE || !req) continue;
      activeWorkers_++;
      uint32_t t_start = millis();

      // Prepare origin / client reuse
      Origin want; std::string wantKey;
      parseOrigin_(req->url, want, wantKey);
      if (!ctx.hasOrigin || wantKey != ctx.originKey) {
        ctx.resetClients();
        if (want.https) {
          ctx.secure = new WiFiClientSecure();
          if (insecureTLS_ && ctx.secure) ctx.secure->setInsecure();
        } else {
          ctx.plain = new WiFiClient();
        }
        ctx.origin = want; ctx.originKey = wantKey; ctx.hasOrigin = true;
      }

      // Begin request
      bool began=false; uint32_t t0 = millis();
      if (ctx.hasOrigin) {
  #if ASYNCREQUEST_FORCE_CLOSE
        ctx.http.addHeader("Connection","close");
  #endif
  ctx.http.setConnectTimeout(ASYNCREQUEST_CONNECT_TIMEOUT_MS);
  ctx.http.setTimeout(ASYNCREQUEST_IDLE_TIMEOUT_MS); // was 7000ms, align with ~10s request
        if (ctx.origin.https && ctx.secure) began = ctx.http.begin(*ctx.secure, req->url.c_str());
        else if (!ctx.origin.https && ctx.plain) began = ctx.http.begin(*ctx.plain, req->url.c_str());
      }

      int status=-1; esp_err_t err=ESP_OK; std::string body; uint32_t t1=millis();
      if (began) {
        for(auto &h: req->headers) ctx.http.addHeader(String(h.first.c_str()), String(h.second.c_str()));
        int code;
        if (req->method == Method::POST) {
          if (!req->payload.empty()) code = ctx.http.POST((uint8_t*)req->payload.data(), req->payload.size());
          else                       code = ctx.http.POST((uint8_t*)NULL,0);
        } else {
          code = ctx.http.GET();
        }
        uint32_t t2 = millis();
        if (code > 0) {
          status = code;
          if (code != HTTP_CODE_NO_CONTENT) {
            int len = ctx.http.getSize();
            WiFiClient *stream = ctx.http.getStreamPtr();
            if (len > 0 && stream) {
              // known length
              body.reserve(len < (int)ASYNCREQUEST_BODY_CAP_BYTES ? (size_t)len : 1024);
              size_t readTot=0; uint32_t lastAct = millis();
              while (readTot < (size_t)len && ctx.http.connected()) {
                size_t avail = stream->available();
                if (!avail) { if (millis()-lastAct > ASYNCREQUEST_IDLE_TIMEOUT_MS) break; vTaskDelay(2); continue; }
                uint8_t buf[512]; size_t wantSz = avail > sizeof(buf)? sizeof(buf): avail;
                size_t n = stream->readBytes(buf, wantSz); if(!n) continue; lastAct = millis(); readTot += n;
                size_t room = ASYNCREQUEST_BODY_CAP_BYTES - body.size(); if (!room) break; size_t take = n < room? n: room; body.append((char*)buf, take); if (take < n) break;
              }
            } else {
              // unknown length (chunked) fallback
              String tmp = ctx.http.getString();
              if (tmp.length() > (int)ASYNCREQUEST_BODY_CAP_BYTES) tmp.remove(ASYNCREQUEST_BODY_CAP_BYTES);
              body.assign(tmp.c_str(), tmp.length());
            }
          }
        } else {
          err = ESP_FAIL;
        }
  uint32_t t3 = millis();
  ctx.http.end(); // will keep socket if reuse & server allowed keep-alive
  if (ASYNCREQUEST_DEBUG) {
    AR_LOGf("[TIMING] method=%s url=%s | inQ=%lums | conn+tls+hdr=%lums | body=%lums | total=%lums | status=%d | bodyB=%u | active=%u\n",
      req->method==Method::GET?"GET":"POST", req->url.c_str(),
      (unsigned long)(t_start - req->t_enq),
      (unsigned long)(t2 - t1),
      (unsigned long)(t3 - t2),
      (unsigned long)(t3 - t_start),
      status, (unsigned)body.size(), (unsigned)activeWorkers_);
  }
      } else {
        err = ESP_FAIL; uint32_t t3=millis();
        if (ASYNCREQUEST_DEBUG) {
    AR_LOGf("[TIMING] method=%s url=%s | inQ=%lums | beginFail | total=%lums | active=%u\n",
      req->method==Method::GET?"GET":"POST", req->url.c_str(),
      (unsigned long)(t_start-req->t_enq), (unsigned long)(t3-t_start), (unsigned)activeWorkers_);
        }
      }
      finish_(req, err, status, body);
      delete req; activeWorkers_--;
    }
  }

  static void finish_(Request *req, esp_err_t err, int status, std::string &body) {
    if (req->cb) req->cb(err,status,body);
  }
};

