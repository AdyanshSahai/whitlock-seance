// Host test for src/presence.h:  g++ -std=c++17 -Isrc tools/presence_test.cpp -o /tmp/presence_test && /tmp/presence_test
#include <cstdio>
#include <cstdlib>
#include "presence.h"

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

static PresenceParams P() {
  return PresenceParams{20, 50, 100, 15, 200, 500, 0, 1500, 3000, 120000, 3000, 60000};
}

struct Sim {
  Presence p{P()};
  uint32_t t = 1000;
  int leaves = 0, busyOn = 0, busyOff = 0;
  Tier firstSeenAfter = TIER_IDLE;
  // hold distance d for ms, stepping 20 ms
  void hold(float d, uint32_t ms) {
    for (uint32_t i = 0; i < ms; i += 20) {
      auto e = p.update(t, d);
      leaves += e.left; busyOn += e.busyStarted; busyOff += e.busyEnded;
      t += 20;
    }
  }
};

int main() {
  { // approach through the bands; never skips the dwell (except NEAR)
    Sim s; s.hold(300, 1000);
    CHECK(s.p.tier() == TIER_IDLE, "start idle");
    s.hold(90, 300);  CHECK(s.p.tier() == TIER_IDLE, "FAR needs 0.5 s dwell, got %d", s.p.tier());
    s.hold(90, 400);  CHECK(s.p.tier() == TIER_FAR, "FAR after dwell, got %d", s.p.tier());
    s.hold(40, 600);  CHECK(s.p.tier() == TIER_MID, "MID, got %d", s.p.tier());
    s.hold(10, 20);   CHECK(s.p.tier() == TIER_NEAR, "NEAR is instant, got %d", s.p.tier());
  }
  { // hysteresis: wobbling around a boundary doesn't flip tiers
    Sim s; s.hold(40, 1500); CHECK(s.p.tier() == TIER_MID, "mid");
    for (int i = 0; i < 20; i++) { s.hold(52, 200); s.hold(48, 200); }
    CHECK(s.p.tier() == TIER_MID, "stays MID wobbling 48-52, got %d", s.p.tier());
    s.hold(70, 1000); CHECK(s.p.tier() == TIER_FAR, "MID->FAR past 65, got %d", s.p.tier());
    s.hold(105, 1000); CHECK(s.p.tier() == TIER_FAR, "FAR holds at 105 (exit 115), got %d", s.p.tier());
    s.hold(120, 1000); CHECK(s.p.tier() == TIER_IDLE, "IDLE past 115, got %d", s.p.tier());
    s.hold(35, 600);  // 35 is between NEAR exit and MID
    CHECK(s.p.tier() == TIER_MID, "idle->MID at 35 cm, got %d", s.p.tier());
    s.hold(10, 20);   CHECK(s.p.tier() == TIER_NEAR, "near");
    s.hold(30, 1000); CHECK(s.p.tier() == TIER_NEAR, "NEAR holds at 30 (exit 35), got %d", s.p.tier());
    s.hold(40, 1000); CHECK(s.p.tier() == TIER_MID, "NEAR->MID past 35, got %d", s.p.tier());
  }
  { // a brief dip (<0.5 s) doesn't change tier
    Sim s; s.hold(40, 1500);
    s.hold(90, 300); s.hold(40, 1000);
    CHECK(s.p.tier() == TIER_MID, "short excursion ignored, got %d", s.p.tier());
  }
  { // leave: 3 s in MID/NEAR, then >200 cm for 1.5 s
    Sim s; s.hold(300, 1000);
    s.hold(40, 3500); s.hold(150, 5000);
    CHECK(s.leaves == 0, "no leave while at 150 cm, got %d", s.leaves);
    s.hold(250, 1400); CHECK(s.leaves == 0, "not yet (1.4 s)");
    s.hold(250, 300);  CHECK(s.leaves == 1, "leave fired once, got %d", s.leaves);
    s.hold(250, 5000); CHECK(s.leaves == 1, "no repeat, got %d", s.leaves);
  }
  { // short visit (<3 s in MID) is not a leave
    Sim s; s.hold(300, 1000);
    s.hold(40, 2000); s.hold(250, 3000);
    CHECK(s.leaves == 0, "2 s visit is not a leave, got %d", s.leaves);
  }
  { // FAR-only visit never counts as engaged
    Sim s; s.hold(300, 1000);
    s.hold(80, 10000); s.hold(250, 3000);
    CHECK(s.leaves == 0, "FAR-only visit is not a leave, got %d", s.leaves);
  }
  { // busy mode: after 2 min in MID/NEAR, ends 60 s after it clears
    Sim s; s.hold(300, 1000);
    s.hold(40, 119000); CHECK(s.busyOn == 0, "not busy at 119 s");
    s.hold(40, 2000);   CHECK(s.busyOn == 1 && s.p.busy(), "busy at 2 min");
    s.hold(150, 30000); CHECK(s.p.busy(), "still busy 30 s after clear");
    s.hold(150, 31000); CHECK(!s.p.busy() && s.busyOff == 1, "busy ends after 60 s clear");
  }
  { // a quick step out and back (<3 s) doesn't restart the 2-minute clock
    Sim s; s.hold(300, 1000);
    s.hold(40, 60000); s.hold(150, 2000); s.hold(40, 61000);
    CHECK(s.busyOn == 1, "gap tolerance keeps the run going, got %d", s.busyOn);
  }
  { // but a long gap does
    Sim s; s.hold(300, 1000);
    s.hold(40, 60000); s.hold(150, 10000); s.hold(40, 61000);
    CHECK(s.busyOn == 0, "long gap resets the run, got %d", s.busyOn);
  }
  { // no echo (999) reads as idle
    Sim s; s.hold(999, 5000); CHECK(s.p.tier() == TIER_IDLE, "999 idle");
  }
  printf(fails ? "\n%d FAILED\n" : "all presence tests passed\n", fails);
  return fails ? 1 : 0;
}
