#include "AsyncRequest.hpp"

// Static variable definitions for AsyncRequest
QueueHandle_t AsyncRequest::s_httpQueue = nullptr;
TaskHandle_t* AsyncRequest::s_workers = nullptr;
size_t AsyncRequest::s_workerCount = 0;
bool AsyncRequest::s_initialized = false;

// Auto-initialization with default settings if not explicitly called
void AsyncRequest::ensureInitialized() {
    if (!s_initialized) {
        begin(1); // Default: 1 worker
    }
}

bool AsyncRequest::begin(size_t workerCount, size_t queueLength, 
                        uint32_t stackSize, UBaseType_t priority, BaseType_t core) {
    if (s_initialized) {
        return true; // Already initialized
    }
    
    if (workerCount < 1) workerCount = 1;
    
    // Create the HTTP request queue
    s_httpQueue = xQueueCreate(queueLength, sizeof(HttpJob*));
    if (!s_httpQueue) {
        return false;
    }
    
    // Create worker tasks
    s_workers = new TaskHandle_t[workerCount];
    s_workerCount = workerCount;
    
    for (size_t i = 0; i < workerCount; i++) {
        char taskName[16];
        snprintf(taskName, sizeof(taskName), "HttpWorker%zu", i);
        
        if (xTaskCreatePinnedToCore(httpWorker, taskName, stackSize, nullptr, 
                                   priority, &s_workers[i], core) != pdPASS) {
            // Cleanup on failure
            for (size_t j = 0; j < i; j++) {
                vTaskDelete(s_workers[j]);
            }
            delete[] s_workers;
            s_workers = nullptr;
            vQueueDelete(s_httpQueue);
            s_httpQueue = nullptr;
            return false;
        }
    }
    
    s_initialized = true;
    return true;
}

void AsyncRequest::fetch(Method method, const std::string& url, std::string payload,
                        const std::vector<std::pair<std::string, std::string>>& headers,
                        DoneCB cb) {
    ensureInitialized();
    
    if (!s_httpQueue) {
        if (cb) cb(ESP_FAIL, -1, "AsyncRequest not initialized");
        return;
    }
    
    auto* job = new HttpJob();
    job->method = method;
    job->url = url;
    job->payload = std::move(payload);
    job->headers = headers;
    job->callback = std::move(cb);
    job->timeout_ms = 7000; // Default timeout
    job->max_body_bytes = 16384; // Default max body size
    
    if (xQueueSend(s_httpQueue, &job, 0) != pdTRUE) {
        delete job;
        if (cb) cb(ESP_FAIL, -1, "Queue full");
    }
}

size_t AsyncRequest::getPendingCount() {
    return s_httpQueue ? uxQueueMessagesWaiting(s_httpQueue) : 0;
}

bool AsyncRequest::clearQueue() {
    if (!s_httpQueue) return false;
    
    // Drain and delete all pending jobs
    HttpJob* job = nullptr;
    while (xQueueReceive(s_httpQueue, &job, 0) == pdTRUE) {
        delete job;
    }
    return true;
}

void AsyncRequest::httpWorker(void* arg) {
    (void)arg;
    
    for (;;) {
        HttpJob* job = nullptr;
        if (xQueueReceive(s_httpQueue, &job, portMAX_DELAY) != pdTRUE || !job) {
            continue;
        }
        
        HTTPClient http;
        int statusCode = -1;
        std::string responseBody;
        esp_err_t error = ESP_OK;
        
        // Determine if HTTPS and handle accordingly
        bool isHttps = (job->url.rfind("https://", 0) == 0);
        
        if (isHttps) {
            WiFiClientSecure secureClient;
            secureClient.setInsecure(); // Disable certificate verification (as requested)
            
            if (http.begin(secureClient, job->url.c_str())) {
                http.setTimeout(job->timeout_ms);
                
                // Set method
                if (job->method == Method::POST) {
                    http.addHeader("Content-Type", "application/json"); // Default for POST
                }
                
                // Add custom headers
                for (const auto& header : job->headers) {
                    http.addHeader(header.first.c_str(), header.second.c_str());
                }
                
                // Execute request
                if (job->method == Method::POST) {
                    statusCode = http.POST(job->payload.c_str());
                } else {
                    statusCode = http.GET();
                }
                
                if (statusCode > 0) {
                    responseBody = (job->max_body_bytes > 0) 
                        ? readBodyCapped(http, job->max_body_bytes, job->timeout_ms)
                        : http.getString().c_str();
                } else {
                    error = ESP_ERR_HTTP_CONNECT;
                }
                
                http.end();
            } else {
                error = ESP_ERR_HTTP_CONNECT;
                statusCode = -2; // begin() failed
            }
        } else {
            // HTTP (non-secure)
            WiFiClient client;
            
            if (http.begin(client, job->url.c_str())) {
                http.setTimeout(job->timeout_ms);
                
                // Set method  
                if (job->method == Method::POST) {
                    http.addHeader("Content-Type", "application/json"); // Default for POST
                }
                
                // Add custom headers
                for (const auto& header : job->headers) {
                    http.addHeader(header.first.c_str(), header.second.c_str());
                }
                
                // Execute request
                if (job->method == Method::POST) {
                    statusCode = http.POST(job->payload.c_str());
                } else {
                    statusCode = http.GET();
                }
                
                if (statusCode > 0) {
                    responseBody = (job->max_body_bytes > 0)
                        ? readBodyCapped(http, job->max_body_bytes, job->timeout_ms) 
                        : http.getString().c_str();
                } else {
                    error = ESP_ERR_HTTP_CONNECT;
                }
                
                http.end();
            } else {
                error = ESP_ERR_HTTP_CONNECT;
                statusCode = -2; // begin() failed
            }
        }
        
        // Call the callback with results
        if (job->callback) {
            job->callback(error, statusCode, responseBody);
        }
        
        delete job;
    }
}

std::string AsyncRequest::readBodyCapped(HTTPClient& http, size_t cap, uint32_t timeout_ms) {
    WiFiClient* stream = http.getStreamPtr();
    std::string output;
    output.reserve(std::min(cap, (size_t)1024));
    
    uint32_t lastActivity = millis();
    while (http.connected()) {
        size_t available = stream->available();
        if (available == 0) {
            if (millis() - lastActivity > timeout_ms) break;
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }
        
        uint8_t buffer[512];
        size_t bytesToRead = std::min(sizeof(buffer), available);
        size_t bytesRead = stream->readBytes(buffer, bytesToRead);
        
        if (bytesRead == 0) {
            if (millis() - lastActivity > timeout_ms) break;
            continue;
        }
        
        lastActivity = millis();
        
        size_t room = (cap > output.length()) ? (cap - output.length()) : 0;
        if (room == 0) break;
        
        size_t bytesToAppend = std::min(room, bytesRead);
        appendBytes(output, buffer, bytesToAppend);
        
        if (bytesToAppend < bytesRead) break; // Cap reached
    }
    
    return output;
}

void AsyncRequest::appendBytes(std::string& s, const uint8_t* buf, size_t n) {
    s.reserve(s.length() + n);
    for (size_t i = 0; i < n; i++) {
        s += (char)buf[i];
    }
}
