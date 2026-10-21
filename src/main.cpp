// Whitlock seance display. See README.md for wiring and tuning.
#include <Arduino.h>
#include <driver/gpio.h>
#include <esp_sleep.h>
#include "audio.h"
#include "config.h"
#include "eyes.h"
#include "presence.h"
#include "sensors.h"

static Ultrasonic us;
static IrSensor ir;
static Battery bat;
static Eyes eyes;
static Presence presence(PresenceParams{TIER_NEAR_CM, TIER_MID_CM, TIER_FAR_CM, TIER_HYST_CM, LEAVE_CM,
                                        TIER_DWELL_MS, NEAR_DWELL_MS, LEAVE_GONE_MS, LEAVE_MIN_ENGAGED_MS,
                                        BUSY_AFTER_MS, BUSY_GAP_MS, BUSY_CLEAR_MS});
static Tier tier = TIER_IDLE;

// murmur scheduler
static bool murmurOn = false;
static uint32_t murmurNext = 0, murmurEnd = 0;

// pools: book lines (CLIPS_NOTTHAT) and reactions (CLIPS_REACT)
static int8_t lastNotThat = -1, lastReact = -1;
static uint32_t reactPendingAt = 0, reactCooldownUntil = 0;
static uint32_t irCooldownUntil = 0;
static uint32_t busyNext = 0;

static uint32_t lastTick = 0, lastLog = 0, lastEyes = 0;
static bool eyesBlocked = false;
static float murmurGain = 256.0f;

static uint32_t rnd(uint32_t lo, uint32_t hi) { return lo + (uint32_t)random(hi - lo + 1); }

static const Clip* pick(const Clip* arr, uint8_t n, int8_t& last) {
  if (n == 0) return nullptr;
  int8_t i = random(n);
  if (n > 1 && i == last) i = (i + 1) % n;  // never the same clip twice running
  last = i;
  return &arr[i];
}

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

// Tier changed. Looks (noticed / listening / dark) and the murmur level follow `tier` in tick();
// this handles the one-off bits. Friday ignores the tiers and gets agitated instead (see tick).
static void onTier(Tier from, Tier to, uint32_t now) {
  LOG("tier %d -> %d\n", (int)from, (int)to);
  if (FRIDAY_MODE) return;
  if (to == TIER_NEAR) {  // the old "approach": the whispers stop dead and the lights go out
    audio::stop(audio::CH_MURMUR, MURMUR_CUT_MS);
    murmurOn = false;
    murmurNext = now + rnd(MURMUR_OFF_MIN_MS, MURMUR_OFF_MAX_MS);
    eyes.cancelOverlays();
  }
}

// Plays one random clip from the reaction pool if the shared cooldown allows. Returns true if it played.
static bool playReaction(uint32_t now, uint16_t gain) {
  if (N_REACT == 0 || now < reactCooldownUntil || audio::active(audio::CH_LINES)) return false;
  const Clip* c = pick(CLIPS_REACT, N_REACT, lastReact);
  if (!c) return false;
  audio::play(audio::CH_LINES, *c, false, 30, 0, gain);
  reactCooldownUntil = now + REACT_COOLDOWN_MS;
  return true;
}

static void onLeave(uint32_t now) {
  LOG("leave\n");
  eyes.flashBoth(FLARE_PEAK, 60, FLARE_HOLD_MS, FLARE_DECAY_MS);  // always, whether or not a sound follows
  reactPendingAt = now + REACT_DELAY_MS;
}

static void onBook(uint32_t now) {
  audio::stop(audio::CH_MURMUR, MURMUR_HUSH_MS);  // the voices stop speaking for Whitlock
  murmurOn = false;
  murmurNext = now + rnd(MURMUR_OFF_MIN_MS, MURMUR_OFF_MAX_MS) + 3000;
  irCooldownUntil = now + IR_COOLDOWN_MS;
  reactPendingAt = 0;
  eyes.flashBoth(1.0f, 0, BOOK_HOLD_MS, BOOK_DECAY_MS);
  const Clip* c = pick(CLIPS_NOTTHAT, N_NOTTHAT, lastNotThat);
  if (c) audio::play(audio::CH_LINES, *c, false, 30);
  LOG("book\n");
}

// Busy mode (someone has stood close for 2+ min): keep the display from sitting dead.
static void busyEvent(uint32_t now) {
  bool dark = !FRIDAY_MODE && tier == TIER_NEAR;       // only when the eyes are dark is a glint visible
  bool glint = dark && random(2) == 0;
  if (!glint) {
    if (playReaction(now, BUSY_SOUND_GAIN)) { LOG("busy: sound\n"); return; }
    if (!dark) return;                                  // sound blocked by the cooldown: skip this round
  }
  eyes.flashBoth(BUSY_EYE_PEAK, BUSY_EYE_UP_MS, 0, BUSY_EYE_DOWN_MS, true);
  LOG("busy: glint\n");
}

