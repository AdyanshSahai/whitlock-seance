#pragma once
#include <Arduino.h>

// ======================= pins (ESP32-S2-DevKitM-1 GPIO) ======================
// Header J1 (3V3/5V side): GPIO4 LED strip data, GPIO11 amp SD, GPIO17 DAC (speaker).
// Header J3 (RST side):   GPIO39 trig, GPIO40 echo, GPIO36 IR  (the two sensors together).
// GPIO18 drives the board's onboard RGB LED and is not used for audio any more.
constexpr uint8_t PIN_DAC           = 17;  // DAC1 -> 100nF -> PAM8302 IN+ (one speaker, inside the mask)
constexpr uint8_t PIN_AMP_SD        = 11;  // PAM8302 SD
constexpr uint8_t PIN_LED_DATA      = 4;   // WS2812 data via 220R (J1, 3V3/5V side)
constexpr uint8_t PIN_UNUSED_RGB    = 18;  // onboard RGB LED data (DAC2): held low, not used
constexpr uint8_t PIN_US_TRIG       = 39;  // HC-SR04 trig (J3, RST side; MTCK/JTAG pin, plain GPIO here)
constexpr uint8_t PIN_US_ECHO       = 40;  // HC-SR04 echo (J3, RST side; MTDO/JTAG pin, plain GPIO here), through the divider
constexpr uint8_t PIN_IR            = 36;  // FC-51 OUT (J3, RST side, with the ultrasonic)
constexpr uint8_t PIN_BAT_SENSE     = 1;   // cell+ -> 100k -> pin -> 100k -> GND (+100nF at pin)
constexpr uint8_t PIN_PERIPH_EN     = 13;  // HIGH = switched 5V rail on (only if USE_PERIPH_SWITCH)

// ============================ which day ======================================
// 0 = Mon-Thu: amber candle eyes, 4 whisperers.   1 = Friday: redder eyes, 6 whisperers.
// Flash it with 1 on Friday morning.
#define FRIDAY_MODE 0

// ============================ hardware options ===============================
// TEST_MODE 1 squeezes every timer so each behaviour fires within seconds. Set 0 for the install.
#define TEST_MODE 1
#define TV(normal, test) ((TEST_MODE) ? (test) : (normal))
#define BATTERY_CUTOFF 0            // 1 only if you wired the battery divider to GPIO1. It is NOT wired on this build:
                                    // a floating pin reads as a dead cell and would shut the display down after ~15 s
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
// While Whitlock speaks, the murmur is turned down to this (0..256, 256 = no ducking).
constexpr int32_t DUCK_LEVEL = 90;
constexpr uint32_t AMP_OFF_DELAY_MS = 300;   // mute amp this long after a channel goes quiet

// ============================ lights =========================================
// Candle-glow eyes. Colours are given relative to R = 255. The strip is driven by our own float
// maths with temporal dithering (see eyes.cpp), so even the dimmest flicker has fine steps and
// the colour does not turn red or stepped at the bottom.
constexpr float EYE_MAX_PWM = 120.0f;        // red channel at level 1.0 (0-255). Lower = dimmer overall.
#if FRIDAY_MODE
constexpr float EYE_G = 40.0f, EYE_B = 5.0f;    // Friday: a redder, angrier candle
#else
constexpr float EYE_G = 90.0f, EYE_B = 15.0f;   // amber candle (R:G:B = 255:90:15)
#endif
constexpr float EYE_MIN_GREEN_PWM = 1.5f;    // green never drops below this while lit
constexpr float EYE_GAMMA = 1.8f;
// Candle flicker (perceived level 0..1). The deepest dip still stays above CANDLE_FLOOR.
constexpr float CANDLE_LO = 0.42f, CANDLE_HI = 0.72f, CANDLE_FLOOR = 0.30f;
constexpr float CANDLE_MID = 0.5f * (CANDLE_LO + CANDLE_HI);   // the level the eyes hold when steady
constexpr uint32_t EYE_UPDATE_MS = 5;        // 200 Hz updates: smooth flicker and fast dithering
constexpr uint32_t EYE_RISE_MS = 1800;                  // dark -> candle glow
constexpr uint32_t EYE_NEAR_FADE_MS = 1;                // NEAR: eyes to dark. 1 = instant (as requested); 2000 = a slow fade

// ============================ ultrasonic + distance tiers ====================
// Aim the sensor straight out from the table, away from the walkway. Tiers use the median distance:
//   IDLE  beyond FAR     nothing
//   FAR   50-100 cm      "noticed":  eyes slowly brighten to NOTICE_BRIGHTNESS x idle, still flickering
//   MID   20-50  cm      "listening": murmur ducks to LISTEN_MURMUR_LEVEL, eyes hold steady (no flicker)
//   NEAR  < 20   cm      murmur cuts, eyes go dark
constexpr float TIER_NEAR_CM = 20.0f, TIER_MID_CM = 50.0f, TIER_FAR_CM = 100.0f;  // enter (moving inward)
constexpr float TIER_HYST_CM = 15.0f;        // moving outward needs this much more distance
constexpr uint32_t TIER_DWELL_MS = 500;      // a candidate tier must hold this long before it is adopted
constexpr uint32_t NEAR_DWELL_MS = 0;        // ...except entering NEAR: the murmur cut / lights out stay instant
constexpr uint8_t  US_WINDOW = 5;        // median of last 5 pings
constexpr uint32_t US_PING_INTERVAL_MS = 60;
constexpr uint32_t US_TIMEOUT_MS = 35;
constexpr float US_NO_ECHO_CM = 999.0f;

