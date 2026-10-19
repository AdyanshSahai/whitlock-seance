#include "murmur.h"
#include <math.h>

namespace {

constexpr float kFs = 8000.0f;
constexpr int32_t kOutGain = 12;   // output level, 64 = x1. Check with tools/murmur_preview.cpp
constexpr int32_t kPulse = 7500;   // glottal pulse height relative to +-2048 breath noise
constexpr int kCtl = 64;           // filters / pitch updated every 64 samples (8 ms)
uint8_t g_env[65];                 // syllable shape, 0..255, peaks early like speech

inline uint32_t xs(uint32_t& s) {
  s ^= s << 13;
  s ^= s >> 17;
  s ^= s << 5;
  return s;
}
inline int32_t rnd(uint32_t& s, int32_t lo, int32_t hi) { return lo + (int32_t)(xs(s) % (uint32_t)(hi - lo + 1)); }
inline float frnd(uint32_t& s, float lo, float hi) { return lo + (hi - lo) * (xs(s) >> 8) / 16777216.0f; }
inline int32_t lim(int32_t v) { return v > 60000 ? 60000 : (v < -60000 ? -60000 : v); }

void setReso(float f, float r, int32_t& c1, int32_t& c2, int32_t& g) {
  float th = 2.0f * 3.14159265f * f / kFs;
  c1 = (int32_t)(2.0f * r * cosf(th) * 16384.0f);
  c2 = (int32_t)(-r * r * 16384.0f);
  g = (int32_t)((1.0f - r * r) * sinf(th) * 16384.0f);
}

struct Profile { float f0, scale; int32_t voiced; };
// Diverse first so any voice count sounds like different people: deep / light, breathy / buzzy.
const Profile kProfiles[Murmur::kMaxVoices] = {
    {110, 0.88f, 130}, {215, 1.15f, 0},   {150, 1.00f, 90},  {250, 1.22f, 150},
    {125, 0.92f, 0},   {185, 1.08f, 110}, {280, 1.30f, 0},   {100, 0.85f, 160}};

}  // namespace

void Murmur::begin(uint32_t seed) {
  for (int i = 0; i <= 64; i++) {
    float t = powf(i / 64.0f, 0.7f);   // skew so the syllable peaks a little early
    float s = sinf(3.14159265f * t);
    g_env[i] = (uint8_t)(255.0f * s * s);
  }
  seed_ = seed ? seed : 1;
  for (int i = 0; i < kMaxVoices; i++) {
    Voice& v = v_[i];
    v = Voice();
    v.rng = seed_ * 2654435761u + (uint32_t)i * 40503u + 1;
    for (int k = 0; k < 4; k++) xs(v.rng);
    v.f0base = kProfiles[i].f0;
    v.scale = kProfiles[i].scale;
    v.voiced = kProfiles[i].voiced;
    v.amp = rnd(v.rng, 50, 120);
    v.rate = rnd(v.rng, 190, 340);               // x0.75 .. x1.33 speaking speed
    v.pauseLeft = rnd(v.rng, 200, 7000);          // stagger the starts
  }
}

void Murmur::setVoices(uint8_t n) {
  if (n < 1) n = 1;
  if (n > kMaxVoices) n = kMaxVoices;
  for (uint8_t i = nv_; i < n; i++) {             // newly joined voices wait a moment
    v_[i].pauseLeft = rnd(v_[i].rng, 200, 4000);
    v_[i].sylLeft = 0;
  }
  nv_ = n;
}

void Murmur::startPhrase(Voice& v) {
  v.sylCount = rnd(v.rng, 3, 8);   // a sentence-sized run of syllables
  v.sylIndex = -1;
}

