#pragma once
#include <Arduino.h>

// ======================= pins (ESP32-S2-DevKitM-1 GPIO) ======================
// GPIO18 (DAC2) also drives the board's onboard RGB LED: remove that LED (see README).
constexpr uint8_t PIN_DAC_MURMUR    = 17;  // DAC1 -> 100nF -> trimpot -> PAM8302 #1 (under table)
constexpr uint8_t PIN_DAC_LINES     = 18;  // DAC2 -> 100nF -> trimpot -> PAM8302 #2 (hollow book)
constexpr uint8_t PIN_AMP_SD_MURMUR = 11;  // PAM8302 #1 SD
constexpr uint8_t PIN_AMP_SD_LINES  = 12;  // PAM8302 #2 SD
constexpr uint8_t PIN_LED_DATA      = 4;   // WS2812 data via 330R
constexpr uint8_t PIN_US_TRIG       = 5;   // HC-SR04 trig
constexpr uint8_t PIN_US_ECHO       = 6;   // HC-SR04 echo, through 1k/2k divider
constexpr uint8_t PIN_IR            = 8;   // FC-51 OUT
constexpr uint8_t PIN_BAT_SENSE     = 1;   // cell+ -> 100k -> pin -> 100k -> GND (+100nF at pin)
constexpr uint8_t PIN_PERIPH_EN     = 13;  // HIGH = switched 5V rail on (only if USE_PERIPH_SWITCH)

// ============================ hardware options ===============================
// TEST_MODE 1 squeezes every timer so each behaviour fires within seconds. Set 0 for the install.
#define TEST_MODE 1
#define T(normal, test) ((TEST_MODE) ? (test) : (normal))
#define DEBUG_LOG 1                 // serial status every 2s; set 0 for the install
#define USE_PERIPH_SWITCH 0         // 1 if you built the P-FET switched 5V rail (bare cells)
#define USE_AMP_SD 1                // 0 if your PAM8302 clones have no usable SD pin
constexpr uint8_t AMP_ON_LEVEL = HIGH;   // SD level that enables the amp
#define IR_ACTIVE_LOW 1             // FC-51: OUT goes LOW when something is in front
#define LED_COUNT 8
constexpr uint8_t EYE_LEFT_FIRST  = 0;   // LEDs 0..3
constexpr uint8_t EYE_RIGHT_FIRST = 4;   // LEDs 4..7
constexpr uint8_t EYE_LEDS_PER_EYE = 4;

// ============================ audio ==========================================
constexpr uint32_t AUDIO_RATE_HZ = 8000;
// Software gain, 0..256. Keep 256 and set loudness with the trimpots (full 8 bits).
constexpr uint16_t GAIN_MURMUR = 256;
constexpr uint16_t GAIN_LINES  = 256;
constexpr uint32_t AMP_OFF_DELAY_MS = 300;   // mute amp this long after a channel goes quiet

// ============================ lights =========================================
constexpr uint8_t EYE_MAX_BRIGHTNESS = 51;   // FastLED global cap = 20% of 255
// pale sickly green. Warm white alternative: 255,170,70
constexpr uint8_t EYE_R = 120, EYE_G = 255, EYE_B = 100;
constexpr float EYE_GAMMA = 1.8f;
constexpr float BREATH_LO = 0.22f, BREATH_HI = 0.62f;   // perceived level 0..1
constexpr float BREATH_PERIOD_L_S = 6.8f, BREATH_PERIOD_R_S = 8.1f;
constexpr float BREATH_PHASE_R = 1.9f;                  // rad, keeps eyes out of sync
constexpr uint32_t EYE_RISE_MS = 1800;                  // dark -> breathing
constexpr uint32_t EYE_APPROACH_FADE_MS = 2000;         // breathing -> dark when someone steps up

// ============================ ultrasonic (presence) ==========================
// Aim it straight out from the table, away from the walkway.
constexpr float US_ENTER_CM = 80.0f;     // present when median < this
constexpr float US_EXIT_CM  = 110.0f;    // gone when median > this
constexpr uint8_t  US_WINDOW = 5;        // median of last 5 pings
constexpr uint32_t US_PING_INTERVAL_MS = 60;
constexpr uint32_t US_TIMEOUT_MS = 35;
constexpr float US_NO_ECHO_CM = 999.0f;

// ============================ behaviour timing ===============================
constexpr uint32_t LEAVE_GONE_MS = 1500;        // gone this long = they left
constexpr uint32_t LEAVE_MIN_PRESENT_MS = T(3000, 1000); // must have been there this long for flare/wait
constexpr uint32_t FLARE_HOLD_MS = 100, FLARE_DECAY_MS = 1500;
constexpr float    FLARE_PEAK = 0.9f;

constexpr uint32_t MURMUR_ON_MIN_MS = T(20000, 6000),  MURMUR_ON_MAX_MS = T(60000, 10000);
constexpr uint32_t MURMUR_OFF_MIN_MS = T(10000, 3000), MURMUR_OFF_MAX_MS = T(40000, 6000);
constexpr uint32_t MURMUR_FADE_IN_MS = 800, MURMUR_FADE_OUT_MS = 1500;
constexpr uint32_t MURMUR_CUT_MS = 25;          // "cuts dead" ramp, avoids a click

constexpr uint32_t STARTUP_MURMUR_MIN_MS = T(5000, 1000), STARTUP_MURMUR_MAX_MS = T(15000, 3000);
constexpr uint32_t WAIT_DELAY_MS = 700;         // after leave confirmed
constexpr uint8_t  WAIT_ODDS = T(3, 1);               // 1 in 3
constexpr uint32_t WAIT_COOLDOWN_MS = T(3UL * 60 * 1000, 6000);
constexpr uint32_t WAIT_COOLDOWN_BUSY_MS = T(5UL * 60 * 1000, 10000);
constexpr uint8_t  BUSY_LEAVES = 4;             // this many leaves within BUSY_WINDOW_MS = busy
constexpr uint32_t BUSY_WINDOW_MS = 10UL * 60 * 1000;

constexpr uint32_t IR_DEBOUNCE_MS = 150;
constexpr uint32_t IR_COOLDOWN_MS = T(20000, 3000);
constexpr uint32_t BOOK_HOLD_MS = 1200, BOOK_DECAY_MS = 1800;

// rare event: nobody near for a long time
constexpr uint32_t RARE_IDLE_MS = T(20UL * 60 * 1000, 20000);
constexpr uint32_t RARE_IDLE_JITTER_MS = T(10UL * 60 * 1000, 5000);
constexpr uint32_t RARE_MIN_SPACING_MS = T(3UL * 60 * 60 * 1000, 30000);
constexpr uint8_t  RARE_MAX_PER_BOOT = T(3, 250);
constexpr uint32_t RARE_SWELL_MS = T(10000, 4000), RARE_HOLD_MS = T(3000, 1500);
constexpr uint32_t RARE_SILENCE_MS = T(8000, 3000);      // dead dark silence afterwards


// ============================ battery ========================================
constexpr float BAT_DIVIDER = 2.0f;             // 100k/100k
constexpr float BAT_CAL = 1.00f;                // trim against a multimeter
constexpr uint16_t BAT_LOW_MV = 3300;
constexpr uint8_t  BAT_LOW_SECONDS = 15;        // consecutive seconds of smoothed reading below

#if DEBUG_LOG
#define LOG(...) Serial.printf(__VA_ARGS__)
#else
#define LOG(...) do {} while (0)
#endif
