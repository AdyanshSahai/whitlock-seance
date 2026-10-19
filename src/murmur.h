#pragma once
#include <stdint.h>

// Procedural "crowd of whispers" at 8 kHz. Each voice is white noise pushed through two
// wandering formant resonators, gated into syllables and phrases with pauses, plus the odd
// hissy consonant. Nothing repeats, nothing is stored in flash. No Arduino dependencies,
// so tools/murmur_preview.cpp can render it on a PC.
class Murmur {
 public:
  static constexpr int kMaxVoices = 8;
  void begin(uint32_t seed);
  void setVoices(uint8_t n);  // 1..kMaxVoices speaking voices
  int8_t next();              // next sample, signed, 8 kHz

 private:
  struct Voice {
    uint32_t rng = 1;
    int32_t a1 = 0, a2 = 0, b1 = 0, b2 = 0;          // resonator state
    int32_t ac1 = 0, ac2 = 0, ag = 0, bc1 = 0, bc2 = 0, bg = 0;  // Q14 coefficients
    int32_t amp = 80;                                // voice loudness, Q8
    int32_t pitch = 256;                             // formant scale, Q8
    uint32_t phase = 0, inc = 0;                     // syllable envelope position (Q16, 0..64)
    uint32_t hissPhase = 64u << 16, hissInc = 0;     // consonant burst envelope
    int32_t hissAmp = 0;
    int32_t sylLeft = 0, pauseLeft = 0, sylsLeft = 0;
    int32_t lastNoise = 0;
  };
  void newSyllable(Voice& v);
  void startPhrase(Voice& v);
  int32_t voiceSample(Voice& v);

  Voice v_[kMaxVoices];
  uint8_t nv_ = 5;
  uint32_t seed_ = 1;
};
