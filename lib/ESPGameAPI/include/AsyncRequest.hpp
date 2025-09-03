// Clean rewritten AsyncRequest (C++11 compatible)
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

#ifndef ASYNCREQUEST_QUEUE_LEN
#define ASYNCREQUEST_QUEUE_LEN 12
#endif
#ifndef ASYNCREQUEST_WORKER_STACK
#define ASYNCREQUEST_WORKER_STACK 6144
#endif
#ifndef ASYNCREQUEST_IDLE_TIMEOUT_MS
#define ASYNCREQUEST_IDLE_TIMEOUT_MS 1500
#endif
#ifndef ASYNCREQUEST_BODY_CAP_BYTES
#define ASYNCREQUEST_BODY_CAP_BYTES 65536
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
    
    // Debug: Log request details and queue status
    UBaseType_t queueWaiting = uxQueueMessagesWaiting(queue_);
    UBaseType_t queueSpaces = uxQueueSpacesAvailable(queue_);
    Serial.printf("[AsyncRequest] 📤 %s %s (Queue: %u/%u)\n", 
        method == Method::GET ? "GET" : "POST", 
        url.c_str(), 
        queueWaiting, 
        queueWaiting + queueSpaces);
    
    Request *r = new Request{method,url,std::move(payload),headers,cb};
    if (xQueueSend(queue_, &r, 0) != pdTRUE) {
      Serial.printf("[AsyncRequest] ❌ Queue full! Dropping %s %s\n", 
          method == Method::GET ? "GET" : "POST", url.c_str());
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
  };

  static QueueHandle_t queue_;
  static bool started_;
  static uint8_t maxWorkers_;
  static bool insecureTLS_;

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

  static void worker_(void *arg) {
    (void)arg;
    for (;;) {
      Request *req = NULL;
      if (xQueueReceive(queue_, &req, portMAX_DELAY) != pdTRUE || !req) continue;
      execute_(req);
      delete req;
    }
  }

  static void execute_(Request *req) {
    uint32_t startTime = millis();
    esp_err_t err = ESP_OK;
    int status = -1;
    std::string body;

    bool https = req->url.rfind("https://",0)==0;
    Serial.printf("[AsyncRequest] ⚡ Processing %s %s\n", 
        req->method == Method::GET ? "GET" : "POST", req->url.c_str());
    
    if (https) {
      WiFiClientSecure cli;
      if (insecureTLS_) cli.setInsecure();
      HTTPClient http;
      if (!http.begin(cli, req->url.c_str())) { err = ESP_FAIL; finish_(req, err, status, body); return; }
      sendAndRead_(http, req, status, body, err);
      http.end();
    } else {
      WiFiClient cli;
      HTTPClient http;
      if (!http.begin(cli, req->url.c_str())) { err = ESP_FAIL; finish_(req, err, status, body); return; }
      sendAndRead_(http, req, status, body, err);
      http.end();
    }
    
    uint32_t duration = millis() - startTime;
    Serial.printf("[AsyncRequest] ✅ Completed %s %s in %lu ms (status: %d, body: %u bytes)\n", 
        req->method == Method::GET ? "GET" : "POST", req->url.c_str(), 
        duration, status, body.length());
    
    finish_(req, err, status, body);
  }

  static void sendAndRead_(HTTPClient &http, Request *req, int &status, std::string &body, esp_err_t &err) {
    http.setTimeout(7000);
    for (auto &h : req->headers) http.addHeader(String(h.first.c_str()), String(h.second.c_str()));
    int code;
    if (req->method == Method::POST) {
      if (!req->payload.empty())
        code = http.POST((uint8_t*)req->payload.data(), req->payload.size());
      else
        code = http.POST((uint8_t*)NULL, 0);
    } else {
      code = http.GET();
    }
    if (code > 0) {
      status = code;
      if (code != HTTP_CODE_NO_CONTENT) {
        WiFiClient *stream = http.getStreamPtr();
        if (stream) {
          body.reserve(1024);
          uint32_t lastAct = millis();
          while (http.connected()) {
            size_t avail = stream->available();
            if (!avail) {
              if (millis()-lastAct > ASYNCREQUEST_IDLE_TIMEOUT_MS) break;
              vTaskDelay(5);
              continue;
            }
            uint8_t buf[512];
            size_t n = stream->readBytes(buf, avail > sizeof(buf)? sizeof(buf): avail);
            if (!n) continue;
            lastAct = millis();
            size_t room = ASYNCREQUEST_BODY_CAP_BYTES - body.size();
            if (!room) break;
            size_t take = n < room? n: room;
            body.append((char*)buf, take);
            if (take < n) break;
          }
        } else {
          String tmp = http.getString();
          body.assign(tmp.c_str(), tmp.length());
        }
      }
    } else {
      err = ESP_FAIL;
    }
  }

  static void finish_(Request *req, esp_err_t err, int status, std::string &body) {
    if (req->cb) req->cb(err,status,body);
  }
};

