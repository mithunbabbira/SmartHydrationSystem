#ifndef HYDRATION_V2_STATEMACHINE_H
#define HYDRATION_V2_STATEMACHINE_H

#include "Config.h"
#include "Log.h"
#include "Comms.h"
#include "Hardware.h"
#include "TimeSync.h"

enum State {
  STATE_MONITORING,
  STATE_WAIT_FOR_PRESENCE,
  STATE_REMINDER_PRE,
  STATE_REMINDER_ACTIVE,
  STATE_REMOVED_DRINKING,
  STATE_MISSING_ALERT,
  STATE_STABILIZING
};

class StateMachine {
  HydrationHW *hw_ = nullptr;
  Comms *comms_ = nullptr;
  TimeSync *timeSync_ = nullptr;

  State state_ = STATE_MONITORING;
  unsigned long stateStart_ = 0;
  unsigned long lastIntervalReset_ = 0;
  unsigned long lastBlinkTime_ = 0;
  unsigned long lastAwayCheck_ = 0;
  bool isBlinkOn_ = false;
  float lastSavedWeight_ = 0.0f;
  float dailyTotal_ = 0.0f;
  int currentDay_ = 0;
  bool isSleeping_ = false;
  unsigned long drinkConfirmAt_ = 0;

  void enterState(State s) {
    State old = state_;
    state_ = s;
    stateStart_ = millis();
    hw_->stopAll();
    if (state_ == STATE_MONITORING)
      hw_->setRgb(isSleeping_ ? COLOR_SLEEP : COLOR_IDLE);
    if ((old == STATE_REMINDER_PRE || old == STATE_REMINDER_ACTIVE) &&
        state_ != STATE_REMINDER_PRE && state_ != STATE_REMINDER_ACTIVE) {
      LOG_INFO("Logic: Alert Stopped -> Sending Notification");
      comms_->send(CMD_ALERT_STOPPED, 0);
    }
  }

  void handleBlink(unsigned long now, int color) {
    if (now - lastBlinkTime_ > BLINK_INTERVAL_MS) {
      lastBlinkTime_ = now;
      isBlinkOn_ = !isBlinkOn_;
      hw_->setLed(isBlinkOn_);
      if (color > 0)
        hw_->setRgb(isBlinkOn_ ? color : 0);
    }
  }

  void evaluateWeightChange(float currentWeight) {
    if (lastSavedWeight_ <= 0.0f) {
      lastSavedWeight_ = currentWeight;
      lastIntervalReset_ = millis();
      hw_->saveHydrationState(lastSavedWeight_, dailyTotal_, currentDay_);
      LOG_INFO("RESULT: Baseline set (bottle placed). No drink/refill.");
      return;
    }
    float diff = lastSavedWeight_ - currentWeight;
    if (diff >= DRINK_MIN_ML) {
      LOG_INFO2("RESULT: User Drank ", diff);
      LOG_INFO(" ml (Good job!)");
      dailyTotal_ += diff;
      comms_->sendFloat(CMD_DRINK_DETECTED, diff);
      comms_->sendFloat(CMD_DAILY_TOTAL, dailyTotal_);
      hw_->setRgb(COLOR_OK);
      delay(2000);
      hw_->setRgb(0);
      lastSavedWeight_ = currentWeight;
      lastIntervalReset_ = millis();
    } else if (diff <= -REFILL_MIN_ML) {
      LOG_INFO2("RESULT: Bottle Refilled (+", -diff);
      LOG_INFO("ml).");
      hw_->setRgb(COLOR_REFILL);
      delay(2000);
      hw_->setRgb(0);
      lastSavedWeight_ = currentWeight;
      lastIntervalReset_ = millis();
    } else {
      LOG_INFO("RESULT: No significant change (Preserving Baseline).");
    }
    hw_->saveHydrationState(lastSavedWeight_, dailyTotal_, currentDay_);
  }

public:
  void begin(HydrationHW *hw, Comms *comms, TimeSync *timeSync) {
    hw_ = hw;
    comms_ = comms;
    timeSync_ = timeSync;
    hw_->loadHydrationState(&lastSavedWeight_, &dailyTotal_, &currentDay_);
    lastIntervalReset_ = millis();
    if (hw_->getWeight() < THRESHOLD_WEIGHT) {
      lastSavedWeight_ = 0.0f;
      LOG_INFO("Logic: Boot without bottle - baseline cleared.");
    }
    hw_->setRgb(COLOR_IDLE);
    LOG_INFO("Logic: Started. State loaded.");
  }