// -------------------------------------------------------------- per-tick work

static void updatePresence(uint32_t now) {
  Presence::Events ev = presence.update(now, us.distanceCm());
  tier = presence.tier();
  if (ev.tierChanged) onTier(ev.from, tier, now);
  if (ev.left) onLeave(now);
  if (ev.busyStarted) {
    busyNext = now + rnd(BUSY_EVERY_MIN_MS, BUSY_EVERY_MAX_MS);
    LOG("busy mode on\n");
  }
  if (ev.busyEnded) LOG("busy mode off\n");
  if (presence.busy() && now >= busyNext) {
    busyEvent(now);
    busyNext = now + rnd(BUSY_EVERY_MIN_MS, BUSY_EVERY_MAX_MS);
  }
}

static void handleIr(uint32_t now) {
  ir.update(now);
  if (ir.rose() && now >= irCooldownUntil) onBook(now);
}

static bool agitatedNow() { return FRIDAY_MODE && tier >= (Tier)FRIDAY_AGITATE_TIER; }

static void murmurScheduler(uint32_t now) {
  if (!FRIDAY_MODE && tier == TIER_NEAR) return;  // NEAR: dead silence
  bool agit = agitatedNow();
  if (agit && murmurOn) audio::setMurmurVoices(MURMUR_VOICES + FRIDAY_EXTRA_VOICES);
  if (!murmurOn && (now >= murmurNext || agit)) {  // Friday: start at once if someone steps up in a gap
    audio::setMurmurVoices(MURMUR_VOICES + (agit ? FRIDAY_EXTRA_VOICES : 0));
    audio::playMurmur(MURMUR_FADE_IN_MS);
    murmurOn = true;
    murmurEnd = now + rnd(MURMUR_ON_MIN_MS, MURMUR_ON_MAX_MS);
  } else if (murmurOn && now >= murmurEnd && !agit) {
    audio::stop(audio::CH_MURMUR, MURMUR_FADE_OUT_MS);
    murmurOn = false;
    murmurNext = now + rnd(MURMUR_OFF_MIN_MS, MURMUR_OFF_MAX_MS);
  }
}

static void reactionScheduler(uint32_t now) {
  if (!reactPendingAt || now < reactPendingAt) return;
  reactPendingAt = 0;
  if (tier >= TIER_MID) return;  // someone came straight back: no goodbye
  if (playReaction(now, 0)) LOG("reaction\n");
}

static void tick(uint32_t now) {
  bat.update(now);
#if BATTERY_CUTOFF
  if (bat.low()) goDead("battery");
#endif

  updatePresence(now);
  handleIr(now);
  murmurScheduler(now);
  reactionScheduler(now);

  // looks and murmur level follow the tier; everything glides so nothing steps
  bool agit = agitatedNow();
  eyes.setAgitated(agit);
  eyes.setTier(FRIDAY_MODE ? TIER_IDLE : tier);
  eyesBlocked = !FRIDAY_MODE && tier == TIER_NEAR;
  float gainTarget = 256.0f;
  if (FRIDAY_MODE) {
    if (agit) gainTarget = FRIDAY_MURMUR_GAIN;
  } else if (tier == TIER_MID) {
    gainTarget = 256.0f * LISTEN_MURMUR_LEVEL;       // "listening": the whispers duck
  }
  murmurGain += (gainTarget - murmurGain) * GAIN_SMOOTH;
  audio::setMurmurGain((uint16_t)murmurGain);
  audio::service(now);

#if DEBUG_LOG
  if (now - lastLog >= 2000) {
    lastLog = now;
    LOG("d=%.0fcm tier=%d busy=%d echoes=%lu timeouts=%lu ir=%d | pins: echo=%d irpin=%d\n",
        us.distanceCm(), (int)tier, presence.busy(), (unsigned long)us.echoes(), (unsigned long)us.timeouts(),
        ir.present(), digitalRead(PIN_US_ECHO), digitalRead(PIN_IR));
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
  murmurNext = now + rnd(STARTUP_MURMUR_MIN_MS, STARTUP_MURMUR_MAX_MS);
  LOG("whitlock up\n");
}

void loop() {
  uint32_t now = millis();
  us.update(now);
  audio::pump();
  if (now - lastEyes >= EYE_UPDATE_MS) {  // eyes update at 200 Hz for smooth flicker and dithering
    lastEyes = now;
    eyes.update(now, eyesBlocked, EYE_NEAR_FADE_MS);
  }
  if (now - lastTick >= 20) {  // 50 Hz
    lastTick = now;
    tick(now);
  }
  delay(1);
}
