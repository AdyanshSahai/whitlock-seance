#pragma once
#include <Arduino.h>
#include "config.h"

// Two eyes (4 LEDs each). Brightness per eye = max(candle * fade, overlay):
//  - candle: random uneven flicker with occasional deeper "draught" dips, different for each eye
//  - fade: 1 normally, slews to 0 when `blocked` (someone is near / dark period)
//  - overlay: one-shot attack / hold / decay (flare, snap-bright, swell, ...)
// Output uses float maths with per-channel error-diffusion (temporal dithering).
class Eyes {
 public:
  void begin();
  void update(uint32_t now, bool blocked, uint32_t fallMs);
  // attackMs == 0 snaps. peak is perceived level 0..1.
  void flash(uint8_t eye, float peak, uint32_t attackMs, uint32_t holdMs, uint32_t decayMs);
  void flashBoth(float peak, uint32_t attackMs, uint32_t holdMs, uint32_t decayMs);
  void cancelOverlays();
  void off();  // immediate black

 private:
  struct Overlay {
    float level = 0, target = 0, attackRate = 0, decayRate = 0;
    uint32_t holdMs = 0, holdUntil = 0;
    bool holdStarted = false;
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
  Overlay ov_[2];
  Candle cd_[2];
  float acc_[8][3] = {};   // dithering residue per LED channel
  uint32_t last_ = 0;
};