void Murmur::newSyllable(Voice& v) {
  if (v.sylIndex < 0 || v.sylIndex + 1 >= v.sylCount) { if (v.sylIndex + 1 >= v.sylCount) startPhrase(v); }
  v.sylIndex++;
  // vowel glide: start where the last syllable ended (coarticulation), head for a new target
  if (v.fresh) { v.f1s = 500.0f * v.scale; v.f2s = 1450.0f * v.scale; v.fresh = false; }
  else { v.f1s = v.f1t; v.f2s = v.f2t; }
  if (rnd(v.rng, 0, 99) < 22) {                   // nasal "mm": low and dark
    v.f1t = 250.0f * v.scale; v.f2t = frnd(v.rng, 900.0f, 1300.0f) * v.scale;
  } else {
    v.f1t = frnd(v.rng, 280.0f, 820.0f) * v.scale;
    v.f2t = frnd(v.rng, 950.0f, 2400.0f) * v.scale;
  }
  if (v.f2t > 3300.0f) v.f2t = 3300.0f;
  v.a1 = v.a2 = v.b1 = v.b2 = 0;

  int32_t len = (rnd(v.rng, 560, 1500) * v.rate) >> 8;
  bool last = (v.sylIndex + 1 >= v.sylCount);
  if (last) len = len * 13 / 10;                  // phrase-final lengthening
  v.sylLeft = v.sylTotal = len;
  v.phase = 0;
  v.inc = (uint32_t)((64u << 16) / (uint32_t)len);
  v.stress = rnd(v.rng, 170, 300);                // some syllables stressed, some soft
  if (v.sylIndex == 0) v.stress = v.stress * 12 / 10;

  if (rnd(v.rng, 0, 99) < 35) {                   // a hissy consonant at the start
    int32_t hl = rnd(v.rng, 120, 360);
    v.hissPhase = 0;
    v.hissInc = (uint32_t)((64u << 16) / (uint32_t)hl);
    v.hissAmp = rnd(v.rng, 40, 110);
  } else {
    v.hissPhase = 64u << 16;
  }
  updateFilters(v);
}

void Murmur::updateFilters(Voice& v) {
  float p = 1.0f - (float)v.sylLeft / (float)v.sylTotal;
  float gl = p < 0.7f ? p / 0.7f : 1.0f;
  gl = gl * gl * (3.0f - 2.0f * gl);               // smoothstep glide
  float f1 = v.f1s + (v.f1t - v.f1s) * gl;
  float f2 = v.f2s + (v.f2t - v.f2s) * gl;
  setReso(f1, 0.955f, v.ac1, v.ac2, v.ag);
  setReso(f2, 0.945f, v.bc1, v.bc2, v.bg);
  // pitch: falls through the phrase, wanders slowly, jitters a little
  float q = ((float)v.sylIndex + p) / (float)(v.sylCount > 0 ? v.sylCount : 1);
  v.wob += frnd(v.rng, -0.004f, 0.004f);
  if (v.wob > 0.05f) v.wob = 0.05f;
  if (v.wob < -0.05f) v.wob = -0.05f;
  float f0 = v.f0base * (1.10f - 0.22f * q) * (1.0f + v.wob) * (1.0f + frnd(v.rng, -0.012f, 0.012f));
  v.pinc = (uint32_t)(f0 / kFs * 4294967296.0f);
}

int32_t Murmur::voiceSample(Voice& v) {
  if (v.pauseLeft > 0) {
    if (--v.pauseLeft == 0) startPhrase(v);
    return 0;
  }
  if (v.sylLeft <= 0) newSyllable(v);
  if ((v.ctl++ & (kCtl - 1)) == 0) updateFilters(v);

  int32_t n = (int32_t)(xs(v.rng) >> 20) - 2048;  // +-2048 breath noise
  uint32_t prev = v.pph;
  v.pph += v.pinc;
  int32_t pulse = (v.pph < prev) ? kPulse : 0;    // glottal pulse each pitch period
  v.gl += (pulse - v.gl) >> 1;                    // rounded off like a real glottal pulse, not a spike
  int32_t exc = (v.voiced * v.gl + (255 - v.voiced) * n) >> 8;

  int32_t ya = ((v.ag * exc) >> 14) + ((v.ac1 * v.a1) >> 14) + ((v.ac2 * v.a2) >> 14);
  ya = lim(ya);
  v.a2 = v.a1; v.a1 = ya;
  int32_t yb = ((v.bg * exc) >> 14) + ((v.bc1 * v.b1) >> 14) + ((v.bc2 * v.b2) >> 14);
  yb = lim(yb);
  v.b2 = v.b1; v.b1 = yb;
  int32_t s = ya + (yb >> 1);

  uint32_t idx = v.phase >> 16;
  if (idx > 64) idx = 64;
  s = (s * (int32_t)g_env[idx]) >> 8;
  s = (s * v.stress) >> 8;
  v.phase += v.inc;

  if (v.hissPhase < (64u << 16)) {                // high-passed breath burst
    uint32_t hi = v.hissPhase >> 16;
    int32_t h = ((n - v.lastNoise) * (int32_t)g_env[hi]) >> 8;
    s += (h * v.hissAmp) >> 9;
    v.hissPhase += v.hissInc;
  }
  v.lastNoise = n;

  if (--v.sylLeft <= 0 && v.sylIndex + 1 >= v.sylCount) {
    // end of the phrase: a short breath, rarely a longer one
    v.pauseLeft = (rnd(v.rng, 0, 99) < 6) ? rnd(v.rng, 8000, 16000) : rnd(v.rng, 1600, 5600);
    v.fresh = true;
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
