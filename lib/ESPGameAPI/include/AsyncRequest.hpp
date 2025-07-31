#pragma once
#include <string>
#include <vector>
#include <utility>
#include <functional>

extern "C" {
  #include "esp_http_client.h"
  #include "esp_crt_bundle.h"
}

class AsyncRequest {
public:
  enum class Method { GET, POST };
  using DoneCB = std::function<void(esp_err_t,int,std::string)>;

  static void fetch(Method                                            method,
                    const std::string&                                url,
                    std::string                                       payload,      // moved in
                    const std::vector<std::pair<std::string,
                                                 std::string>>&       headers,
                    DoneCB                                            cb)
  {
    auto* ctx = new Ctx;
    ctx->cb       = std::move(cb);
    ctx->payload  = std::move(payload);
    ctx->url      = url;
    ctx->method   = method;

    const bool isHttp = url.rfind("http://", 0) == 0;

    esp_http_client_config_t cfg = {};
    cfg.url               = ctx->url.c_str();
    cfg.event_handler      = _event;
    cfg.user_data          = ctx;
    cfg.is_async           = !isHttp;                 // async only for HTTPS
    cfg.timeout_ms         = 7000;
    cfg.crt_bundle_attach  = arduino_esp_crt_bundle_attach;

    ctx->client = esp_http_client_init(&cfg);
    if (!ctx->client) { ctx->logFail(ESP_FAIL); delete ctx; return; }

    if (method == Method::POST) {
      esp_http_client_set_method(ctx->client, HTTP_METHOD_POST);
      if (!ctx->payload.empty())
        esp_http_client_set_post_field(ctx->client,
                                       ctx->payload.c_str(),
                                       ctx->payload.size());
    }

    for (auto& h : headers)
      esp_http_client_set_header(ctx->client,
                                 h.first.c_str(), h.second.c_str());

    xTaskCreate(_worker, "httpw", 6144, ctx,
                tskIDLE_PRIORITY + 1, nullptr);
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
      bool isBinary = !payload.empty() &&
                      (payload.find_first_not_of("0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ") == std::string::npos);
      Serial.printf("❌ HTTP %s %s\n"
                    "   err  = %s\n",
                    "   len  = " + std::to_string(payload.size()),
                    method==Method::POST?"POST":"GET",
                    url.c_str(),
                    esp_err_to_name(err),);
    }
  };

  static void _worker(void* arg){
    auto* ctx = static_cast<Ctx*>(arg);
    while (true) {
      esp_err_t r = esp_http_client_perform(ctx->client);
      if (r == ESP_OK) {                      // success
        ctx->finish(ESP_OK,
                    esp_http_client_get_status_code(ctx->client));
        break;
      }
      if (r != ESP_ERR_HTTP_EAGAIN) {         // fatal
        ctx->logFail(r);
        ctx->finish(r,-1);
        break;
      }
      vTaskDelay(pdMS_TO_TICKS(250));
    }
    esp_http_client_cleanup(ctx->client);
    delete ctx;
    vTaskDelete(nullptr);
  }

  static esp_err_t _event(esp_http_client_event_t* e){
    auto* ctx = static_cast<Ctx*>(e->user_data);
    if (e->event_id == HTTP_EVENT_ON_DATA &&
        !esp_http_client_is_chunked_response(e->client))
      ctx->body.append(static_cast<char*>(e->data), e->data_len);
    return ESP_OK;
  }
};
