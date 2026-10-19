#include "murmur.h"
#include <math.h>

namespace {

constexpr float kFs = 8000.0f;
constexpr int32_t kOutGain = 12;   // output level, 64 = x1. Check with tools/murmur_preview.cpp
uint8_t g_env[65];                  // sin^2 syllable shape, 0..255

inline uint32_t xs(uint32_t& s) {
  s ^= s << 13;
  s ^= s >> 17;
  s ^= s << 5;
  return s;
}
inline int32_t rnd(uint32_t& s, int32_t lo, int32_t hi) { return lo + (int32_t)(xs(s) % (uint32_t)(hi - lo + 1)); }

void setReso(float f, float r, int32_t& c1, int32_t& c2, int32_t& g) {
  float th = 2.0f * 3.14159265f * f / kFs;
  c1 = (int32_t)(2.0f * r * cosf(th) * 16384.0f);
  c2 = (int32_t)(-r * r * 16384.0f);
  g = (int32_t)((1.0f - r * r) * sinf(th) * 16384.0f);
}

}  // namespace

void Murmur::begin(uint32_t seed) {
  for (int i = 0; i <= 64; i++) {
    float s = sinf(3.14159265f * i / 64.0f);
    g_env[i] = (uint8_t)(255.0f * s * s);
  }
  seed_ = seed ? seed : 1;
  static const int32_t kPitch[5] = {205, 230, 256, 295, 335};  // x0.8 .. x1.3
  for (int i = 0; i < kMaxVoices; i++) {
    Voice& v = v_[i];
    v = Voice();
    v.rng = seed_ * 2654435761u + (uint32_t)i * 40503u + 1;
    for (int k = 0; k < 4; k++) xs(v.rng);
    v.pitch = kPitch[i % 5];
    v.amp = rnd(v.rng, 55, 115);
    v.pauseLeft = rnd(v.rng, 200, 9000);   // stagger so they don't all start together
  }
}

void Murmur::setVoices(uint8_t n) {
  if (n < 1) n = 1;
  if (n > kMaxVoices) n = kMaxVoices;
  for (uint8_t i = nv_; i < n; i++) {      // newly joined voices wait a moment before speaking
    v_[i].pauseLeft = rnd(v_[i].rng, 200, 6000);
    v_[i].sylLeft = 0;
  }
  nv_ = n;
}

void Murmur::startPhrase(Voice& v) { v.sylsLeft = rnd(v.rng, 2, 6); }

void Murmur::newSyllable(Voice& v) {
  float scale = v.pitch / 256.0f;
  float f1 = rnd(v.rng, 250, 850) * scale;
  float f2 = rnd(v.rng, 900, 2500) * scale;
  if (f2 > 3300.0f) f2 = 3300.0f;
  setReso(f1, 0.955f, v.ac1, v.ac2, v.ag);
  setReso(f2, 0.945f, v.bc1, v.bc2, v.bg);
  v.a1 = v.a2 = v.b1 = v.b2 = 0;
  int32_t len = rnd(v.rng, 640, 1600);     // 80-200 ms
  v.sylLeft = len;
  v.phase = 0;
  v.inc = (uint32_t)((64u << 16) / (uint32_t)len);
  if (rnd(v.rng, 0, 99) < 45) {            // a hissy consonant at the start
    int32_t hl = rnd(v.rng, 120, 360);
    v.hissPhase = 0;
    v.hissInc = (uint32_t)((64u << 16) / (uint32_t)hl);
    v.hissAmp = rnd(v.rng, 40, 110);
  } else {
    v.hissPhase = 64u << 16;
  }
  if (v.sylsLeft <= 0) startPhrase(v);
}

int32_t Murmur::voiceSample(Voice& v) {
  if (v.pauseLeft > 0) {
    if (--v.pauseLeft == 0) startPhrase(v);
    return 0;
  }
  if (v.sylLeft <= 0) newSyllable(v);

  int32_t n = (int32_t)(xs(v.rng) >> 20) - 2048;  // +-2048 white noise
  int32_t ya = ((v.ag * n) >> 14) + ((v.ac1 * v.a1) >> 14) + ((v.ac2 * v.a2) >> 14);
  v.a2 = v.a1; v.a1 = ya;
  int32_t yb = ((v.bg * n) >> 14) + ((v.bc1 * v.b1) >> 14) + ((v.bc2 * v.b2) >> 14);
  v.b2 = v.b1; v.b1 = yb;
  int32_t s = ya + (yb >> 1);

  uint32_t idx = v.phase >> 16;
  if (idx > 64) idx = 64;
  s = (s * (int32_t)g_env[idx]) >> 8;
  v.phase += v.inc;

  if (v.hissPhase < (64u << 16)) {                // high-passed noise burst
    uint32_t hi = v.hissPhase >> 16;
    int32_t h = ((n - v.lastNoise) * (int32_t)g_env[hi]) >> 8;
    s += (h * v.hissAmp) >> 9;
    v.hissPhase += v.hissInc;
  }
  v.lastNoise = n;

  if (--v.sylLeft <= 0) {
    if (--v.sylsLeft <= 0) {
      // end of phrase: short gap, sometimes a long one
      v.pauseLeft = (rnd(v.rng, 0, 99) < 15) ? rnd(v.rng, 24000, 48000) : rnd(v.rng, 2400, 12000);
    }
  }
  return (s * v.amp) >> 8;
}

int8_t Murmur::next() {
  int32_t sum = 0;
  for (uint8_t i = 0; i < nv_; i++) sum += voiceSample(v_[i]);
  sum = (sum * kOutGain) >> 6;       // overall level (x kOutGain/64)
  // soft knee: linear up to +-60, then compressed, so rare loud peaks don't clip
  int32_t a = sum < 0 ? -sum : sum;
  if (a > 60) a = 60 + ((a - 60) >> 2);
  if (a > 127) a = 127;
  return (int8_t)(sum < 0 ? -a : a);
}
