# Whitlock séance display

One ESP32-S2-DevKitM-1 (PlatformIO, Arduino). Pins are the GPIO numbers printed on its headers. All tunables are in `src/config.h`.

## Wiring

One speaker, inside the mask. Murmur and Whitlock's lines are mixed in firmware onto
one DAC. See `docs/schematic.png` for the full drawing.

```
J1 (3V3/5V side)   GPIO17 DAC1 -> 100nF -> PAM8302 IN+ (IN- to GND)
                   GPIO11 -> PAM8302 SD (direct)
                   GPIO4  WS2812 data via 2x110R (= 220R)
J3 (RST side)      GPIO39 HC-SR04 TRIG
                   GPIO40 HC-SR04 ECHO via the 2.2k divider (1 on top, 2 in series below)
                   GPIO36 FC-51 OUT (active-low); FC-51 powered from the 3V3 pin on J1
GPIO18             onboard RGB LED data: unused, held low by the firmware
```

- Power: USB-PD board (CH224K, all 3 switches OFF = 5V) -> master switch -> +5V rail.
  Flash with the master switch OFF and USB from the computer only; never both at once.
- Caps: 470uF across the strip's 5V/GND; 100uF + 100nF across the PAM's VIN/GND.
- PAM module pot is the volume; start fully down.
- Do not ground either transducer lead.
- Power the FC-51 from 3V3 (if it needs 5V, see the notes on the schematic).

## Audio

The **murmur is synthesised** on the chip (`src/murmur.cpp`): a crowd of whispering voices
with wandering vowel-like filters, syllable rhythm, breaths and pauses. It never repeats and
uses no flash. Tune it in `src/config.h` (`MURMUR_VOICES`) and in the
constants at the top of `murmur.cpp`. To hear it on a PC first:

```
g++ -O2 -std=c++17 -Isrc tools/murmur_preview.cpp src/murmur.cpp -o /tmp/murmur_preview
/tmp/murmur_preview docs/murmur_preview.wav 40 6      # file, seconds, voices
```

`docs/murmur_preview.wav` is a 40 s render with 6 voices.

Whitlock's spoken lines are recorded clips, in two pools (one random clip is picked each time,
never the same one twice running):

```
audio/notthat/*.wav    book pool: "Not that one." / "No... not that one." / "That's not the one." / "That isn't mine."
audio/reaction/*.wav   reaction pool: wait, stay, dont_go, please, r_breath, r_sigh ...
python3 tools/wav2c.py          # writes src/audio_data.cpp (8 kHz, 8-bit, mono)
```

`src/audio_data.cpp` currently holds synthesised placeholders (beeps and breaths). Run the script
with your real files, then rebuild. It stops if the total exceeds 3.5 MB, which is about 7 minutes of
audio; a full 3.5 MB leaves about 180 KB spare in the app slot (checked).

## Behaviour

The ultrasonic median distance sets one of four tiers (with 15 cm of hysteresis and a 0.5 s dwell
before switching; entering NEAR stays instant):

| Tier | Distance | Murmur | Eyes |
|---|---|---|---|
| IDLE | beyond 100 cm | normal stretches and gaps | candle flicker |
| FAR "noticed" | 50-100 cm | unchanged | slowly brighten to 1.5x idle, still flickering |
| MID "listening" | 20-50 cm | ducks to 30% | hold steady at the noticed level (no flicker) |
| NEAR | under 20 cm | cut dead | go dark (`EYE_NEAR_FADE_MS`, 1 = instant) |

Moving outward reverses smoothly; the eyes never jump from dark to bright. A **leave** is someone
who spent 3+ s in MID/NEAR and is then beyond 200 cm for 1.5 s: the eyes flare, and a random clip from
the reaction pool plays (20 s cooldown). A book on the IR plays a random "not that one" clip, hushes the
murmur and flashes the eyes.

**Busy mode** (so it isn't dead at lunch): someone in MID/NEAR for 2+ min, then every 40-90 s either a
slow eye glint up to half idle brightness (only when the eyes are dark) or a quiet (60%) reaction clip,
sharing the 20 s cooldown. It ends once nobody has been in MID/NEAR for 60 s.

The tier / leave / busy logic is in `src/presence.h`, tested on a PC:
`g++ -std=c++17 -Isrc tools/presence_test.cpp -o /tmp/presence_test && /tmp/presence_test`.

## Show days

`FRIDAY_MODE` at the top of `src/config.h` picks the look:

| `FRIDAY_MODE` | Days | Murmur | Eyes |
|---|---|---|---|
| 0 | Mon-Thu | 4 whisperers | amber candle (R:G:B = 255:90:15) |
| 1 | Friday | 6 whisperers | redder candle (255:40:5) |

Flash with `FRIDAY_MODE 1` on Friday morning. On Friday the tiers are ignored: from
`FRIDAY_AGITATE_TIER` inward (FAR by default) the eyes flare brighter and the murmur gets louder with
extra voices, instead of going quiet. The eyes are a random candle flicker (different for
each eye, with occasional deeper dips that never go below `CANDLE_FLOOR`), updated 200 times a
second with our own temporal dithering so the dim levels have fine steps. `EYE_MAX_PWM` sets
overall brightness; with a tissue/paper diffuser over the strip you can run it higher.

## First checks on hardware

1. Set `DEBUG_LOG 1`, open the serial monitor with nobody near, and check the
   `d=` distance and `tier=`. It should read 999 / tier 0. If the table or a wall
   reads under 100cm, re-aim the sensor.
2. Walk up to it and watch `tier=` go 1 (FAR), 2 (MID), 3 (NEAR) at 100 / 50 / 20 cm.
3. Check the IR with the real books and turn its trim pot down to about 8–10cm.
4. Leave the battery cutoff off (there is no battery sense wired).
5. Set the trimpot volumes, then set `DEBUG_LOG 0` for the install.
