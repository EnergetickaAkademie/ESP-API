#include "AsyncRequest.hpp"

// Static storage definitions for new AsyncRequest implementation
QueueHandle_t AsyncRequest::queue_ = NULL;
bool AsyncRequest::started_ = false;
uint8_t AsyncRequest::maxWorkers_ = 1;
bool AsyncRequest::insecureTLS_ = true;
volatile uint32_t AsyncRequest::activeWorkers_ = 0;
