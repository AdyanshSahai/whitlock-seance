#include "eyes.h"
#include <FastLED.h>

static CRGB g_leds[LED_COUNT];

static inline float r01() { return random(10000) / 10000.0f; }

void Eyes::begin() {
  FastLED.addLeds<WS2812, PIN_LED_DATA, GRB>(g_leds, LED_COUNT);
  FastLED.setBrightness(255);                 // no global dimming: we scale the values ourselves
  FastLED.setCorrection(UncorrectedColor);
  FastLED.setDither(0);                       // our own temporal dithering below
  off();
}

void Eyes::off() {
  fill_solid(g_leds, LED_COUNT, CRGB::Black);
  FastLED.show();
  fade_[0] = fade_[1] = 0;
  for (auto& a : acc_) a[0] = a[1] = a[2] = 0;
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

// One eye's candle: quick uneven wobble plus an occasional deeper dip, each eye on its own dice.
float Eyes::candle(uint8_t e, uint32_t now, uint32_t dt) {
  Candle& c = cd_[e];
  if (now >= c.nextChange) {
    c.target = CANDLE_LO + (CANDLE_HI - CANDLE_LO) * (0.15f + 0.85f * r01());
    c.nextChange = now + 30 + random(110);               // new wobble every 30-140 ms
  }
  float k = dt / 38.0f;                                  // chase the target with ~40 ms lag
  c.cur += (c.target - c.cur) * (k > 1 ? 1 : k);
  if (now >= c.nextDip) {                                // a draught: short, fast-recovering dip
    c.dipStart = now;
    c.dipDur = 180 + random(280);
    c.dipDepth = (0.14f + 0.22f * r01()) * (CANDLE_HI - CANDLE_FLOOR) * 2.0f;
    c.nextDip = now + 2500 + random(5500);
  }
  float d = 0;
  if (now - c.dipStart < c.dipDur) {
    float u = (float)(now - c.dipStart) / c.dipDur;
    d = c.dipDepth * (u < 0.25f ? u / 0.25f : (1.0f - u) / 0.75f);
  }
  float lvl = c.cur - d;
  if (lvl < CANDLE_FLOOR) lvl = CANDLE_FLOOR;
  return lvl;
}

void Eyes::paint(uint8_t eye, float level) {
  level = constrain(level, 0.0f, 1.0f);
  float p = powf(level, EYE_GAMMA) * EYE_MAX_PWM;       // red channel, 0..255 (float)
  float col[3] = {p, p * (EYE_G / 255.0f), p * (EYE_B / 255.0f)};
  if (p > 0.01f) {                                      // keep the candle amber, not blood red
    float floor = EYE_MIN_GREEN_PWM * (p * 0.25f > 1.0f ? 1.0f : p * 0.25f);
    if (col[1] < floor) col[1] = floor;
  }
  uint8_t first = eye ? EYE_RIGHT_FIRST : EYE_LEFT_FIRST;
  for (uint8_t i = 0; i < EYE_LEDS_PER_EYE; i++) {
    float* a = acc_[first + i];
    uint8_t out[3];
    for (uint8_t ch = 0; ch < 3; ch++) {                // error diffusion: average equals the float value
      a[ch] += col[ch];
      int o = (int)a[ch];
      if (o > 255) o = 255;
      a[ch] -= o;
      out[ch] = (uint8_t)o;
    }
    g_leds[first + i] = CRGB(out[0], out[1], out[2]);
  }
}

void Eyes::update(uint32_t now, bool blocked, uint32_t fallMs) {
  uint32_t dt = now - last_;
  last_ = now;
  if (dt > 100) dt = 100;

  if (agitated_) agit_ = min(1.0f, agit_ + dt / (float)AGITATE_UP_MS);
  else agit_ = max(0.0f, agit_ - dt / (float)AGITATE_DOWN_MS);
  const float fallRate = 1.0f / (float)fallMs;
  const float riseRate = 1.0f / (float)EYE_RISE_MS;
  for (uint8_t e = 0; e < 2; e++) {
    float want = blocked ? 0.0f : 1.0f;
    if (fade_[e] < want) fade_[e] = min(want, fade_[e] + riseRate * dt);
    else if (fade_[e] > want) fade_[e] = max(want, fade_[e] - fallRate * dt);
    step(ov_[e], dt, now);
    float c = candle(e, now, dt);
    float base = c * fade_[e];
    if (agit_ > 0) {                      // agitated: bright, with a livelier flicker on top
      float norm = (c - CANDLE_FLOOR) / (CANDLE_HI - CANDLE_FLOOR);
      float hot = AGITATE_LEVEL + 0.12f * (norm - 0.5f);
      base += (hot - base) * agit_;
    }
    paint(e, max(base, ov_[e].level));
  }
  FastLED.show();
}