// ============================ behaviour timing ===============================
// Noticed / listening looks and sounds
constexpr float    NOTICE_BRIGHTNESS = 1.5f;     // eyes at this multiple of idle brightness when someone is FAR/MID
constexpr uint32_t NOTICE_UP_MS = 1500, NOTICE_DOWN_MS = 1500;   // brighten / relax (reverses smoothly)
constexpr uint32_t STEADY_MS = 500;              // flicker fades out (MID) / back in over this
constexpr float    LISTEN_MURMUR_LEVEL = 0.30f;  // murmur volume in MID (fraction of normal)
constexpr float    GAIN_SMOOTH = 0.11f;          // per 20 ms tick: ~0.5 s to settle a murmur level change

// Friday: instead of going quiet when someone steps up, the mask gets agitated:
// eyes flare brighter, the murmur gets louder and more voices join. (Used when FRIDAY_MODE is 1.)
constexpr uint8_t  FRIDAY_EXTRA_VOICES = 2;     // extra whisperers while someone is near
constexpr float    FRIDAY_MURMUR_GAIN = 420.0f; // murmur gain while someone is near (256 = normal)
constexpr uint8_t  FRIDAY_AGITATE_TIER = 1;     // agitated from this tier inward: 1 = FAR (within 100 cm), 2 = MID, 3 = NEAR
constexpr float    AGITATE_LEVEL = 0.85f;       // eye brightness while someone is near (0..1)
constexpr uint32_t AGITATE_UP_MS = 450, AGITATE_DOWN_MS = 1600;

// A visit = time in MID/NEAR. It ends when the median is beyond LEAVE_CM for LEAVE_GONE_MS.
constexpr float    LEAVE_CM = 200.0f;
constexpr uint32_t LEAVE_GONE_MS = 1500;
constexpr uint32_t LEAVE_MIN_ENGAGED_MS = TV(3000, 1000);  // needs this long in MID/NEAR to count as a leave
constexpr uint32_t FLARE_HOLD_MS = 100, FLARE_DECAY_MS = 1500;
constexpr float    FLARE_PEAK = 0.9f;

constexpr uint32_t MURMUR_ON_MIN_MS = TV(20000, 6000),  MURMUR_ON_MAX_MS = TV(60000, 10000);
constexpr uint32_t MURMUR_OFF_MIN_MS = TV(10000, 3000), MURMUR_OFF_MAX_MS = TV(40000, 6000);
#if FRIDAY_MODE
constexpr uint8_t  MURMUR_VOICES = 6;          // whisperers in the idle murmur (1-8)
#else
constexpr uint8_t  MURMUR_VOICES = 4;
#endif
constexpr uint32_t MURMUR_FADE_IN_MS = 800, MURMUR_FADE_OUT_MS = 1500;
constexpr uint32_t MURMUR_CUT_MS = 3;           // "cuts dead": 3 ms ramp, instant to the ear but not a click
constexpr uint32_t MURMUR_HUSH_MS = 120;        // the voices fall silent when Whitlock speaks

constexpr uint32_t STARTUP_MURMUR_MIN_MS = TV(5000, 1000), STARTUP_MURMUR_MAX_MS = TV(15000, 3000);
// Reaction pool (audio/reaction): one random clip when someone leaves; the eye flare happens either way.
constexpr uint32_t REACT_DELAY_MS = 700;                    // after the leave is confirmed
constexpr uint32_t REACT_COOLDOWN_MS = TV(20000, 5000);     // shared by leave reactions and busy-mode sounds

// Busy mode, so the display doesn't sit dead at lunch: someone in MID/NEAR for 2+ minutes, then every
// 40-90 s either a slow eye flicker up to ~half idle brightness (when the eyes are dark, i.e. NEAR) or a
// quiet reaction from the pool. Ends once nobody has been in MID/NEAR for 60 s.
constexpr uint32_t BUSY_AFTER_MS = TV(120000, 15000);
constexpr uint32_t BUSY_GAP_MS = 3000;                      // a step out shorter than this doesn't restart the 2 min
constexpr uint32_t BUSY_EVERY_MIN_MS = TV(40000, 4000), BUSY_EVERY_MAX_MS = TV(90000, 8000);
constexpr uint32_t BUSY_CLEAR_MS = TV(60000, 10000);
constexpr uint16_t BUSY_SOUND_GAIN = 154;                   // 60% of 256
constexpr float    BUSY_EYE_PEAK = 0.39f;                   // perceived level = half of idle brightness
constexpr uint32_t BUSY_EYE_UP_MS = 1500, BUSY_EYE_DOWN_MS = 1500;

constexpr uint32_t IR_DEBOUNCE_MS = 150;
constexpr uint32_t IR_COOLDOWN_MS = TV(20000, 3000);
constexpr uint32_t BOOK_HOLD_MS = 1200, BOOK_DECAY_MS = 1800;

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
