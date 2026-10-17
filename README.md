# Whitlock séance display

One ESP32-S2-DevKitM-1 (PlatformIO, Arduino). Pins are the GPIO numbers printed on its headers. All tunables are in `src/config.h`.

## Wiring

```
GPIO17 DAC1 -> 100nF -> 10k trimpot -> PAM8302 #1 IN+  (IN- to GND)   murmur, under table
GPIO18 DAC2 -> 100nF -> 10k trimpot -> PAM8302 #2 IN+  (IN- to GND)   Whitlock's lines, hollow book
GPIO11 / GPIO12   PAM #1 / #2 SD (config: USE_AMP_SD, AMP_ON_LEVEL)
GPIO4  WS2812 data via 330R (+10k to GND so it can't float in deep sleep)
GPIO5  HC-SR04 TRIG        GPIO6  ECHO via 1k (top) / 2k (bottom) divider
GPIO8  FC-51 OUT (active-low)
GPIO1  cell+ -> 100k -> pin -> 100k -> GND, 100nF pin to GND
GPIO13 reserved for a switched 5V rail (USE_PERIPH_SWITCH, off: cells are protected)
```

- **Remove the onboard RGB LED** (the little square marked RGB@IO18, next to the LDO). It is wired
  to GPIO18, which is DAC2, so the audio would drive it and it would flicker and draw current. Snip it
  off with side cutters or desolder it. Tape only hides it.
- J5019 5V goes to the board's 5V pin through a Schottky so USB can't push into the boost. The board's
  docs say USB and the 5V pin are mutually exclusive power sources, so **unplug USB (or switch the
  battery off) when the other is connected**. For flashing, use USB only.
- Put 100µF + 100nF at each PAM's VDD and across the strip.
- A 1N400x in series with the strip's 5V feed makes the 3.3V data line reliable.
- Power the FC-51 from 3V3 (or divide its output if on 5V).

## Audio

```
audio/murmur/*.wav   idle loops (tens of seconds)
audio/notthat/*.wav  "not that one" + variants
audio/wait/*.wav     "wait"
python3 tools/wav2c.py          # writes src/audio_data.cpp (8 kHz, 8-bit, mono)
```

`src/audio_data.cpp` currently holds synthesised placeholders. Run the script with
your real files, then rebuild. It stops if the total exceeds ~3.5 MB.

## First checks on hardware

1. Set `DEBUG_LOG 1`, open the serial monitor with nobody near, and check the
   `d=` distance. It should read 999 or well over 110cm. If the table or a wall
   reads under 80cm, re-aim the sensor.
2. Walk up to it and check the thresholds (80cm present / 110cm gone).
3. Check the IR with the real books and turn the trimpot down to about 8–10cm.
4. Compare the `bat=` reading to a multimeter and adjust `BAT_CAL`.
5. Set the trimpot volumes, then set `DEBUG_LOG 0` for the install.
