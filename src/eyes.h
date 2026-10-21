#pragma once
#include <Arduino.h>
#include "config.h"
#include "presence.h"

// Two eyes (4 LEDs each). Brightness per eye = max(base, overlay), base = candle * boost * fade:
//  - candle: random uneven flicker with occasional deeper "draught" dips, different for each eye;
//    it settles to a steady level when someone is MID/NEAR ("listening")
//  - boost: up to NOTICE_BRIGHTNESS x idle while someone is FAR/MID/NEAR ("noticed")
//  - fade: 1 normally, slews to 0 when `blocked` (NEAR)
//  - overlay: one-shot attack / hold / decay (flare, snap-bright, swell, ...)
// Output uses float maths with per-channel error-diffusion (temporal dithering).
class Eyes {
 public:
  void begin();
  void update(uint32_t now, bool blocked, uint32_t fallMs);
  // attackMs == 0 snaps. peak is perceived level 0..1.
  void flash(uint8_t eye, float peak, uint32_t attackMs, uint32_t holdMs, uint32_t decayMs, bool flicker = false);
  void flashBoth(float peak, uint32_t attackMs, uint32_t holdMs, uint32_t decayMs, bool flicker = false);
  void setTier(Tier t);  // drives the noticed (brighter) and listening (steady) looks, smoothly
  void cancelOverlays();
  void setAgitated(bool on) { agitated_ = on; }  // someone is near (Friday): eyes flare brighter
  void off();  // immediate black

 private:
  struct Overlay {
    float level = 0, target = 0, attackRate = 0, decayRate = 0;
    uint32_t holdMs = 0, holdUntil = 0;
    bool holdStarted = false;
    bool flicker = false;  // wobble with the candle while it plays (busy-mode glint)
  };
  struct Candle {
    float cur = 0.55f, target = 0.55f;
    uint32_t nextChange = 0, nextDip = 0, dipStart = 0, dipDur = 0;
    float dipDepth = 0;
  };
  void step(Overlay& o, uint32_t dt, uint32_t now);
  float candle(uint8_t eye, uint32_t now, uint32_t dt);
  void paint(uint8_t eye, float level);

  float fade_[2] = {0, 0};
  float agit_ = 0;
  float noticed_ = 0, noticedTarget_ = 0;   // 0 idle .. 1 noticed
  float flick_ = 1, flickTarget_ = 1;       // 1 flickering .. 0 steady
  float noticeGain_ = 1;
  bool agitated_ = false;
  Overlay ov_[2];
  Candle cd_[2];
  float acc_[8][3] = {};   // dithering residue per LED channel
  uint32_t last_ = 0;
};
