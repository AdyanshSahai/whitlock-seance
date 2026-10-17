#pragma once
#include <Arduino.h>
#include "config.h"

struct Debounced {
  bool stable = false, cand = false;
  uint32_t candSince = 0;
  // returns true when the stable value changed
  bool update(bool raw, uint32_t now, uint32_t ms) {
    if (raw == stable) { cand = stable; return false; }
    if (raw != cand) { cand = raw; candSince = now; return false; }
    if (now - candSince >= ms) { stable = raw; return true; }
    return false;
  }
};

// HC-SR04: non-blocking (echo timed in an edge ISR), median of the last few pings,
// hysteresis between US_ENTER_CM and US_EXIT_CM.
class Ultrasonic {
 public:
  void begin();
  void update(uint32_t now);
  bool present() const { return present_; }
  float distanceCm() const { return median_; }
  // diagnostics: echoes seen, timeouts, last echo pulse width in microseconds
  uint32_t echoes() const { return echoes_; }
  uint32_t timeouts() const { return timeouts_; }
  uint32_t lastPulseUs() const { return lastPulse_; }

 private:
  void push(float cm);
  float samples_[US_WINDOW];
  uint8_t head_ = 0;
  float median_ = US_NO_ECHO_CM;
  bool present_ = false;
  bool waiting_ = false;
  uint32_t lastPing_ = 0;
  uint32_t echoes_ = 0, timeouts_ = 0, lastPulse_ = 0;
};

// FC-51: debounced, edge event on "something placed".
class IrSensor {
 public:
  void begin();
  void update(uint32_t now);
  bool present() const { return d_.stable; }
  bool rose() { bool r = rose_; rose_ = false; return r; }  // consumed

 private:
  Debounced d_;
  bool rose_ = false;
};

class Battery {
 public:
  void begin();
  void update(uint32_t now);  // samples once a second
  uint16_t mv() const { return (uint16_t)avg_; }
  bool low() const { return lowCount_ >= BAT_LOW_SECONDS; }

 private:
  float avg_ = 0;
  bool init_ = false;
  uint8_t lowCount_ = 0;
  uint32_t last_ = 0;
};
