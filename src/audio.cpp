#include "audio.h"
#include "config.h"
#include "murmur.h"

namespace audio {

namespace {

constexpr int32_t ENV_ONE = 65536;

struct Channel {
  const uint8_t* data = nullptr;
  uint32_t len = 0;
  uint32_t pos = 0;
  bool loop = false;
  volatile bool active = false;
  bool synth = false;  // true: samples come from the murmur ring buffer, not a clip
  int32_t env = 0;
  int32_t target = 0;
  int32_t step = ENV_ONE;
  uint16_t gain = 256;
};

// Two logical channels (murmur, Whitlock's lines) mixed onto the single DAC / speaker.
Channel g_ch[2];

// Murmur synth renders ahead into a ring buffer (pump() fills it, the ISR drains it).
constexpr uint16_t kRing = 2048;  // 256 ms
int8_t g_ring[kRing];
volatile uint16_t g_head = 0, g_tail = 0;
Murmur g_murmur;
int32_t g_duck = 256;      // murmur level multiplier 0..256, drops while a line is playing
uint8_t g_duckDiv = 0;
bool g_ampOn = false;
uint32_t g_idleSince = 0;
portMUX_TYPE g_mux = portMUX_INITIALIZER_UNLOCKED;
hw_timer_t* g_timer = nullptr;

// Returns a signed sample (-128..127) for one channel and advances it.
inline int32_t IRAM_ATTR nextSample(Channel& c) {
  if (c.env != c.target) {
    if (c.env < c.target) {
      c.env += c.step;
      if (c.env > c.target) c.env = c.target;
    } else {
      c.env -= c.step;
      if (c.env < c.target) c.env = c.target;
    }
  }
  if (!c.active) return 0;
  int32_t s;
  if (c.synth) {
    uint16_t t = g_tail;
    if (t != g_head) {
      s = g_ring[t & (kRing - 1)];
      g_tail = t + 1;
    } else {
      s = 0;  // underrun: silence rather than a glitch
    }
  } else {
    s = (int32_t)c.data[c.pos] - 128;
    if (++c.pos >= c.len) {
      if (c.loop) c.pos = 0;
      else c.active = false;
    }
  }
  int32_t v = (s * (c.env >> 8)) >> 8;
  v = (v * c.gain) >> 8;
  if (c.target == 0 && c.env == 0) c.active = false;
  return v;
}

void IRAM_ATTR onTick() {
  // Slew the duck level slowly (about 0.5 s end to end) so it isn't audible as a step.
  if ((++g_duckDiv & 15) == 0) {
    int32_t want = g_ch[1].active ? DUCK_LEVEL : 256;
    if (g_duck < want) g_duck++;
    else if (g_duck > want) g_duck--;
  }
  int32_t m = (nextSample(g_ch[0]) * g_duck) >> 8;
  int32_t l = nextSample(g_ch[1]);
  int32_t v = 128 + m + l;
  if (v < 0) v = 0;
  if (v > 255) v = 255;
  dacWrite(PIN_DAC, (uint8_t)v);
}

void setAmp(bool on) {
#if USE_AMP_SD
  digitalWrite(PIN_AMP_SD, on ? AMP_ON_LEVEL : !AMP_ON_LEVEL);
#endif
  g_ampOn = on;
  g_idleSince = 0;
}

int32_t stepFor(uint32_t ms) {
  uint32_t samples = ms * (AUDIO_RATE_HZ / 1000);
  if (samples == 0) return ENV_ONE;
  int32_t s = ENV_ONE / (int32_t)samples;
  return s < 1 ? 1 : s;
}

}  // namespace

void begin() {
  g_ch[0].gain = GAIN_MURMUR;
  g_ch[1].gain = GAIN_LINES;
  g_murmur.begin((uint32_t)random(2147483647L) | 1u);
  g_murmur.setVoices(MURMUR_VOICES);
#if USE_AMP_SD
  pinMode(PIN_AMP_SD, OUTPUT);
  digitalWrite(PIN_AMP_SD, !AMP_ON_LEVEL);
#endif
  // First dacWrite() allocates the DAC driver, so do it before the ISR runs.
  dacWrite(PIN_DAC, 128);
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  g_timer = timerBegin(1000000);
  timerAttachInterrupt(g_timer, &onTick);
  timerAlarm(g_timer, 1000000 / AUDIO_RATE_HZ, true, 0);
#else
  g_timer = timerBegin(0, 80, true);
  timerAttachInterrupt(g_timer, &onTick, true);
  timerAlarmWrite(g_timer, 1000000 / AUDIO_RATE_HZ, true);
  timerAlarmEnable(g_timer);
#endif
}

void play(Ch ch, const Clip& clip, bool loop, uint32_t fadeInMs, uint32_t startOffset, uint16_t gain) {
  if (!clip.data || clip.len == 0) return;
  setAmp(true);
  Channel& c = g_ch[ch];
  portENTER_CRITICAL(&g_mux);
  c.data = clip.data;
  c.len = clip.len;
  c.pos = startOffset < clip.len ? startOffset : 0;
  c.loop = loop;
  c.synth = false;
  c.gain = gain ? gain : (ch == CH_LINES ? GAIN_LINES : GAIN_MURMUR);
  c.env = 0;
  c.target = ENV_ONE;
  c.step = stepFor(fadeInMs < 30 ? 30 : fadeInMs);  // 30 ms floor covers amp wake-up
  c.active = true;
  portEXIT_CRITICAL(&g_mux);
}

void stop(Ch ch, uint32_t fadeOutMs) {
  Channel& c = g_ch[ch];
  portENTER_CRITICAL(&g_mux);
  if (c.active) {
    c.target = 0;
    c.step = stepFor(fadeOutMs);
  }
  portEXIT_CRITICAL(&g_mux);
}

void playMurmur(uint32_t fadeInMs) {
  setAmp(true);
  Channel& c = g_ch[CH_MURMUR];
  portENTER_CRITICAL(&g_mux);
  g_tail = 0;
  g_head = 0;
  c.synth = true;
  c.data = nullptr;
  c.len = 0;
  c.pos = 0;
  c.loop = false;
  c.env = 0;
  c.target = ENV_ONE;
  c.step = stepFor(fadeInMs < 30 ? 30 : fadeInMs);
  c.active = true;
  portEXIT_CRITICAL(&g_mux);
}

void setMurmurVoices(uint8_t n) { g_murmur.setVoices(n); }

void setMurmurGain(uint16_t g256) { g_ch[CH_MURMUR].gain = g256; }

void pump() {
  Channel& c = g_ch[CH_MURMUR];
  if (!(c.active && c.synth)) return;
  uint16_t h = g_head;
  while ((uint16_t)(h - g_tail) < kRing - 1) {
    g_ring[h & (kRing - 1)] = g_murmur.next();
    h = h + 1;
  }
  g_head = h;
}

bool active(Ch ch) { return g_ch[ch].active; }

void service(uint32_t now) {
  if (g_ch[0].active || g_ch[1].active) {
    g_idleSince = 0;
  } else if (g_ampOn) {
    if (g_idleSince == 0) g_idleSince = now ? now : 1;
    else if (now - g_idleSince > AMP_OFF_DELAY_MS) setAmp(false);
  }
}

void shutdown() {
  if (g_timer) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
    timerEnd(g_timer);
#else
    timerAlarmDisable(g_timer);
    timerEnd(g_timer);
#endif
    g_timer = nullptr;
  }
  g_ch[0].active = false;
  g_ch[1].active = false;
  dacWrite(PIN_DAC, 128);
#if USE_AMP_SD
  digitalWrite(PIN_AMP_SD, !AMP_ON_LEVEL);
#endif
}

}  // namespace audio
