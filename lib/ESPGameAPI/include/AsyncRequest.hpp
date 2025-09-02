#pragma once
#include <string>
#include <vector>
#include <utility>
#include <functional>
#include <queue>
#include <memory>

extern "C" {
  #include "esp_http_client.h"
  #include "esp_crt_bundle.h"
  #include "freertos/FreeRTOS.h"
  #include "freertos/task.h"
  #include "freertos/semphr.h"
  #include "esp_log.h"
}

class AsyncRequest {
public:
  enum class Method { GET, POST };
  using DoneCB = std::function<void(esp_err_t,int,std::string)>;

private:
  struct RequestItem {
    Method method;
    std::string url;
    std::string payload;
    std::vector<std::pair<std::string, std::string>> headers;
    DoneCB callback;
  };

  static std::queue<RequestItem*> requestQueue;
  static SemaphoreHandle_t queueMutex;
  static bool isProcessing;
  static TaskHandle_t queueProcessorTask;

  static void initializeQueue() {
    if (!queueMutex) {
      queueMutex = xSemaphoreCreateMutex();
    }
  }

public:
  static void fetch(Method                                            method,
                    const std::string&                                url,
                    std::string                                       payload,      // moved in
                    const std::vector<std::pair<std::string,
                                                 std::string>>&       headers,
                    DoneCB                                            cb)
  {
    initializeQueue();
    
    auto* request = new RequestItem();
    request->method = method;
    request->url = url;
    request->payload = std::move(payload);
    request->headers = headers;
    request->callback = std::move(cb);

    // Add request to queue
    if (xSemaphoreTake(queueMutex, portMAX_DELAY) == pdTRUE) {
      requestQueue.push(request);
      
      // Start queue processor if not already running
      if (!isProcessing) {
        isProcessing = true;
        xTaskCreate(_queueProcessor, "httpqueue", 6144, nullptr,
                    tskIDLE_PRIORITY + 1, &queueProcessorTask);
      }
      
      xSemaphoreGive(queueMutex);
    }
  }

private:
  struct Ctx {
    esp_http_client_handle_t client{nullptr};
    std::string              payload;
    std::string              body;
    std::string              url;
    Method                   method{Method::GET};
    DoneCB                   cb;

    void finish(esp_err_t err,int status){
      if(cb) cb(err,status,body);
    }
    void logFail(esp_err_t err){
      // The payload can be binary that is not printable,
      // so we only log the URL and error.
      ESP_LOGE("AsyncRequest", "❌ HTTP %s %s\n"
                              "   err  = %s\n"
                              "   len  = %d",
                              method==Method::POST?"POST":"GET",
                              url.c_str(),
                              esp_err_to_name(err),
                              (int)payload.size());
    }
  };

  static void _queueProcessor(void* arg) {
    while (true) {
      RequestItem* request = nullptr;
      
      // Get next request from queue
      if (xSemaphoreTake(queueMutex, portMAX_DELAY) == pdTRUE) {
        if (!requestQueue.empty()) {
          request = requestQueue.front();
          requestQueue.pop();
        }
        xSemaphoreGive(queueMutex);
      }
      
      if (!request) {
        // No more requests, stop processing
        if (xSemaphoreTake(queueMutex, portMAX_DELAY) == pdTRUE) {
          isProcessing = false;
          xSemaphoreGive(queueMutex);
        }
        break;
      }
      
      // Process the request synchronously
      _executeRequest(request);
      delete request;
    }
    
    vTaskDelete(nullptr);
  }

  static void _executeRequest(RequestItem* request) {
    auto* ctx = new Ctx;
    ctx->cb = std::move(request->callback);
    ctx->payload = std::move(request->payload);
    ctx->url = request->url;
    ctx->method = request->method;

    const bool isHttp = request->url.rfind("http://", 0) == 0;

    esp_http_client_config_t cfg = {};
    cfg.url = ctx->url.c_str();
    cfg.event_handler = _event;
    cfg.user_data = ctx;
    cfg.is_async = false; // Always synchronous now for queue control
    cfg.timeout_ms = 7000;

  // --- TLS configuration --------------------------------------------------
  // Attach built-in certificate bundle but skip hostname verification.
  // This avoids esp-tls "no server verification" errors while still
  // relaxing the certificate's common-name check.
  cfg.crt_bundle_attach = esp_crt_bundle_attach;
  cfg.skip_cert_common_name_check = true;     // allow mismatched hostnames

    ctx->client = esp_http_client_init(&cfg);
    if (!ctx->client) { 
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
