#include "sensors.h"
#include <driver/gpio.h>
#include <esp_timer.h>

// ---------------------------------------------------------------- ultrasonic
static volatile uint32_t g_rise = 0, g_fall = 0;
static volatile bool g_high = false, g_done = false;

static void IRAM_ATTR onEcho() {
  uint32_t t = (uint32_t)esp_timer_get_time();
  if (gpio_get_level((gpio_num_t)PIN_US_ECHO)) {
    g_rise = t;
    g_high = true;
  } else if (g_high) {
    g_fall = t;
    g_high = false;
    g_done = true;
  }
}

void Ultrasonic::begin() {
  pinMode(PIN_US_TRIG, OUTPUT);
  digitalWrite(PIN_US_TRIG, LOW);
  pinMode(PIN_US_ECHO, INPUT);
  for (uint8_t i = 0; i < US_WINDOW; i++) samples_[i] = US_NO_ECHO_CM;
  attachInterrupt(digitalPinToInterrupt(PIN_US_ECHO), onEcho, CHANGE);
}

void Ultrasonic::push(float cm) {
  samples_[head_] = cm;
  head_ = (head_ + 1) % US_WINDOW;
  float s[US_WINDOW];
  memcpy(s, samples_, sizeof(s));
  for (uint8_t i = 1; i < US_WINDOW; i++) {  // insertion sort, tiny N
    float v = s[i];
    int8_t j = i - 1;
    while (j >= 0 && s[j] > v) { s[j + 1] = s[j]; j--; }
    s[j + 1] = v;
  }
  median_ = s[US_WINDOW / 2];
  if (!present_ && median_ < US_ENTER_CM) present_ = true;
  else if (present_ && median_ > US_EXIT_CM) present_ = false;
}

void Ultrasonic::update(uint32_t now) {
  if (!waiting_) {
    if (now - lastPing_ >= US_PING_INTERVAL_MS) {
      g_done = false;
      g_high = false;
      digitalWrite(PIN_US_TRIG, HIGH);
      delayMicroseconds(10);
      digitalWrite(PIN_US_TRIG, LOW);
      lastPing_ = now;
      waiting_ = true;
    }
    return;
  }
  if (g_done) {
    float cm = (g_fall - g_rise) / 58.0f;
    waiting_ = false;
    push((cm < 2.0f || cm > 400.0f) ? US_NO_ECHO_CM : cm);
  } else if (now - lastPing_ >= US_TIMEOUT_MS) {
    waiting_ = false;
    push(US_NO_ECHO_CM);
  }
}

// ------------------------------------------------------------------------ IR
void IrSensor::begin() { pinMode(PIN_IR, INPUT); }  // module has its own pull-up

void IrSensor::update(uint32_t now) {
#if IR_ACTIVE_LOW
  bool raw = digitalRead(PIN_IR) == LOW;
#else
  bool raw = digitalRead(PIN_IR) == HIGH;
#endif
  if (d_.update(raw, now, IR_DEBOUNCE_MS) && d_.stable) rose_ = true;
}

// ----------------------------------------------------------------------- pad
void HoldPad::begin() { pinMode(PIN_FINALE_PAD, INPUT_PULLUP); }

void HoldPad::update(uint32_t now) {
  bool raw = digitalRead(PIN_FINALE_PAD) == LOW;
  if (d_.update(raw, now, 40)) {
    if (d_.stable) pressedAt_ = now ? now : 1;
    else { pressedAt_ = 0; fired_ = false; }
  }
}

bool HoldPad::heldEvent(uint32_t now) {
  if (d_.stable && !fired_ && pressedAt_ && now - pressedAt_ >= ARM_HOLD_MS) {
    fired_ = true;
    return true;
  }
  return false;
}

// ------------------------------------------------------------------- battery
void Battery::begin() {
  analogSetPinAttenuation(PIN_BAT_SENSE, ADC_11db);
}

void Battery::update(uint32_t now) {
  if (init_ && now - last_ < 1000) return;
  last_ = now;
  uint32_t sum = 0;
  for (uint8_t i = 0; i < 16; i++) sum += analogReadMilliVolts(PIN_BAT_SENSE);
  float mv = (sum / 16.0f) * BAT_DIVIDER * BAT_CAL;
  if (!init_) { avg_ = mv; init_ = true; }
  else avg_ += (mv - avg_) * 0.1f;  // ~10 s smoothing; sags under audio bursts get averaged out
  if (avg_ < BAT_LOW_MV) { if (lowCount_ < 255) lowCount_++; }
  else lowCount_ = 0;
}
