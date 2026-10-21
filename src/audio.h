#pragma once
#include <Arduino.h>
#include "audio_data.h"

// Two independent 8 kHz / 8-bit channels driven from one timer ISR onto the two DACs.
namespace audio {

enum Ch : uint8_t { CH_MURMUR = 0, CH_LINES = 1 };

void begin();
// startOffset is clamped to the clip length. Loops wrap seamlessly (wav2c crossfades the tail).
// gain: 0 = the channel's normal level, otherwise 0..256 (256 = full).
void play(Ch ch, const Clip& clip, bool loop, uint32_t fadeInMs, uint32_t startOffset = 0, uint16_t gain = 0);
void stop(Ch ch, uint32_t fadeOutMs);
// Procedural murmur (see murmur.h) on the murmur channel. Never repeats; stop() it like any clip.
void playMurmur(uint32_t fadeInMs);
void setMurmurVoices(uint8_t n);
void setMurmurGain(uint16_t g256);  // 256 = normal; raise to make the murmur louder
void pump();  // call every loop(): keeps the murmur sample buffer topped up
bool active(Ch ch);
void service(uint32_t now);  // call often: mutes idle amps
void shutdown();             // stop ISR, DACs to midpoint, amps off

}  // namespace audio
