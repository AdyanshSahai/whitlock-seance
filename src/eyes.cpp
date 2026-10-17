#include "eyes.h"
#include <FastLED.h>
#include "config.h"

static CRGB g_leds[LED_COUNT];

void Eyes::begin() {
  FastLED.addLeds<WS2812, PIN_LED_DATA, GRB>(g_leds, LED_COUNT);
  FastLED.setBrightness(EYE_MAX_BRIGHTNESS);
  FastLED.setDither(1);
  setColor(EYE_R, EYE_G, EYE_B);
  off();
}

void Eyes::off() {
  fill_solid(g_leds, LED_COUNT, CRGB::Black);
  FastLED.show();
  base_[0] = base_[1] = 0;
  cancelOverlays();
}

void Eyes::cancelOverlays() {
  ov_[0] = Overlay();
  ov_[1] = Overlay();
}

void Eyes::flash(uint8_t eye, float peak, uint32_t attackMs, uint32_t holdMs, uint32_t decayMs) {
  Overlay& o = ov_[eye];
  o.target = peak;
  if (o.level > peak) o.level = peak;
  o.attackRate = attackMs ? peak / attackMs : 0;
  o.decayRate = decayMs ? peak / decayMs : peak;
  o.holdMs = holdMs;
  o.holdStarted = false;
}

void Eyes::flashBoth(float peak, uint32_t attackMs, uint32_t holdMs, uint32_t decayMs) {
  flash(0, peak, attackMs, holdMs, decayMs);
  // right eye lags a touch so the two never move in lockstep
  flash(1, peak, attackMs, holdMs + 120, decayMs + 250);
}

void Eyes::step(Overlay& o, uint32_t dt, uint32_t now) {
  if (o.level < o.target) {
    o.level = o.attackRate > 0 ? min(o.target, o.level + o.attackRate * dt) : o.target;
  } else if (o.target > 0) {
    if (!o.holdStarted) {
      o.holdStarted = true;
      o.holdUntil = now + o.holdMs;
    }
    if ((int32_t)(now - o.holdUntil) >= 0) o.target = 0;
  } else if (o.level > 0) {
    o.level = max(0.0f, o.level - o.decayRate * dt);
  }
}

void Eyes::paint(uint8_t eye, float level) {
  level = constrain(level, 0.0f, 1.0f);
  uint8_t v = (uint8_t)(powf(level, EYE_GAMMA) * 255.0f + 0.5f);
  CRGB c(r_, g_, b_);
  c.nscale8(v);
  uint8_t first = eye ? EYE_RIGHT_FIRST : EYE_LEFT_FIRST;
  for (uint8_t i = 0; i < EYE_LEDS_PER_EYE; i++) g_leds[first + i] = c;
}

void Eyes::update(uint32_t now, bool blocked, uint32_t fallMs) {
  uint32_t dt = now - last_;
  last_ = now;
  if (dt > 100) dt = 100;

  const float t = now / 1000.0f;
  const float period[2] = {BREATH_PERIOD_L_S, BREATH_PERIOD_R_S};
  const float phase[2] = {0.0f, BREATH_PHASE_R};
  const float fallRate = BREATH_HI / (float)fallMs;
  const float riseRate = BREATH_HI / (float)EYE_RISE_MS;

  for (uint8_t e = 0; e < 2; e++) {
    float want = 0;
    if (!blocked) {
      float s = 0.5f + 0.5f * sinf(TWO_PI * t / period[e] + phase[e]);
      want = BREATH_LO + (BREATH_HI - BREATH_LO) * s;
    }
    if (base_[e] < want) base_[e] = min(want, base_[e] + riseRate * dt);
    else if (base_[e] > want) base_[e] = max(want, base_[e] - fallRate * dt);
    step(ov_[e], dt, now);
    paint(e, max(base_[e], ov_[e].level));
  }
  FastLED.show();
}
