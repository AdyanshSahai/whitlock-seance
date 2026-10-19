#pragma once
#include <stdint.h>

// Procedural "crowd of whisperers" at 8 kHz. Each voice has its own pitch, vocal-tract size,
// speaking rate and breathiness. A voice is a buzz (glottal pulses, wobbling pitch) mixed with
// breath noise, pushed through two formant resonators that glide continuously from one
// syllable's vowel to the next, gated into sentence-like phrases with short pauses, with the
// odd hissy consonant. Nothing repeats and nothing is stored in flash. No Arduino
// dependencies, so tools/murmur_preview.cpp can render it on a PC.
class Murmur {
 public:
  static constexpr int kMaxVoices = 8;
  void begin(uint32_t seed);
  void setVoices(uint8_t n);  // 1..kMaxVoices speaking voices
  int8_t next();              // next sample, signed, 8 kHz

 private:
  struct Voice {
    uint32_t rng = 1;
    // fixed character
    float f0base = 150, scale = 1.0f;   // pitch (Hz) and vocal-tract size (formant scale)
    int32_t voiced = 0;                 // 0 = pure whisper .. 255 = fully voiced
    int32_t amp = 80;                   // loudness, Q8
    int32_t rate = 256;                 // syllable length multiplier, Q8
    // resonators
    int32_t a1 = 0, a2 = 0, b1 = 0, b2 = 0;
    int32_t ac1 = 0, ac2 = 0, ag = 0, bc1 = 0, bc2 = 0, bg = 0;  // Q14
    // formant glide (Hz)
    float f1s = 500, f2s = 1450, f1t = 500, f2t = 1450;
    bool fresh = true;                  // first syllable after a pause
    // pitch
    uint32_t pph = 0, pinc = 0;         // glottal phase accumulator / increment
    int32_t gl = 0;                     // smoothed glottal pulse
    float wob = 0;                      // slow pitch wander
    // timing
    uint32_t phase = 0, inc = 0;        // syllable envelope (Q16 position in a 64-entry table)
    int32_t stress = 256;
    uint32_t hissPhase = 64u << 16, hissInc = 0;
    int32_t hissAmp = 0;
    int32_t sylLeft = 0, sylTotal = 1, pauseLeft = 0, sylIndex = 0, sylCount = 0, ctl = 0;
    int32_t lastNoise = 0;
  };
  void newSyllable(Voice& v);
  void startPhrase(Voice& v);
  void updateFilters(Voice& v);
  int32_t voiceSample(Voice& v);

  Voice v_[kMaxVoices];
  uint8_t nv_ = 5;
  uint32_t seed_ = 1;
};
