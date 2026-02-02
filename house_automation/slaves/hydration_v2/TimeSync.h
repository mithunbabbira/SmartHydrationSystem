#ifndef HYDRATION_V2_TIMESYNC_H
#define HYDRATION_V2_TIMESYNC_H

#include "Config.h"
#include "Log.h"
#include "Comms.h"

class TimeSync {
  unsigned long rtcOffset_ = 0;
  unsigned long syncStart_ = 0;
  unsigned long lastRequest_ = 0;
  bool timeSynced_ = false;
  bool timedOut_ = false;

public:
  void begin() {
    syncStart_ = millis();
    lastRequest_ = 0;
    timedOut_ = false;
  }

  void setTimeFromPi(uint32_t timestamp) {
    rtcOffset_ = timestamp - (millis() / 1000);
    timeSynced_ = true;
    LOG_INFO2("TIME SYNC Received: ", timestamp);
  }

  bool isSynced() const { return timeSynced_; }
  bool isTimedOut() const { return timedOut_; }

  int getHour() const {
    if (!timeSynced_)
      return 12;
    unsigned long epoch = rtcOffset_ + (millis() / 1000) + 19800;
    return (epoch % 86400L) / 3600;
  }

  unsigned long getDay() const {
    if (!timeSynced_)
      return 0;
    return (rtcOffset_ + (millis() / 1000) + 19800) / 86400;
  }

  void tick(Comms &comms) {
    if (timeSynced_ || timedOut_)
      return;
    unsigned long now = millis();
    if (now - syncStart_ >= TIME_SYNC_TIMEOUT_MS) {
      timedOut_ = true;
      LOG_WARN("Time sync timeout - continuing without time.");
      return;
    }
    if (now - lastRequest_ >= TIME_SYNC_REQUEST_MS) {
      lastRequest_ = now;
      LOG_INFO("Requesting Time...");
      comms.send(CMD_REQUEST_TIME, 0);
    }
  }
};

#endif
