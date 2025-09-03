#pragma once
#include <string>
#include <vector>
#include <utility>
#include <functional>
#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <WiFiClient.h>

extern "C" {
  #include "freertos/FreeRTOS.h"
  #include "freertos/task.h"
  #include "freertos/queue.h"
  #include "esp_log.h"
}

class AsyncRequest {
public:
  enum class Method { GET, POST };
  using DoneCB = std::function<void(esp_err_t,int,std::string)>;

  // Configuration for worker threads
  static bool begin(size_t workerCount = 1, size_t queueLength = 8, 
                   uint32_t stackSize = 8192, UBaseType_t priority = 1, 
                   BaseType_t core = 1);
  
  // Main fetch method - maintains existing interface
  static void fetch(Method method,
                    const std::string& url,
                    std::string payload,
                    const std::vector<std::pair<std::string, std::string>>& headers,
                    DoneCB cb);

  // Get number of pending requests
  static size_t getPendingCount();
  
  // Clear all pending requests
  static bool clearQueue();

private:
  struct HttpJob {
    Method method;
    std::string url;
    std::string payload;
    std::vector<std::pair<std::string, std::string>> headers;
    DoneCB callback;
    uint32_t timeout_ms;
    size_t max_body_bytes;
  };

  static QueueHandle_t s_httpQueue;
  static TaskHandle_t* s_workers;
  static size_t s_workerCount;
  static bool s_initialized;

  // Worker thread function
  static void httpWorker(void* arg);
  
  // Helper to read response body with size limit
  static std::string readBodyCapped(HTTPClient& http, size_t cap, uint32_t timeout_ms);
  
  // Helper to append binary data safely to string
  static void appendBytes(std::string& s, const uint8_t* buf, size_t n);
  
  // Auto-initialization helper
  static void ensureInitialized();
};