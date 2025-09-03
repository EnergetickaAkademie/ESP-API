#pragma once
#include <string>
#include <vector>
#include <utility>
#include <functional>
#include <queue>
#include <memory>
#include <Arduino.h>  // Ensure Arduino core types (IPAddress) defined before lwIP macros

extern "C" {
  #include "esp_http_client.h"
  #include "esp_crt_bundle.h"
  #include "freertos/FreeRTOS.h"
  #include "freertos/task.h"
  #include "freertos/semphr.h"
  #include "esp_log.h"
}

// lwIP defines INADDR_NONE as a macro which collides with Arduino's extern variable
// AsyncRequest (rewritten) ----------------------------------------------------
// Thread-safe queued HTTP(S) requests using Arduino HTTPClient underneath.
// Preserves public interface so ESPGameAPI does not need changes.

#pragma once
#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <esp_err.h>
#include <vector>
#include <string>
#include <functional>

class AsyncRequest {
public:
  enum class Method { GET, POST };
  using DoneCB = std::function<void(esp_err_t,int,std::string)>;

  // Optional global configuration (call before first fetch)
  static void configure(uint8_t maxWorkers = 1, bool allowInsecureTLS = true) {
    if (s_started) return; // ignore if already running
    if (maxWorkers == 0) maxWorkers = 1;
    s_maxWorkers = maxWorkers;
    s_insecureTLS = allowInsecureTLS;
  }

  static void fetch(Method                                            method,
                    const std::string&                                url,
                    std::string                                       payload,
                    const std::vector<std::pair<std::string,std::string>>& headers,
                    DoneCB cb) {
    _init();
    RequestItem *item = new RequestItem{method,url,std::move(payload),headers,std::move(cb)};
    if (xQueueSend(s_queue, &item, 0) != pdTRUE) {
      // queue full
      if (item->callback) item->callback(ESP_FAIL,-1,"queue_full");
      delete item;
    }
  }

private:
  struct RequestItem {
    Method method;
    std::string url;
    std::string payload; // may contain binary
    std::vector<std::pair<std::string,std::string>> headers;
    DoneCB callback;
  };

  static inline QueueHandle_t s_queue = nullptr;
  static inline bool          s_started = false;
  static inline uint8_t       s_maxWorkers = 1;
  static inline bool          s_insecureTLS = true;

  static void _init() {
    if (s_started) return;
    s_queue = xQueueCreate(12, sizeof(RequestItem*)); // capacity 12 requests
    if (!s_queue) return; // OOM; subsequent fetch will fail to enqueue
    for (uint8_t i=0;i<s_maxWorkers;i++) {
      char name[16]; snprintf(name,sizeof(name),"httpw%u", i);
      xTaskCreatePinnedToCore(_worker, name, 6144, nullptr, tskIDLE_PRIORITY+1, nullptr, 1);
    }
    s_started = true;
  }

  static void _worker(void *arg) {
    (void)arg;
    for (;;) {
      RequestItem *job = nullptr;
      if (xQueueReceive(s_queue, &job, portMAX_DELAY) != pdTRUE || !job) continue;
      _execute(job);
      delete job;
    }
  }

  static void _execute(RequestItem *job) {
    int status = -1;
    std::string body;
    esp_err_t err = ESP_OK;

    // Choose client based on scheme
    bool isHttps = job->url.rfind("https://",0)==0;
    if (isHttps) {
      WiFiClientSecure secure;
      if (s_insecureTLS) secure.setInsecure();
      HTTPClient http;
      if (!http.begin(secure, job->url.c_str())) {
        err = ESP_FAIL; _finish(job, err, status, body); return; }
      _prepareAndSend(http, job, status, body, err);
      http.end();
    } else {
      WiFiClient client;
      HTTPClient http;
      if (!http.begin(client, job->url.c_str())) {
        err = ESP_FAIL; _finish(job, err, status, body); return; }
      _prepareAndSend(http, job, status, body, err);
      http.end();
    }
    _finish(job, err, status, body);
  }

  static void _prepareAndSend(HTTPClient &http, RequestItem *job, int &status, std::string &body, esp_err_t &err) {
    http.setTimeout(7000);
    // Set headers (HTTPClient needs addHeader before POST). For GET they will still be sent.
    for (auto &h: job->headers) http.addHeader(String(h.first.c_str()), String(h.second.c_str()));

    int code = -1;
    if (job->method == Method::POST) {
      if (!job->payload.empty()) {
        // Use binary safe posting
        code = http.POST((uint8_t*)job->payload.data(), job->payload.size());
      } else {
        code = http.POST((uint8_t*)nullptr, 0);
      }
    } else { // GET
      code = http.GET();
    }

    if (code > 0) {
      status = code;
      // Only read body for 2xx / 4xx typical responses (avoid huge downloads blindly)
      if (code != HTTP_CODE_NO_CONTENT) {
        WiFiClient *stream = http.getStreamPtr();
        if (stream) {
          // Read available into std::string (bounded to 64k to avoid memory explosion)
          const size_t CAP = 65536;
          body.reserve(1024);
          uint32_t lastActivity = millis();
          while (http.connected()) {
            size_t avail = stream->available();
            if (avail==0) {
              if (millis()-lastActivity>1500) break; // idle timeout
              vTaskDelay(5);
              continue;
            }
            uint8_t buf[512];
            size_t n = stream->readBytes(buf, std::min(avail,(size_t)sizeof(buf)));
            if (n==0) continue;
            lastActivity = millis();
            size_t room = CAP - body.size();
            if (room==0) break;
            size_t take = n<room? n: room;
            body.append((char*)buf, take);
            if (take < n) break; // cap reached
          }
        } else {
          // Fallback getString (still limited by memory)
          String tmp = http.getString();
          body.assign(tmp.c_str(), tmp.length());
        }
      }
    } else {
      err = ESP_FAIL;
    }
  }

  static void _finish(RequestItem *job, esp_err_t err, int status, std::string &body) {
    if (job->callback) job->callback(err, status, body);
  }
};
      ctx->logFail(ESP_FAIL);
      ctx->finish(ESP_FAIL, -1);
      delete ctx;
      return;
    }

    if (request->method == Method::POST) {
      esp_http_client_set_method(ctx->client, HTTP_METHOD_POST);
      if (!ctx->payload.empty())
        esp_http_client_set_post_field(ctx->client,
                                       ctx->payload.c_str(),
                                       ctx->payload.size());
    }

    for (auto& h : request->headers)
      esp_http_client_set_header(ctx->client,
                                 h.first.c_str(), h.second.c_str());

    // Execute the request synchronously
    esp_err_t r = esp_http_client_perform(ctx->client);
    if (r == ESP_OK) {
      ctx->finish(ESP_OK, esp_http_client_get_status_code(ctx->client));
    } else {
      ctx->logFail(r);
      ctx->finish(r, -1);
    }

    esp_http_client_cleanup(ctx->client);
    delete ctx;
  }

  static esp_err_t _event(esp_http_client_event_t* e){
    auto* ctx = static_cast<Ctx*>(e->user_data);
    if (e->event_id == HTTP_EVENT_ON_DATA &&
        !esp_http_client_is_chunked_response(e->client))
      ctx->body.append(static_cast<char*>(e->data), e->data_len);
    return ESP_OK;
  }
};