  void setSleep(bool sleeping) {
    bool changed = (isSleeping_ != sleeping);
    isSleeping_ = sleeping;
    if (!changed) return;
    LOG_INFO2("Logic: Sleep Mode ", isSleeping_ ? "ACTIVATED" : "DEACTIVATED");
    if (state_ == STATE_MONITORING)
      hw_->setRgb(isSleeping_ ? COLOR_SLEEP : COLOR_IDLE);
    if (isSleeping_ && (state_ == STATE_REMINDER_PRE || state_ == STATE_REMINDER_ACTIVE)) {
      LOG_INFO("Logic: Sleep silencing alert.");
      enterState(STATE_MONITORING);
    }
  }

  void checkDay(int newDay) {
    if (currentDay_ == newDay) return;
    LOG_INFO2("Logic: New Day ", newDay);
    currentDay_ = newDay;
    dailyTotal_ = 0.0f;
    hw_->saveHydrationState(lastSavedWeight_, dailyTotal_, currentDay_);
    comms_->sendFloat(CMD_DAILY_TOTAL, dailyTotal_);
  }

  void handlePresence(bool isHome) {
    LOG_INFO2("Logic: Presence ", isHome ? "HOME" : "AWAY");
    if (!isHome) {
      if (state_ == STATE_WAIT_FOR_PRESENCE || state_ == STATE_REMINDER_PRE || state_ == STATE_REMINDER_ACTIVE) {
        LOG_INFO("Logic: User Away. Snoozing.");
        enterState(STATE_MONITORING);
        lastIntervalReset_ = millis();
      }
      return;
    }
    if (state_ == STATE_WAIT_FOR_PRESENCE) {
      LOG_INFO("Logic: User Home. Starting Reminder.");
      enterState(STATE_REMINDER_PRE);
      comms_->send(CMD_ALERT_REMINDER, 0);
    }
  }

  float getDailyTotal() const { return dailyTotal_; }

