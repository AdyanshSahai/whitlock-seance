// Whitlock seance display. See README.md for wiring and tuning.
#include <Arduino.h>
#include <driver/gpio.h>
#include <esp_sleep.h>
#include "audio.h"
#include "config.h"
#include "eyes.h"
#include "sensors.h"

enum class Mode : uint8_t { Idle, Rare };

static Ultrasonic us;
static IrSensor ir;
static Battery bat;
static Eyes eyes;

static Mode mode = Mode::Idle;
static bool present = false;
static uint32_t presentSince = 0, goneSince = 0, rawSince = 0;
static uint32_t lastActivity = 0;

// murmur scheduler
static bool murmurOn = false;
static uint32_t murmurNext = 0, murmurEnd = 0;
static int8_t lastNotThat = -1, lastWait = -1;

// wait line
static uint32_t waitPendingAt = 0, waitCooldownUntil = 0;
static uint32_t leaveLog[BUSY_LEAVES];
static uint8_t leaveHead = 0;

// book / IR
static uint32_t irCooldownUntil = 0;

// rare event
static uint32_t rareStart = 0, lastRare = 0, darkUntil = 0, rareIdleNeeded = 0;
static uint8_t rareCount = 0;

static uint32_t lastTick = 0, lastLog = 0;

static uint32_t rnd(uint32_t lo, uint32_t hi) { return lo + (uint32_t)random(hi - lo + 1); }

static const Clip* pick(const Clip* arr, uint8_t n, int8_t& last) {
  if (n == 0) return nullptr;
  int8_t i = random(n);
  if (n > 1 && i == last) i = (i + 1) % n;
  last = i;
  return &arr[i];
}

static void rescheduleRare() { rareIdleNeeded = RARE_IDLE_MS + rnd(0, RARE_IDLE_JITTER_MS); }

// Battery cutoff. The rail is protected by the cell PCB; this just stops the load first.
static void goDead(const char* why) {
  LOG("dead: %s\n", why);
  audio::shutdown();
  eyes.off();
  pinMode(PIN_LED_DATA, INPUT);
  gpio_reset_pin((gpio_num_t)PIN_LED_DATA);
  gpio_set_direction((gpio_num_t)PIN_LED_DATA, GPIO_MODE_OUTPUT);
  gpio_set_level((gpio_num_t)PIN_LED_DATA, 0);
  gpio_hold_en((gpio_num_t)PIN_LED_DATA);
#if USE_PERIPH_SWITCH
  digitalWrite(PIN_PERIPH_EN, LOW);
  gpio_hold_en((gpio_num_t)PIN_PERIPH_EN);
#endif
#if USE_AMP_SD
  gpio_hold_en((gpio_num_t)PIN_AMP_SD);
#endif
  gpio_deep_sleep_hold_en();
  delay(50);
  esp_deep_sleep_start();  // no wake source: stays down until power-cycled
}

// ------------------------------------------------------------------ events

static void abortRare() {
  if (mode != Mode::Rare) return;
  mode = Mode::Idle;
  darkUntil = 0;
  murmurOn = false;
}

static void onApproach(uint32_t now) {
  present = true;
  presentSince = now;
  goneSince = 0;
  abortRare();
  audio::stop(audio::CH_MURMUR, MURMUR_CUT_MS);  // dead, with a click-free ramp
  murmurOn = false;
  eyes.cancelOverlays();
  waitPendingAt = 0;
  LOG("approach\n");
}

static bool isBusy(uint32_t now) {
  uint8_t n = 0;
  for (uint8_t i = 0; i < BUSY_LEAVES; i++)
    if (leaveLog[i] && now - leaveLog[i] < BUSY_WINDOW_MS) n++;
  return n >= BUSY_LEAVES;
}

static void onLeave(uint32_t now, bool longEnough) {
  present = false;
  lastActivity = now;
  murmurOn = false;
  murmurNext = now + rnd(MURMUR_OFF_MIN_MS, MURMUR_OFF_MAX_MS);
  LOG("leave (long=%d)\n", longEnough);
  if (!longEnough) return;
  eyes.flashBoth(FLARE_PEAK, 60, FLARE_HOLD_MS, FLARE_DECAY_MS);
  leaveLog[leaveHead] = now;
  leaveHead = (leaveHead + 1) % BUSY_LEAVES;
  if (now >= waitCooldownUntil && random(WAIT_ODDS) == 0) waitPendingAt = now + WAIT_DELAY_MS;
}

static void onBook(uint32_t now) {
  abortRare();
  irCooldownUntil = now + IR_COOLDOWN_MS;
  waitPendingAt = 0;
  eyes.flashBoth(1.0f, 0, BOOK_HOLD_MS, BOOK_DECAY_MS);
  const Clip* c = pick(CLIPS_NOTTHAT, N_NOTTHAT, lastNotThat);
  if (c) audio::play(audio::CH_LINES, *c, false, 30);
  LOG("book\n");
}

static void startRare(uint32_t now) {
  mode = Mode::Rare;
  rareStart = now;
  lastRare = now;
  rareCount++;
  audio::setMurmurVoices(MURMUR_VOICES);
  audio::playMurmur(RARE_SWELL_MS);
  eyes.flashBoth(1.0f, RARE_SWELL_MS, RARE_HOLD_MS, 1500);
  murmurOn = false;
  LOG("rare event\n");
}

// -------------------------------------------------------------- per-tick work

