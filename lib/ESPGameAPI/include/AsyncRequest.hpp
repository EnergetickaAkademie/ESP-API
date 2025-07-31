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
                    std::string                                       payload,   // NOTE: by value → we can move-store
                    const std::vector<std::pair<std::string,
                                                 std::string>>&       headers,
                    DoneCB                                            cb)
  {
    auto* ctx = new Ctx;
    ctx->cb       = std::move(cb);
    ctx->payload  = std::move(payload);      // ⭐ keep a private copy
    auto isHttp = url.rfind("http://", 0) == 0;
    esp_http_client_config_t cfg = {};
    cfg.url               = url.c_str();
    cfg.event_handler      = _event;
    cfg.user_data          = ctx;
    cfg.is_async           = !isHttp;
    cfg.timeout_ms         = 7000;
    cfg.crt_bundle_attach  = arduino_esp_crt_bundle_attach;

    ctx->client = esp_http_client_init(&cfg);
    if (!ctx->client) { ctx->finish(ESP_FAIL, -1); return; }

    if (method == Method::POST) {
      esp_http_client_set_method(ctx->client, HTTP_METHOD_POST);
      if (!ctx->payload.empty())
        esp_http_client_set_post_field(ctx->client,
              ctx->payload.c_str(), ctx->payload.size());  // pointer now stays valid
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
    std::string              payload;   // ⭐ lives as long as Ctx
    std::string              body;
    DoneCB                   cb;
    void finish(esp_err_t err,int status){ if(cb) cb(err,status,body); }
  };

  static void _worker(void* arg){
    auto* ctx=static_cast<Ctx*>(arg);
    while(true){
      esp_err_t r=esp_http_client_perform(ctx->client);
      if(r==ESP_OK){ ctx->finish(ESP_OK,
          esp_http_client_get_status_code(ctx->client)); break;}
      if(r!=ESP_ERR_HTTP_EAGAIN){ ctx->finish(r,-1); break;}
      vTaskDelay(pdMS_TO_TICKS(10));
    }
    esp_http_client_cleanup(ctx->client);
    delete ctx;
    vTaskDelete(nullptr);
  }

  static esp_err_t _event(esp_http_client_event_t* e){
    auto* ctx=static_cast<Ctx*>(e->user_data);
    if(e->event_id==HTTP_EVENT_ON_DATA &&
       !esp_http_client_is_chunked_response(e->client))
      ctx->body.append(static_cast<char*>(e->data), e->data_len);
    return ESP_OK;
  }
};