  void update() {
    float w = hw_->getWeight();
    unsigned long now = millis();

    switch (state_) {
      case STATE_MONITORING: {
        if (w < THRESHOLD_WEIGHT) {
          LOG_INFO("Logic: Bottle Lifted.");
          enterState(STATE_REMOVED_DRINKING);
          return;
        }
        if (now - lastIntervalReset_ <= CHECK_INTERVAL_MS) {
          drinkConfirmAt_ = 0;
          break;
        }
        if (isSleeping_) return;
        float delta = lastSavedWeight_ - w;
        if (drinkConfirmAt_ != 0) {
          if (now < drinkConfirmAt_) return;
          float cw = hw_->getWeight();
          float d2 = lastSavedWeight_ - cw;
          drinkConfirmAt_ = 0;
          if (d2 >= DRINK_MIN_ML) {
            LOG_INFO2("Logic: Proactive Drink Confirmed ", d2);
            dailyTotal_ += d2;
            comms_->sendFloat(CMD_DRINK_DETECTED, d2);
            comms_->sendFloat(CMD_DAILY_TOTAL, dailyTotal_);
            lastIntervalReset_ = now;
          } else {
            LOG_INFO("Logic: Weight noise - no drink. Checking Presence...");
            comms_->send(CMD_REQUEST_PRESENCE, 0);
            enterState(STATE_WAIT_FOR_PRESENCE);
          }
          return;
        }
        if (delta >= DRINK_MIN_ML) {
          LOG_INFO2("Logic: Possible drink - confirming in ", DRINK_CONFIRM_MS);
          drinkConfirmAt_ = now + DRINK_CONFIRM_MS;
          return;
        }
        LOG_INFO("Logic: Interval Expired. Checking Presence...");
        comms_->send(CMD_REQUEST_PRESENCE, 0);
        enterState(STATE_WAIT_FOR_PRESENCE);
        break;
      }

      case STATE_WAIT_FOR_PRESENCE:
        if (w < THRESHOLD_WEIGHT) {
          enterState(STATE_REMOVED_DRINKING);
          return;
        }
        if (now - stateStart_ > PRESENCE_TIMEOUT_MS) {
          LOG_INFO("Logic: Presence Timeout. Snooze.");
          enterState(STATE_MONITORING);
          lastIntervalReset_ = millis();
        }
        break;

      case STATE_REMINDER_PRE:
        if (w < THRESHOLD_WEIGHT) {
          LOG_INFO("Logic: Bottle Lifted! Silenced.");
          enterState(STATE_REMOVED_DRINKING);
          return;
        }
        handleBlink(now, COLOR_ALERT);
        if (now - stateStart_ > LED_ALERT_DURATION) {
          LOG_INFO("Logic: Escalating to Buzzer.");
          enterState(STATE_REMINDER_ACTIVE);
        }
        break;

      case STATE_REMINDER_ACTIVE:
        if (w < THRESHOLD_WEIGHT) {
          LOG_INFO("Logic: Bottle Lifted! Silenced.");
          enterState(STATE_REMOVED_DRINKING);
          return;
        }
        handleBlink(now, COLOR_ALERT);
        hw_->setBuzzer((now - lastBlinkTime_) < (BLINK_INTERVAL_MS / 2));
        if (now - lastAwayCheck_ > AWAY_CHECK_INTERVAL_MS) {
          lastAwayCheck_ = now;
          LOG_INFO("Logic: Checking Presence...");
          comms_->send(CMD_REQUEST_PRESENCE, 0);
        }
        break;

      case STATE_REMOVED_DRINKING:
        hw_->stopAll();
        if (w >= THRESHOLD_WEIGHT) {
          LOG_INFO("Logic: Bottle Returned. Stabilizing...");
          enterState(STATE_STABILIZING);
          return;
        }
        if (now - stateStart_ > MISSING_TIMEOUT_MS) {
          LOG_INFO2("Logic: Bottle Missing > ", MISSING_TIMEOUT_MS / 1000);
          LOG_INFO("s -> MISSING Alert.");
          enterState(STATE_MISSING_ALERT);
          comms_->send(CMD_ALERT_MISSING, 0);
        }
        break;

      case STATE_MISSING_ALERT:
        if (w >= THRESHOLD_WEIGHT) {
          LOG_INFO("Logic: Missing Bottle Found!");
          comms_->send(CMD_ALERT_REPLACED, 0);
          enterState(STATE_STABILIZING);
          return;
        }
        if (now - lastBlinkTime_ > BLINK_INTERVAL_MS) {
          lastBlinkTime_ = now;
          isBlinkOn_ = !isBlinkOn_;
          hw_->setLed(isBlinkOn_);
          hw_->setRgb(isBlinkOn_ ? COLOR_ALERT : 0);
        }
        if (now - stateStart_ > BUZZER_START_DELAY_MS)
          hw_->setBuzzer(isBlinkOn_);
        break;

      case STATE_STABILIZING:
        hw_->stopAll();
        if (now - stateStart_ > STABILIZATION_MS) {
          float fw = hw_->getWeight();
          LOG_INFO2("Logic: Stabilized at ", fw);
          LOG_INFO("g. Evaluating...");
          evaluateWeightChange(fw);
          enterState(STATE_MONITORING);
        }
        break;
    }
  }
};

#endif