static void updatePresence(uint32_t now) {
  bool raw = us.present();
  if (!present) {
    if (!raw) {
      rawSince = 0;
    } else {
      if (!rawSince) rawSince = now ? now : 1;
      if (now - rawSince >= APPROACH_DELAY_MS) {
        rawSince = 0;
        onApproach(now);
      }
    }
    return;
  }
  lastActivity = now;
  if (raw) {
    goneSince = 0;
  } else {
    if (!goneSince) goneSince = now;
    if (now - goneSince >= LEAVE_GONE_MS) {
      bool longEnough = (goneSince - presentSince) >= LEAVE_MIN_PRESENT_MS;
      onLeave(now, longEnough);
    }
  }
}

static void handleIr(uint32_t now) {
  ir.update(now);
  if (ir.rose()) {
    lastActivity = now;
    if (now >= irCooldownUntil) onBook(now);
  }
}

static void murmurScheduler(uint32_t now) {
  if (present || now < darkUntil) return;
  if (!murmurOn && now >= murmurNext) {
    audio::setMurmurVoices(MURMUR_VOICES);
    audio::playMurmur(MURMUR_FADE_IN_MS);
    murmurOn = true;
    murmurEnd = now + rnd(MURMUR_ON_MIN_MS, MURMUR_ON_MAX_MS);
  } else if (murmurOn && now >= murmurEnd) {
    audio::stop(audio::CH_MURMUR, MURMUR_FADE_OUT_MS);
    murmurOn = false;
    murmurNext = now + rnd(MURMUR_OFF_MIN_MS, MURMUR_OFF_MAX_MS);
  }
}

static void waitScheduler(uint32_t now) {
  if (!waitPendingAt || now < waitPendingAt) return;
  waitPendingAt = 0;
  if (present || audio::active(audio::CH_LINES)) return;
  const Clip* c = pick(CLIPS_WAIT, N_WAIT, lastWait);
  if (!c) return;
  audio::play(audio::CH_LINES, *c, false, 30);
  waitCooldownUntil = now + (isBusy(now) ? WAIT_COOLDOWN_BUSY_MS : WAIT_COOLDOWN_MS);
  LOG("wait\n");
}

static void rareCheck(uint32_t now) {
  if (mode != Mode::Idle || present || murmurOn || now < darkUntil) return;
  if (rareCount >= RARE_MAX_PER_BOOT) return;
  if (rareCount && now - lastRare < RARE_MIN_SPACING_MS) return;
  if (audio::active(audio::CH_LINES)) return;
  if (now - lastActivity >= rareIdleNeeded) startRare(now);
}

static void rareStep(uint32_t now) {
  if (mode != Mode::Rare) return;
  uint32_t el = now - rareStart;
  if (el > RARE_SWELL_MS) el = RARE_SWELL_MS;
  audio::setMurmurVoices(MURMUR_VOICES + (el * RARE_EXTRA_VOICES) / RARE_SWELL_MS);  // the crowd gathers
  if (now - rareStart < RARE_SWELL_MS + RARE_HOLD_MS) return;
  audio::stop(audio::CH_MURMUR, 40);  // then dead silence
  mode = Mode::Idle;
  darkUntil = now + 1500 + RARE_SILENCE_MS;
  murmurNext = darkUntil + rnd(MURMUR_OFF_MIN_MS, MURMUR_OFF_MAX_MS);
  lastActivity = now;
  rescheduleRare();
}

static void tick(uint32_t now) {
  bat.update(now);
#if BATTERY_CUTOFF
  if (bat.low()) goDead("battery");
#endif

  updatePresence(now);
  handleIr(now);
  murmurScheduler(now);
  waitScheduler(now);
  rareCheck(now);
  rareStep(now);

  bool blocked = present || now < darkUntil;
  eyes.update(now, blocked, EYE_APPROACH_FADE_MS);
  audio::service(now);

#if DEBUG_LOG
  if (now - lastLog >= 2000) {
    lastLog = now;
    LOG("d=%.0fcm echoes=%lu timeouts=%lu pulse=%luus present=%d ir=%d mode=%d | pins: echo=%d irpin=%d\n",
        us.distanceCm(), (unsigned long)us.echoes(), (unsigned long)us.timeouts(),
        (unsigned long)us.lastPulseUs(), present, ir.present(), (int)mode,
        digitalRead(PIN_US_ECHO), digitalRead(PIN_IR));
  }
#endif
}

void setup() {
#if DEBUG_LOG
  Serial.begin(115200);
#endif
#if USE_PERIPH_SWITCH
  pinMode(PIN_PERIPH_EN, OUTPUT);
  digitalWrite(PIN_PERIPH_EN, HIGH);
  delay(150);
#endif
  // Keep the onboard RGB LED's data line quiet; nothing is wired to GPIO18 now.
  pinMode(PIN_UNUSED_RGB, OUTPUT);
  digitalWrite(PIN_UNUSED_RGB, LOW);
  audio::begin();
  eyes.begin();
  us.begin();
  ir.begin();
  bat.begin();

  uint32_t now = millis();
  lastActivity = now;
  murmurNext = now + rnd(STARTUP_MURMUR_MIN_MS, STARTUP_MURMUR_MAX_MS);
  rescheduleRare();
  LOG("whitlock up\n");
}

void loop() {
  uint32_t now = millis();
  us.update(now);
  audio::pump();
  if (now - lastTick >= 20) {  // 50 Hz
    lastTick = now;
    tick(now);
  }
  delay(1);
}
