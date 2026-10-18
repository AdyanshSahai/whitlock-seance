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
                   GPIO35 FC-51 OUT (active-low); FC-51 powered from the 3V3 pin on J1
GPIO18             onboard RGB LED data: unused, held low by the firmware
```

- Power: USB-PD board (CH224K, all 3 switches OFF = 5V) -> master switch -> +5V rail.
  Flash with the master switch OFF and USB from the computer only; never both at once.
- Caps: 470uF across the strip's 5V/GND; 100uF + 100nF across the PAM's VIN/GND.
- PAM module pot is the volume; start fully down.
- Do not ground either transducer lead.
- Power the FC-51 from 3V3 (if it needs 5V, see the notes on the schematic).

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
3. Check the IR with the real books and turn its trim pot down to about 8–10cm.
4. Leave the battery cutoff off (there is no battery sense wired).
5. Set the trimpot volumes, then set `DEBUG_LOG 0` for the install.
