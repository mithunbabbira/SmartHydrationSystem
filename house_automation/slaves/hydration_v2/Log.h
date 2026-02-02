#ifndef HYDRATION_V2_LOG_H
#define HYDRATION_V2_LOG_H

#include "Config.h"
#include <Arduino.h>

#if HYDRATION_LOG
  #define LOG_MS() (millis())
  #define LOG_INFO(msg) do { Serial.print("["); Serial.print(LOG_MS()); Serial.print("] "); Serial.println(msg); } while(0)
  #define LOG_INFO2(msg, val) do { Serial.print("["); Serial.print(LOG_MS()); Serial.print("] "); Serial.print(msg); Serial.println(val); } while(0)
  #define LOG_WARN(msg) do { Serial.print("["); Serial.print(LOG_MS()); Serial.print("] WARN "); Serial.println(msg); } while(0)
  #define LOG_WARN2(msg, val) do { Serial.print("["); Serial.print(LOG_MS()); Serial.print("] WARN "); Serial.print(msg); Serial.println(val); } while(0)
#else
  #define LOG_INFO(msg)
  #define LOG_INFO2(msg, val)
  #define LOG_WARN(msg)
  #define LOG_WARN2(msg, val)
#endif

#endif
