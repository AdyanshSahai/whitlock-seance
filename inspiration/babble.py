#!/usr/bin/env python3
"""
wordless séance murmur generator.
runs on your PC, not the board. outputs an 8 kHz, 8-bit mono wav ready for the
wav -> C array converter.

how it fakes speech without words:
  - source: glottal pulse train (voiced) mixed with noise (whisper)
  - 3 formant resonators gliding between random vowel targets -> "vowels"
  - short noise bursts between vowels -> "consonants"
  - syllables at ~4-5 per second, grouped into phrases with pauses
  - pitch falls across each phrase (declination), like real sentences
  - several voices layered, each with its own pitch, speed and whisper amount
  - low-pass + small room reverb so it sounds like it's inside something

usage: python3 babble.py out.wav [seconds] [seed] [voices]
"""
import sys
import numpy as np
from scipy.signal import lfilter, butter

SR = 8000
FRAME = 40  # 5 ms control frames

# rough (F1, F2, F3) in Hz for adult male vowels, plus a couple of reduced ones
VOWELS = [
    (730, 1090, 2440),  # a  (father)
    (530, 1840, 2480),  # e  (bed)
    (270, 2290, 3010),  # i  (see)
    (570, 840, 2410),   # o  (law)
    (300, 870, 2240),   # u  (boot)
    (660, 1720, 2410),  # ae (cat)
    (500, 1500, 2500),  # schwa, used a lot like real speech
    (500, 1500, 2500),
    (440, 1020, 2240),  # ʊ  (book)
]
BW = (90, 110, 170)  # formant bandwidths


def resonator_coeffs(f, bw):
    r = np.exp(-np.pi * bw / SR)
    th = 2 * np.pi * f / SR
    a1, a2 = -2 * r * np.cos(th), r * r
    g = 1 + a1 + a2  # unity gain at DC-ish, keeps levels sane
    return [g], [1, a1, a2]


def make_voice(seconds, rng, f0_base, whisper, rate):
    n = int(seconds * SR)
    nfr = n // FRAME
    f0 = np.zeros(nfr)
    amp = np.zeros(nfr)
    fmt = np.zeros((nfr, 3))
    cons = np.zeros(nfr)  # consonant noise level
    cons_hi = np.zeros(nfr, dtype=bool)

    t = int(rng.uniform(0, 1.5) * SR / FRAME)  # staggered start
    prev = np.array(VOWELS[6], float)
    while t < nfr:
        nsyl = rng.integers(2, 9)
        phrase_start_f0 = f0_base * rng.uniform(1.05, 1.2)
        for s in range(nsyl):
            syl = int(rng.uniform(0.14, 0.30) / rate * SR / FRAME)
            if t + syl >= nfr:
                break
            stress = rng.uniform(0.55, 1.0) if s % 2 else rng.uniform(0.8, 1.0)
            # pitch declines across the phrase, small bump on stressed syllables
            p = phrase_start_f0 * (1 - 0.25 * s / max(nsyl - 1, 1))
            p *= 1 + 0.06 * (stress - 0.75)
            target = np.array(VOWELS[rng.integers(len(VOWELS))], float)
            target *= rng.uniform(0.92, 1.08, 3)
            # consonant onset
            con_len = int(rng.uniform(0.02, 0.07) * SR / FRAME) if rng.random() < 0.7 else 0
            hi = rng.random() < 0.35  # s/sh/f-like vs stop/h-like
            for k in range(syl):
                i = t + k
                x = k / syl
                env = np.sin(np.pi * min(1, x * 1.15)) ** 0.7 * stress
                g = min(1, k / max(syl * 0.35, 1))  # formant glide
                fmt[i] = prev + (target - prev) * g
                f0[i] = p * (1 - 0.05 * x)
                amp[i] = env if k >= con_len else 0.15 * stress
                if k < con_len:
                    cons[i] = 0.6 * stress
                    cons_hi[i] = hi
            prev = target
            t += syl
        # pause between phrases, sometimes long
        t += int(rng.choice([rng.uniform(0.25, 0.7), rng.uniform(0.8, 2.5)],
                            p=[0.6, 0.4]) * SR / FRAME)

    # --- sample-rate source ---
    f0s = np.repeat(f0, FRAME)[:n]
    ampS = np.repeat(amp, FRAME)[:n]
    jitter = 1 + 0.02 * lfilter([0.02], [1, -0.98], rng.standard_normal(n))
    f0s = np.where(f0s > 0, f0s * jitter, f0_base)
    phase = np.cumsum(f0s / SR) % 1.0
    # simple glottal pulse: sharp closure each period, then lowpass via formants
    pulse = np.zeros(n)
    edges = np.where(np.diff(phase) < 0)[0]
    pulse[edges] = 1.0
    pulse = lfilter([1], [1, -0.9], pulse)  # glottal tilt
    pulse -= pulse.mean()
    pulse /= np.abs(pulse).max() + 1e-9
    noise = rng.standard_normal(n) * 0.35
    src = ((1 - whisper) * pulse + whisper * noise) * ampS

    # --- time-varying formant filter, frame by frame ---
    out = np.zeros(n)
    zis = [np.zeros(2) for _ in range(3)]
    for fi in range(nfr):
        a, b = fi * FRAME, (fi + 1) * FRAME
        seg = src[a:b]
        if not seg.any() and not zis[0].any():
            continue
        y = np.zeros_like(seg)
        for j in range(3):
            bb, aa = resonator_coeffs(fmt[fi, j] or VOWELS[6][j], BW[j])
            yj, zis[j] = lfilter(bb, aa, seg, zi=zis[j])
            y += yj * (1.0, 0.5, 0.25)[j]
        out[a:b] = y

    # --- consonants: filtered noise bursts ---
    consS = np.repeat(cons, FRAME)[:n]
    hiS = np.repeat(cons_hi, FRAME)[:n]
    bh, ah = butter(2, [2200 / (SR / 2), 3800 / (SR / 2)], "band")
    bl, al = butter(2, [700 / (SR / 2), 2500 / (SR / 2)], "band")
    cn = rng.standard_normal(n)
    hiss = lfilter(bh, ah, cn)
    thud = lfilter(bl, al, cn)
    out = out / (np.abs(out).max() + 1e-9)
    out += consS * np.where(hiS, hiss * 0.5, thud * 0.35)
    return out


