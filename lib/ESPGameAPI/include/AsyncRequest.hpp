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

// Fallback insecure client (only used if no certificate validation is configured)
#include <WiFiClientSecure.h>

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
  const bool isHttps = request->url.rfind("https://", 0) == 0;

    // If HTTPS and we have chosen to ignore certificates, use a manual insecure client path.
    // This avoids esp_http_client rejecting the config due to missing verification options.
    if (isHttps) {
      // Basic URL parsing: https://host[:port]/path
      std::string urlNoScheme = ctx->url.substr(8); // after https://
      std::string hostPort, path;
      size_t slashPos = urlNoScheme.find('/');
      if (slashPos == std::string::npos) {
        hostPort = urlNoScheme;
        path = "/";
      } else {
        hostPort = urlNoScheme.substr(0, slashPos);
        path = urlNoScheme.substr(slashPos);
      }
      std::string host = hostPort;
      int port = 443;
      size_t colonPos = hostPort.find(':');
      if (colonPos != std::string::npos) {
        host = hostPort.substr(0, colonPos);
        try { port = std::stoi(hostPort.substr(colonPos + 1)); } catch(...) { port = 443; }
      }

      WiFiClientSecure secureClient;
      secureClient.setInsecure(); // accept all certs (INSECURE)
      if (!secureClient.connect(host.c_str(), port)) {
        ctx->logFail(ESP_ERR_HTTP_CONNECT);
        ctx->finish(ESP_ERR_HTTP_CONNECT, -1);
        delete ctx;
        return;
      }

      // Build HTTP request manually
      std::string methodStr = (ctx->method == Method::POST ? "POST" : "GET");
      std::string requestLine = methodStr + " " + path + " HTTP/1.1\r\n";
      secureClient.print(requestLine.c_str());
      secureClient.print("Host: "); secureClient.print(host.c_str()); secureClient.print("\r\n");
      secureClient.print("User-Agent: AsyncRequest/1.0\r\n");
      secureClient.print("Connection: close\r\n");
      bool hasContentLength = false;
      for (auto &h : request->headers) {
        secureClient.print(h.first.c_str());
        secureClient.print(": ");
        secureClient.print(h.second.c_str());
        secureClient.print("\r\n");
        if (strcasecmp(h.first.c_str(), "Content-Length") == 0) hasContentLength = true;
      }
      if (ctx->method == Method::POST && !hasContentLength) {
        secureClient.print("Content-Length: ");
        secureClient.print((int)ctx->payload.size());
        secureClient.print("\r\n");
      }
      secureClient.print("\r\n");
      if (ctx->method == Method::POST && !ctx->payload.empty()) {
        secureClient.write((const uint8_t*)ctx->payload.data(), ctx->payload.size());
      }

      // Read status line
      std::string statusLine;
      while (secureClient.connected()) {
        int c = secureClient.read();
        if (c < 0) { delay(1); continue; }
        if (c == '\r') continue;
        if (c == '\n') break;
        statusLine.push_back((char)c);
      }
      int statusCode = -1;
      if (statusLine.rfind("HTTP/", 0) == 0) {
        size_t sp1 = statusLine.find(' ');
        if (sp1 != std::string::npos) {
          size_t sp2 = statusLine.find(' ', sp1 + 1);
          std::string codeStr = statusLine.substr(sp1 + 1, sp2 - (sp1 + 1));
          try { statusCode = std::stoi(codeStr); } catch(...) { statusCode = -1; }
        }
      }
      // Skip headers
      std::string headerLine;
      while (secureClient.connected()) {
        int c = secureClient.read();
        if (c < 0) { delay(1); continue; }
        if (c == '\r') continue;
        if (c == '\n') {
          if (headerLine.empty()) break; // end of headers
          headerLine.clear();
        } else {
          headerLine.push_back((char)c);
        }
      }
      // Read body
      while (secureClient.connected() || secureClient.available()) {
        int c = secureClient.read();
        if (c < 0) { delay(1); continue; }
        ctx->body.push_back((char)c);
      }
      ctx->finish(statusCode >= 0 ? ESP_OK : ESP_FAIL, statusCode);
      delete ctx;
      return;
    }

    esp_http_client_config_t cfg = {};
    cfg.url = ctx->url.c_str();
    cfg.event_handler = _event;
    cfg.user_data = ctx;
    cfg.is_async = false; // Always synchronous now for queue control
    cfg.timeout_ms = 7000;
    cfg.crt_bundle_attach = nullptr;            // no CA
    cfg.skip_cert_common_name_check = true;     // ignore hostname

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
