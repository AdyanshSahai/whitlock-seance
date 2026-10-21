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
#define BATTERY_CUTOFF (!TEST_MODE)  // off on the USB bench: with no cell the sense pin floats
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
constexpr uint32_t EYE_UPDATE_MS = 5;        // 200 Hz updates: smooth flicker and fast dithering
constexpr uint32_t EYE_RISE_MS = 1800;                  // dark -> candle glow
constexpr uint32_t EYE_APPROACH_FADE_MS = 2000;         // candle -> dark when someone steps up

// ============================ ultrasonic (presence) ==========================
// Aim it straight out from the table, away from the walkway.
constexpr float US_ENTER_CM = 10.0f;     // present when median < this
constexpr float US_EXIT_CM  = 15.0f;     // gone when median > this
constexpr uint8_t  US_WINDOW = 5;        // median of last 5 pings
constexpr uint32_t US_PING_INTERVAL_MS = 60;
constexpr uint32_t US_TIMEOUT_MS = 35;
constexpr float US_NO_ECHO_CM = 999.0f;

// ============================ behaviour timing ===============================
constexpr uint32_t APPROACH_DELAY_MS = 2000;    // must stay in range this long before the mask reacts (0 = instant)
// Friday: instead of going quiet when someone steps up, the mask gets agitated:
// eyes flare brighter, the murmur gets louder and more voices join. (Used when FRIDAY_MODE is 1.)
constexpr uint8_t  FRIDAY_EXTRA_VOICES = 2;     // extra whisperers while someone is near
constexpr float    FRIDAY_MURMUR_GAIN = 420.0f; // murmur gain while someone is near (256 = normal)
constexpr float    AGITATE_LEVEL = 0.85f;       // eye brightness while someone is near (0..1)
constexpr uint32_t AGITATE_UP_MS = 450, AGITATE_DOWN_MS = 1600;

constexpr uint32_t LEAVE_GONE_MS = 1500;        // gone this long = they left
constexpr uint32_t LEAVE_MIN_PRESENT_MS = TV(3000, 1000); // must have been there this long for flare/wait
constexpr uint32_t FLARE_HOLD_MS = 100, FLARE_DECAY_MS = 1500;
constexpr float    FLARE_PEAK = 0.9f;

constexpr uint32_t MURMUR_ON_MIN_MS = TV(20000, 6000),  MURMUR_ON_MAX_MS = TV(60000, 10000);
constexpr uint32_t MURMUR_OFF_MIN_MS = TV(10000, 3000), MURMUR_OFF_MAX_MS = TV(40000, 6000);
#if FRIDAY_MODE
constexpr uint8_t  MURMUR_VOICES = 6;          // whisperers in the idle murmur (1-8)
#else
constexpr uint8_t  MURMUR_VOICES = 4;
#endif
constexpr uint8_t  RARE_EXTRA_VOICES = 3;       // extra voices that join during the rare swell
constexpr uint32_t MURMUR_FADE_IN_MS = 800, MURMUR_FADE_OUT_MS = 1500;
constexpr uint32_t MURMUR_CUT_MS = 3;           // "cuts dead": 3 ms ramp, instant to the ear but not a click
constexpr uint32_t MURMUR_HUSH_MS = 120;        // the voices fall silent when Whitlock speaks

constexpr uint32_t STARTUP_MURMUR_MIN_MS = TV(5000, 1000), STARTUP_MURMUR_MAX_MS = TV(15000, 3000);
constexpr uint32_t WAIT_DELAY_MS = 700;         // after leave confirmed
constexpr uint8_t  WAIT_ODDS = TV(3, 1);               // 1 in 3
constexpr uint32_t WAIT_COOLDOWN_MS = TV(3UL * 60 * 1000, 6000);
constexpr uint32_t WAIT_COOLDOWN_BUSY_MS = TV(5UL * 60 * 1000, 10000);
constexpr uint8_t  BUSY_LEAVES = 4;             // this many leaves within BUSY_WINDOW_MS = busy
constexpr uint32_t BUSY_WINDOW_MS = 10UL * 60 * 1000;

constexpr uint32_t IR_DEBOUNCE_MS = 150;
constexpr uint32_t IR_COOLDOWN_MS = TV(20000, 3000);
constexpr uint32_t BOOK_HOLD_MS = 1200, BOOK_DECAY_MS = 1800;

// rare event: nobody near for a long time
constexpr uint32_t RARE_IDLE_MS = TV(20UL * 60 * 1000, 20000);
constexpr uint32_t RARE_IDLE_JITTER_MS = TV(10UL * 60 * 1000, 5000);
constexpr uint32_t RARE_MIN_SPACING_MS = TV(3UL * 60 * 60 * 1000, 30000);
constexpr uint8_t  RARE_MAX_PER_BOOT = TV(3, 250);
constexpr uint32_t RARE_SWELL_MS = TV(10000, 4000), RARE_HOLD_MS = TV(3000, 1500);
constexpr uint32_t RARE_SILENCE_MS = TV(8000, 3000);      // dead dark silence afterwards


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
