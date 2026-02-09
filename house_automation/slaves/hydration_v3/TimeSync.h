#ifndef HYDRATION_V3_TIMESYNC_H
#define HYDRATION_V3_TIMESYNC_H

#include "Config.h"
#include "Comms.h"

class TimeSync {
  unsigned long rtcOffset_ = 0;
  unsigned long syncStart_ = 0;
  unsigned long lastRequest_ = 0;
  bool timeSynced_ = false;
  bool timedOut_ = false;

  // Local time = Pi epoch + offset. Adjust for your timezone if needed (e.g. IST +19800).
  static const unsigned long TIMEZONE_OFFSET_SEC = 19800;

public:
  void begin() {
    syncStart_ = millis();
    lastRequest_ = 0;
    timedOut_ = false;
  }

  void setTimeFromPi(uint32_t timestamp) {
    rtcOffset_ = timestamp - (millis() / 1000);
    timeSynced_ = true;
#if HYDRATION_LOG
    Serial.print("[");
    Serial.print(millis());
    Serial.print("] TIME SYNC: received ");
    Serial.println(timestamp);
#endif
  }

  bool isSynced() const { return timeSynced_; }
  bool isTimedOut() const { return timedOut_; }

  int getHour() const {
    if (!timeSynced_) return 12;
    unsigned long epochSec = rtcOffset_ + (millis() / 1000) + TIMEZONE_OFFSET_SEC;
    return (int)((epochSec % 86400UL) / 3600);
  }

  unsigned long getDay() const {
    if (!timeSynced_) return 0;
    return (rtcOffset_ + (millis() / 1000) + TIMEZONE_OFFSET_SEC) / 86400;
  }

  void tick(Comms &comms) {
    if (timeSynced_ || timedOut_) return;
    unsigned long now = millis();
    if (now - syncStart_ >= TIME_SYNC_TIMEOUT_MS) {
      timedOut_ = true;
#if HYDRATION_LOG
      Serial.println("[TimeSync] Timeout - continuing without time.");
#endif
      return;
    }
    if (now - lastRequest_ >= TIME_SYNC_REQUEST_MS) {
      lastRequest_ = now;
#if HYDRATION_LOG
      Serial.print("[");
      Serial.print(now);
      Serial.println("] Requesting time from Pi...");
#endif
      comms.send(CMD_REQUEST_TIME, 0);
    }
  }
};

#endif
