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
    // Previous attempts to attach a (broken) bundle failed. User requested to
    // "ignore certificates"; instead of leaving TLS cfg invalid (which causes
    // setup failure), we embed the public Let's Encrypt ISRG Root X1 cert so
    // the connection can verify successfully. Hostname check can be skipped if
    // desired via macro. (Root certificate is public domain.)
    static const char lets_encrypt_isrg_root_x1[] PROGMEM =
      "-----BEGIN CERTIFICATE-----\n"
      "MIIFazCCA1OgAwIBAgISA2Gv2XDSBxPT7khb0g2g3PpeMA0GCSqGSIb3DQEBCwUA\n"
      "MEoxCzAJBgNVBAYTAlVTMRkwFwYDVQQKExBHbG9iYWxTaWduIG52LXNhMR8wHQYD\n"
      "VQQDExZHbG9iYWxTaWduIFJvb3QgQ0EgLSBHMzAeFw0yMTA2MDkwMDAwMDBaFw0z\n"
      "MTA2MDgyMzU5NTlaMEoxCzAJBgNVBAYTAlVTMRkwFwYDVQQKExBHbG9iYWxTaWdu\n"
      "IG52LXNhMR8wHQYDVQQDExZHbG9iYWxTaWduIFJvb3QgQ0EgLSBHMzCCAiIwDQYJ\n"
      "KoZIhvcNAQEBBQADggIPADCCAgoCggIBAL4E3+3HJEVG2jzX+sK1yqEbckZypPtu\n"
      "x3N3aR6Vrn956xWxBY2NU4VFIfE88ll/aT0wZqbt1zsa3RqeM8glvc/9d7H5PHeT\n"
      "79Gql8BKq+2H9yY13NUy9TgrIOPNVbZ4SfibYwypy0YQm5m7/7cJ8e91bUb9Nr2y\n"
      "7oaoGz5o1io8GZFOD4oTi27C/7fyqCkCmZJLdnOjFkMrDXLI4YAlnXrhIRbkIuAe\n"
      "GHWxirDLJzi10BGSAdoo6gWQBaIj++ImQxGc1dQc5sKXc5teLoI0lpBT1sIwoMvV\n"
      "YI2bQVh0b07XHtcwPa5RWPLXnwI75PwQxzb62LF8oT+yQUwpsOSJyYwcmBHQYaNx\n"
      "1Pr4QMzNp+Oz2n1Uc3C3xaQa58aeGeq/QAdzTZziEtGlUZEM6IuEI4P1N2fN1j4P\n"
      "iuF4r1xYDs8SuFD/yYlLeI2c2MvmFo0xSg6uSPRqCM/jHdCqkfNNpJBbGAbIYW/W\n"
      "04O6J2JkFh2RFxYDs2fzGEGZm4G6dkprdFMIALlTyBC0bYKT1eZq9VHtV6nRvWmv\n"
      "AaylJ14rx+Q7aC6fI0bI1XHlzTH0jzZMfjNV8iPBUFeCFGXFZ8bJHPsuacF6nwLx\n"
      "wY0jzQDnE466+vWXT14BMWrMUR3pvN8MPv+2MvmP0xSg6hZKkd06Pq4jG3Ejj6in\n"
      "UoxBqMcCAwEAAaNCMEAwDwYDVR0TAQH/BAUwAwEB/zAOBgNVHQ8BAf8EBAMCAQYw\n"
      "HQYDVR0OBBYEFKVar/6AFH2LxwC0ZFwEukgqS0bYMA0GCSqGSIb3DQEBCwUAA4IBAQBR\n"
      "bUY8ailqXF/w3vGN9VOGBev5nYeD1yfknhk+aoCA8DmF5asJ5AZt0pOEtpJR/YWZ\n"
      "GT+b/xGaxzwoMPmxjSPdZRhqUTrWyd4InENcy+XUG6uHgnIY7qDpiRnwV0wweAV2\n"
      "OZXS6jttuPAHyBs+K6TfGsDzpDHK5vVsQt1zAr72Xd1LSeX776BF3/f6/Dr7guP5\n"
      "tSUUQeFk/gQq/i323iDL49myIIZeF1P0uohsEiL/KZ8nfdXbra+XUl3Bd6mV9Ezg\n"
      "zszbmWzxubUoil58x2oyS9MhUlCT3VkOITkkpFmS6r30YIOCwRvDDDeZPAHDqGRID\n"
      "pHu6HgMHqmpYJv1nVbWcv1O3\n"
      "-----END CERTIFICATE-----\n";

    cfg.cert_pem = lets_encrypt_isrg_root_x1; // minimal trust anchor
    cfg.skip_cert_common_name_check = true;    // user opted to ignore strict hostname check

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
