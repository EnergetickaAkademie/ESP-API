# ESP32 Binary Protocol Documentation

This document describes the optimized binary protocol for ESP32 board communication with the WebControl game server.

## Overview

The binary protocol is designed to minimize bandwidth usage and memory consumption on ESP32 devices. It uses fixed-size packets with structured binary data instead of JSON.

### Key Benefits

- **~65% bandwidth savings** compared to JSON
- **Fixed packet sizes** - predictable memory usage
- **Overflow protection** - validates all inputs
- **Unix timestamps** - 64-bit timestamps prevent Y2038 issues
- **String length validation** - prevents buffer overflows

## Protocol Version

Current version: `0x01`

## Packet Formats

### 1. Board Registration Request

**Endpoint:** `POST /coreapi/register_binary`  
**Size:** 53 bytes (fixed)  
**Content-Type:** `application/octet-stream`

```
Offset | Size | Type   | Description
-------|------|--------|---------------------------
0      | 1    | uint8  | Protocol version (0x01)
1      | 4    | uint32 | Board ID (big-endian)
5      | 32   | char[] | Board name (null-terminated)
37     | 16   | char[] | Board type (null-terminated)
```

**Board Types:** `"solar"`, `"wind"`, `"battery"`, `"generic"`

### 2. Registration Response

**Size:** 3-67 bytes (variable)

```
Offset | Size | Type   | Description
-------|------|--------|---------------------------
0      | 1    | uint8  | Protocol version (0x01)
1      | 1    | uint8  | Success flag (0x00=fail, 0x01=success)
2      | 1    | uint8  | Message length (N)
3      | N    | char[] | Status message (max 64 bytes)
```

### 3. Power Data Submission

**Endpoint:** `POST /coreapi/power_data_binary`  
**Size:** 22 bytes (fixed)  
**Content-Type:** `application/octet-stream`

```
Offset | Size | Type   | Description
-------|------|--------|---------------------------
0      | 1    | uint8  | Protocol version (0x01)
1      | 4    | uint32 | Board ID (big-endian)
5      | 8    | uint64 | Unix timestamp (big-endian)
13     | 4    | int32  | Generation (watts * 100, big-endian)
17     | 4    | int32  | Consumption (watts * 100, big-endian)
21     | 1    | uint8  | Data flags (see below)
```

**Data Flags:**
- Bit 0: Generation data present (0x01)
- Bit 1: Consumption data present (0x02)
- Bits 2-7: Reserved (must be 0)

**Special Values:**
- `0x7FFFFFFF` = null/not present for power values
- Power values are stored as `watts * 100` for 2 decimal precision

### 4. Board Status Poll

**Endpoint:** `GET /coreapi/poll_binary/{board_id}`  
**Size:** 24 bytes (fixed)  
**Content-Type:** `application/octet-stream`

```
Offset | Size | Type   | Description
-------|------|--------|---------------------------
0      | 1    | uint8  | Protocol version (0x01)
1      | 8    | uint64 | Unix timestamp (big-endian)
9      | 2    | uint16 | Round number (big-endian)
11     | 4    | uint32 | Score (big-endian)
15     | 4    | int32  | Generation (watts * 100, big-endian)
19     | 4    | int32  | Consumption (watts * 100, big-endian)
23     | 1    | uint8  | Status flags (see below)
```

**Status Flags:**
- Bit 0: Round type (0=night, 1=day)
- Bit 1: Game active (0=inactive, 1=active)
- Bit 2: Expecting data (0=no, 1=yes)
- Bits 3-7: Reserved

## Error Responses

Binary endpoints return simple ASCII error codes:

- `BOARD_NOT_FOUND` - Board ID not registered
- `PROTOCOL_ERROR` - Invalid binary format
- `TIME_ERROR` - Timestamp out of range
- `INTERNAL_ERROR` - Server error

## ESP32 Implementation Example

```cpp
#include <WiFi.h>
#include <HTTPClient.h>

struct RegistrationRequest {
    uint8_t version = 0x01;
    uint32_t board_id;
    char board_name[32];
    char board_type[16];
} __attribute__((packed));

struct PowerData {
    uint8_t version = 0x01;
    uint32_t board_id;
    uint64_t timestamp;
    int32_t generation;  // watts * 100
    int32_t consumption; // watts * 100
    uint8_t flags;
} __attribute__((packed));

struct PollResponse {
    uint8_t version;
    uint64_t timestamp;
    uint16_t round;
    uint32_t score;
    int32_t generation;
    int32_t consumption;
    uint8_t flags;
} __attribute__((packed));

// Convert to big-endian for network transmission
uint32_t htonl(uint32_t hostlong);
uint64_t htonll(uint64_t hostlonglong);

bool registerBoard(uint32_t board_id, const char* name, const char* type) {
    RegistrationRequest req;
    req.board_id = htonl(board_id);
    strncpy(req.board_name, name, 31);
    strncpy(req.board_type, type, 15);
    
    HTTPClient http;
    http.begin("http://server/coreapi/register_binary");
    http.addHeader("Authorization", "Bearer " + token);
    http.addHeader("Content-Type", "application/octet-stream");
    
    int httpCode = http.POST((uint8_t*)&req, sizeof(req));
    // Handle response...
}
```

## Memory Usage

- **Registration:** 53 bytes packet
- **Power data:** 22 bytes packet  
- **Poll response:** 24 bytes packet
- **Total structs:** ~100 bytes RAM
- **HTTP overhead:** ~200-300 bytes

**Total ESP32 memory usage:** < 400 bytes per operation

## Security

- **Authentication:** Bearer token in HTTP header
- **Validation:** All inputs validated for size/range
- **Overflow protection:** String lengths enforced
- **Timestamp validation:** Prevents time-based attacks

## Bandwidth Comparison

| Operation | JSON Size | Binary Size | Savings |
|-----------|-----------|-------------|---------|
| Registration | 76 bytes | 53 bytes | 30% |
| Power Data | 59 bytes | 22 bytes | 63% |
| Poll Response | ~150 bytes | 24 bytes | 84% |
| **Overall** | **~285 bytes** | **~99 bytes** | **65%** |

## Error Handling

Always check:
1. HTTP status code
2. Protocol version in response
3. Data validation flags
4. Timestamp reasonableness