def room(x, wet=0.25):
    # tiny feedback-comb reverb, like being inside a hollow mask / small room
    y = x.copy()
    for d_ms, g in ((23, 0.45), (31, 0.4), (41, 0.35), (53, 0.3)):
        d = int(SR * d_ms / 1000)
        y += wet * lfilter([0] * d + [1], [1] + [0] * (d - 1) + [-g], x)
    return y


def main():
    out_path = sys.argv[1] if len(sys.argv) > 1 else "murmur.wav"
    secs = float(sys.argv[2]) if len(sys.argv) > 2 else 40
    seed = int(sys.argv[3]) if len(sys.argv) > 3 else 7
    nvoices = int(sys.argv[4]) if len(sys.argv) > 4 else 6
    rng = np.random.default_rng(seed)

    mix = np.zeros(int(secs * SR))
    for v in range(nvoices):
        f0 = rng.uniform(85, 150)          # mixed voices, mostly low
        whisper = rng.uniform(0.55, 0.95)  # mostly whispered: hides synth artefacts
        rate = rng.uniform(0.85, 1.15)
        gain = rng.uniform(0.4, 1.0)
        mix += gain * make_voice(secs, rng, f0, whisper, rate)

    b, a = butter(4, 2600 / (SR / 2))       # bone conduction can't do highs anyway
    mix = lfilter(b, a, mix)
    b, a = butter(2, 120 / (SR / 2), "high")  # no rumble / DC
    mix = lfilter(b, a, mix)
    mix = room(mix)
    # slow swell so it breathes as a crowd
    tt = np.arange(len(mix)) / SR
    mix *= 0.75 + 0.25 * np.sin(2 * np.pi * tt / rng.uniform(9, 15))
    # fade in/out for clean looping
    f = int(0.5 * SR)
    mix[:f] *= np.linspace(0, 1, f)
    mix[-f:] *= np.linspace(1, 0, f)
    mix = np.tanh(1.5 * mix / (np.percentile(np.abs(mix), 99.5) + 1e-9)) * 0.95
    u8 = np.clip(mix * 127 + 128, 0, 255).astype(np.uint8)

    import wave
    with wave.open(out_path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(1)
        w.setframerate(SR)
        w.writeframes(u8.tobytes())
    print(f"wrote {out_path}: {secs:.0f}s, {len(u8)/1024:.0f} KB as a C array")


if __name__ == "__main__":
    main()
