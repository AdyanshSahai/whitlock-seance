#pragma once
#include <Arduino.h>

// Two eyes (4 LEDs each). Brightness per eye = max(base, overlay):
//  - base: slow breathing, slewed to dark when `blocked`
//  - overlay: one-shot attack / hold / decay (flare, snap-bright, swell, cue)
class Eyes {
 public:
  void begin();
  void update(uint32_t now, bool blocked, uint32_t fallMs);
  // attackMs == 0 snaps. peak is perceived level 0..1.
  void flash(uint8_t eye, float peak, uint32_t attackMs, uint32_t holdMs, uint32_t decayMs);
  void flashBoth(float peak, uint32_t attackMs, uint32_t holdMs, uint32_t decayMs);
  void cancelOverlays();
  void off();  // immediate black
  void setColor(uint8_t r, uint8_t g, uint8_t b) { r_ = r; g_ = g; b_ = b; }

 private:
  struct Overlay {
    float level = 0, target = 0, attackRate = 0, decayRate = 0;
    uint32_t holdMs = 0, holdUntil = 0;
    bool holdStarted = false;
  };
  void step(Overlay& o, uint32_t dt, uint32_t now);
  void paint(uint8_t eye, float level);

  float base_[2] = {0, 0};
  Overlay ov_[2];
  uint32_t last_ = 0;
  uint8_t r_ = 0, g_ = 0, b_ = 0;
};
