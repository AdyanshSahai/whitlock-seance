#include "audio.h"
#include "config.h"

namespace audio {

namespace {

constexpr int32_t ENV_ONE = 65536;

struct Channel {
  const uint8_t* data = nullptr;
  uint32_t len = 0;
  uint32_t pos = 0;
  bool loop = false;
  volatile bool active = false;
  int32_t env = 0;
  int32_t target = 0;
  int32_t step = ENV_ONE;
  uint16_t gain = 256;
};

Channel g_ch[2];
const uint8_t kDacPin[2] = {PIN_DAC_MURMUR, PIN_DAC_LINES};
const uint8_t kSdPin[2] = {PIN_AMP_SD_MURMUR, PIN_AMP_SD_LINES};
bool g_ampOn[2] = {false, false};
uint32_t g_idleSince[2] = {0, 0};
portMUX_TYPE g_mux = portMUX_INITIALIZER_UNLOCKED;
hw_timer_t* g_timer = nullptr;

inline uint8_t IRAM_ATTR nextSample(Channel& c) {
  if (c.env != c.target) {
    if (c.env < c.target) {
      c.env += c.step;
      if (c.env > c.target) c.env = c.target;
    } else {
      c.env -= c.step;
      if (c.env < c.target) c.env = c.target;
    }
  }
  if (!c.active) return 128;
  int32_t s = (int32_t)c.data[c.pos] - 128;
  if (++c.pos >= c.len) {
    if (c.loop) c.pos = 0;
    else c.active = false;
  }
  int32_t v = (s * (c.env >> 8)) >> 8;
  v = (v * c.gain) >> 8;
  if (c.target == 0 && c.env == 0) c.active = false;
  v += 128;
  if (v < 0) v = 0;
  if (v > 255) v = 255;
  return (uint8_t)v;
}

void IRAM_ATTR onTick() {
  dacWrite(kDacPin[0], nextSample(g_ch[0]));
  dacWrite(kDacPin[1], nextSample(g_ch[1]));
}

void setAmp(uint8_t ch, bool on) {
#if USE_AMP_SD
  digitalWrite(kSdPin[ch], on ? AMP_ON_LEVEL : !AMP_ON_LEVEL);
#endif
  g_ampOn[ch] = on;
  g_idleSince[ch] = 0;
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
#if USE_AMP_SD
  for (uint8_t i = 0; i < 2; i++) {
    pinMode(kSdPin[i], OUTPUT);
    digitalWrite(kSdPin[i], !AMP_ON_LEVEL);
  }
#endif
  // First dacWrite() allocates the DAC driver, so do it before the ISR runs.
  dacWrite(kDacPin[0], 128);
  dacWrite(kDacPin[1], 128);
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

void play(Ch ch, const Clip& clip, bool loop, uint32_t fadeInMs, uint32_t startOffset) {
  if (!clip.data || clip.len == 0) return;
  setAmp(ch, true);
  Channel& c = g_ch[ch];
  portENTER_CRITICAL(&g_mux);
  c.data = clip.data;
  c.len = clip.len;
  c.pos = startOffset < clip.len ? startOffset : 0;
  c.loop = loop;
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

bool active(Ch ch) { return g_ch[ch].active; }

void service(uint32_t now) {
  for (uint8_t i = 0; i < 2; i++) {
    if (g_ch[i].active) {
      g_idleSince[i] = 0;
    } else if (g_ampOn[i]) {
      if (g_idleSince[i] == 0) g_idleSince[i] = now ? now : 1;
      else if (now - g_idleSince[i] > AMP_OFF_DELAY_MS) setAmp(i, false);
    }
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
  dacWrite(kDacPin[0], 128);
  dacWrite(kDacPin[1], 128);
#if USE_AMP_SD
  for (uint8_t i = 0; i < 2; i++) digitalWrite(kSdPin[i], !AMP_ON_LEVEL);
#endif
}

}  // namespace audio
