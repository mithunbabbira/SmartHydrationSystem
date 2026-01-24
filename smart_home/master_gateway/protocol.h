#ifndef SMART_HOME_CONFIG_H
#define SMART_HOME_CONFIG_H

#include <stdint.h>

// Protocol Version
#define PROTOCOL_VERSION 1

// Slave IDs
#define SLAVE_ID_HYDRATION 1
#define SLAVE_ID_LED 2
#define SLAVE_ID_IR 3

// Message Types
#define MSG_TYPE_TELEMETRY 1
#define MSG_TYPE_COMMAND 2
#define MSG_TYPE_ACK 3

// Structs for ESP-NOW
typedef struct __attribute__((packed)) {
  uint8_t slave_id;
  uint8_t msg_type;
  uint8_t version;
} ESPNowHeader;

// ID 1: Hydration Telemetry
typedef struct __attribute__((packed)) {
  ESPNowHeader header;
  float weight;
  float delta;
  uint8_t alert_level;
  bool bottle_missing;
} HydrationTelemetry;

// ID 2: LED Control / Status
typedef struct __attribute__((packed)) {
  ESPNowHeader header;
  bool is_on;
  uint8_t r, g, b;
  uint8_t mode;
  uint8_t speed;
} LEDData;

// ID 3: IR Command (Binary format from Master to Slave)
typedef struct __attribute__((packed)) {
  ESPNowHeader header;
  uint32_t ir_code;
  uint8_t bits;
} IRData;

// General Command Packet (used for simple commands like tare, snooze)
typedef struct __attribute__((packed)) {
  ESPNowHeader header;
  uint8_t command_id; // 1=Tare, 2=Snooze, 3=Reset
  uint32_t val;
} GenericCommand;

#endif
