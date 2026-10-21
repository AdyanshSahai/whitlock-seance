#pragma once
#include <stdint.h>

// Distance tiers, visit tracking and "busy mode", as pure logic (no Arduino) so it can be tested on a PC
// (tools/presence_test.cpp). Fed the ultrasonic median distance at ~50 Hz.
//
//   IDLE  beyond FAR       nothing
//   FAR   50-100 cm        "noticed"
//   MID   20-50  cm        "listening"
//   NEAR  < 20   cm        the old "approach"
//
// Moving inward switches at the enter distance; moving outward needs hystCm more. A candidate tier
// must hold for dwellMs before it is adopted (entering NEAR can use its own, shorter, dwell).
enum Tier : uint8_t { TIER_IDLE = 0, TIER_FAR = 1, TIER_MID = 2, TIER_NEAR = 3 };

struct PresenceParams {
  float nearCm, midCm, farCm;   // enter thresholds (moving inward)
  float hystCm;                 // extra distance needed to move outward
  float leaveCm;                // beyond this for leaveGoneMs ends a visit
  uint32_t dwellMs, nearDwellMs;
  uint32_t leaveGoneMs, leaveMinEngagedMs;
  uint32_t busyAfterMs, busyGapMs, busyClearMs;
};

class Presence {
 public:
  struct Events {
    bool tierChanged = false;
    Tier from = TIER_IDLE;
    bool left = false;          // someone was in MID/NEAR long enough, then went away
    bool busyStarted = false;
    bool busyEnded = false;
  };

  explicit Presence(const PresenceParams& p) : p_(p) {}

  Events update(uint32_t now, float d) {
    Events ev;
    uint32_t dt = first_ ? 0 : now - last_;
    if (dt > 100) dt = 100;
    last_ = now;
    first_ = false;

    // ---- tier with hysteresis and dwell
    Tier tin = d < p_.nearCm ? TIER_NEAR : d < p_.midCm ? TIER_MID : d < p_.farCm ? TIER_FAR : TIER_IDLE;
    float h = p_.hystCm;
    Tier tout = d < p_.nearCm + h ? TIER_NEAR : d < p_.midCm + h ? TIER_MID : d < p_.farCm + h ? TIER_FAR : TIER_IDLE;
    Tier want = tier_;
    if (tin > tier_) want = tin;          // moved inward past an enter distance
    else if (tout < tier_) want = tout;   // moved outward past an exit distance
    if (want == tier_) {
      cand_ = tier_;
    } else {
      if (want != cand_) { cand_ = want; candSince_ = now; }
      uint32_t dwell = (want == TIER_NEAR) ? p_.nearDwellMs : p_.dwellMs;
      if (now - candSince_ >= dwell) {
        ev.tierChanged = true;
        ev.from = tier_;
        tier_ = want;
        cand_ = want;
      }
    }

    // ---- visit: time spent in MID/NEAR, ended by being beyond leaveCm for a while
    bool engaged = tier_ >= TIER_MID;
    if (engaged) engaged_ += dt;
    if (d > p_.leaveCm) {
      if (!farSince_) farSince_ = now ? now : 1;
      if (now - farSince_ >= p_.leaveGoneMs) {
        if (engaged_ >= p_.leaveMinEngagedMs) ev.left = true;
        engaged_ = 0;
      }
    } else {
      farSince_ = 0;
    }

    // ---- busy mode: 2+ min continuously in MID/NEAR; ends after a quiet spell
    if (engaged) {
      if (!runSince_) runSince_ = now ? now : 1;
      lastEngaged_ = now;
      clearSince_ = 0;
    } else {
      if (runSince_ && now - lastEngaged_ > p_.busyGapMs) runSince_ = 0;
      if (!clearSince_) clearSince_ = now ? now : 1;
    }
    if (!busy_) {
      if (runSince_ && now - runSince_ >= p_.busyAfterMs) { busy_ = true; ev.busyStarted = true; }
    } else if (!engaged && clearSince_ && now - clearSince_ >= p_.busyClearMs) {
      busy_ = false;
      runSince_ = 0;
      ev.busyEnded = true;
    }
    return ev;
  }

  Tier tier() const { return tier_; }
  bool busy() const { return busy_; }

 private:
  PresenceParams p_;
  Tier tier_ = TIER_IDLE, cand_ = TIER_IDLE;
  uint32_t candSince_ = 0, last_ = 0, engaged_ = 0, farSince_ = 0;
  uint32_t runSince_ = 0, lastEngaged_ = 0, clearSince_ = 0;
  bool first_ = true, busy_ = false;
};
