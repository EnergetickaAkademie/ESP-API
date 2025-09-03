#include "AsyncRequest.hpp"

// Static variable definitions for AsyncRequest
std::queue<AsyncRequest::RequestItem*> AsyncRequest::requestQueue;
SemaphoreHandle_t AsyncRequest::queueMutex = nullptr;
bool AsyncRequest::isProcessing = false;
TaskHandle_t AsyncRequest::queueProcessorTask = nullptr;
