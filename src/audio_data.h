#pragma once
#include <Arduino.h>

// Clip tables for Whitlock's spoken lines and reactions. The definitions live in audio_data.cpp, generated
// by tools/wav2c.py. (The murmur is synthesised, see murmur.h.)
struct Clip {
  const uint8_t* data;  // unsigned 8-bit PCM, 8 kHz, 128 = silence
  uint32_t len;
};

extern const Clip CLIPS_NOTTHAT[];  // book pool: "not that one" and variants
extern const uint8_t N_NOTTHAT;
extern const Clip CLIPS_REACT[];    // reaction pool: wait, stay, dont go, please, breath, sigh...
extern const uint8_t N_REACT;
