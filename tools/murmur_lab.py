#!/usr/bin/env python3
"""Murmur lab: renders crowd-of-whisperers WAVs on the PC so you can compare ideas by ear.

    python3 tools/murmur_lab.py                       # writes the three comparison files
    python3 tools/murmur_lab.py out.wav 40 7 6 0.6    # file, seconds, seed, voices, pause scale

Voice model: a breath-noise / buzz mix pushed through three formant resonators that glide between
random vowels, with short hissy and thudding consonants, syllables grouped into phrases of 2-8
with a falling pitch, and a pause between phrases. Several voices are layered, each with its own
pitch, speed and breathiness, then low-passed and given a touch of reverb. Needs numpy.
"""
import math
import sys
import wave
from pathlib import Path

import numpy as np

FS = 8000


def smoothstep(x):
    x = np.clip(x, 0.0, 1.0)
    return x * x * (3 - 2 * x)


def resonate(x, track, r):
    """Two-pole resonator whose centre frequency follows `track` (Hz per sample)."""
    xl = x.tolist()
    n = len(xl)
    y = [0.0] * n
    y1 = y2 = 0.0
    c2 = -r * r
    g_base = 1.0 - r * r
    for b in range(0, n, 32):
        th = 2 * math.pi * float(track[b]) / FS
        c1 = 2 * r * math.cos(th)
        g = g_base * math.sin(th)
        for i in range(b, min(b + 32, n)):
            v = g * xl[i] + c1 * y1 + c2 * y2
            y2 = y1
            y1 = v
            y[i] = v
    return np.array(y)


def render_voice(rng, seconds, f0, tract, speed, whisper, pause_scale, long_pause_chance):
    n = int(seconds * FS)
    F = np.zeros((3, n))            # formant tracks
    env = np.zeros(n)               # vowel loudness
    f0t = np.full(n, f0, float)
    hiss_env = np.zeros(n)
    thud_env = np.zeros(n)

    prev = np.array([500.0, 1450.0, 2600.0]) * tract
    F[:] = prev[:, None]
    t = int(rng.uniform(0, 3.0) * FS)
    while t < n:
        count = int(rng.integers(2, 9))
        for k in range(count):
            dur = int(rng.uniform(0.11, 0.27) / speed * FS)
            if k == count - 1:
                dur = int(dur * 1.25)                      # phrase-final lengthening
            if rng.random() < 0.2:                         # a dark "mm"
                tgt = np.array([270.0, rng.uniform(900, 1300), 2400.0]) * tract
            else:
                tgt = np.array([rng.uniform(300, 850), rng.uniform(900, 2400), rng.uniform(2200, 3100)]) * tract
            end = min(n, t + dur)
            if t >= n:
                break
            m = end - t
            u = np.arange(m) / max(1, dur)
            g = smoothstep(u / 0.7)
            for j in range(3):
                F[j, t:end] = prev[j] + (tgt[j] - prev[j]) * g
            stress = rng.uniform(0.6, 1.15) * (1.15 if k == 0 else 1.0)
            env[t:end] = stress * np.sin(math.pi * np.power(u, 0.65)) ** 2
            q = (k + u) / count
            f0t[t:end] = f0 * (1.10 - 0.25 * q)             # pitch falls across the phrase
            if rng.random() < 0.5:                          # fricative burst
                hl = int(rng.uniform(0.02, 0.06) * FS)
                a, b = max(0, t - hl // 2), min(n, t + hl // 2)
                hiss_env[a:b] = np.hanning(b - a) * rng.uniform(0.4, 1.0)
            elif rng.random() < 0.3:                        # soft plosive thud
                tl = int(rng.uniform(0.04, 0.08) * FS)
                b = min(n, t + tl)
                thud_env[t:b] = np.exp(-np.arange(b - t) / (0.35 * tl)) * rng.uniform(0.4, 1.0)
            prev = tgt
            t = end
        gap = rng.uniform(0.25, 1.1) * pause_scale          # a sentence-sized breath
        if rng.random() < long_pause_chance:
            gap = rng.uniform(1.5, 3.5) * pause_scale
        hold = min(n, t + int(gap * FS))
        F[:, t:hold] = prev[:, None]
        t = hold

    # slow pitch wander and tiny jitter
    wander = np.cumsum(rng.normal(0, 0.00025, n))
    wander -= np.linspace(wander[0], wander[-1], n)
    f0t *= 1.0 + np.clip(wander, -0.05, 0.05) + rng.normal(0, 0.004, n)
    ph = np.cumsum(f0t / FS)
    buzz = 2 * (ph % 1.0) - 1.0
    buzz = np.diff(np.concatenate([[0.0], buzz * 0.0 + np.tanh(3 * np.sin(2 * math.pi * ph))]))  # rounded pulses
    buzz /= np.max(np.abs(buzz)) + 1e-9
    noise = rng.normal(0, 1, n)
    exc = whisper * noise * 0.5 + (1 - whisper) * buzz

    vow = resonate(exc, F[0], 0.955) + 0.8 * resonate(exc, F[1], 0.945) + 0.5 * resonate(exc, F[2], 0.935)
    vow *= env
    vow /= np.sqrt(np.mean(vow ** 2) + 1e-12) / 0.10

    hiss = np.diff(np.concatenate([[0.0], rng.normal(0, 1, n)])) * hiss_env * 0.05
    lowp = np.zeros(n)
    a = 0.0
    nz = rng.normal(0, 1, n)
    for i in range(n):                                       # one-pole lowpass for the thud
        a += 0.08 * (nz[i] - a)
        lowp[i] = a
    thud = lowp * thud_env * 0.35
    return vow + hiss + thud, env


def lowpass(x, cutoff=2600.0, taps=129):
    k = np.arange(taps) - (taps - 1) / 2
    h = np.sinc(2 * cutoff / FS * k) * np.hamming(taps)
    h /= h.sum()
    return np.convolve(x, h, mode="same")


def reverb(x, rng, wet=0.22, rt=0.35):
    m = int(rt * FS)
    ir = rng.normal(0, 1, m) * np.exp(-np.arange(m) / (0.28 * rt * FS))
    ir[0] = 0
    size = 1 << int(np.ceil(np.log2(len(x) + m)))
    y = np.fft.irfft(np.fft.rfft(x, size) * np.fft.rfft(ir, size), size)[: len(x)]
    y /= np.sqrt(np.mean(y ** 2) + 1e-12) / np.sqrt(np.mean(x ** 2) + 1e-12)
    return (1 - wet) * x + wet * y


def crowd(seconds, seed, voices, pause_scale=1.0, long_pause_chance=0.06, report=False):
    """Layer several speakers. To keep them audibly different people, each gets its own pitch,
    vocal-tract size (not locked to pitch), speed, breathiness, brightness, chattiness and a slow
    loudness swell, so the 'foreground' voice keeps changing while the others carry on under it."""
    rng = np.random.default_rng(seed)
    n = int(seconds * FS)
    if voices == 1:
        f0s, whispers = np.array([175.0]), np.array([0.65])
    else:
        f0s = np.linspace(105, 290, voices)
        whispers = np.linspace(0.35, 0.95, voices)         # from half-voiced to pure whisper
        rng.shuffle(f0s)
        rng.shuffle(whispers)
    mix = np.zeros(n)
    acts = []
    tt = np.arange(n) / FS
    for i in range(voices):
        f0 = float(f0s[i]) * rng.uniform(0.97, 1.03)
        tract = float(np.clip(0.80 + 0.55 * (f0 - 95) / 195 + rng.uniform(-0.10, 0.10), 0.78, 1.40))
        speed = rng.uniform(0.7, 1.5)
        chatty = rng.uniform(0.45, 1.3)                    # per-voice pause length
        v, env = render_voice(rng, seconds, f0, tract, speed, float(whispers[i]),
                              pause_scale * chatty, long_pause_chance)
        v = lowpass(v, rng.uniform(1800, 3300), 97)        # some voices bright, some dull
        period = rng.uniform(6.0, 15.0)
        swell = 0.30 + 0.70 * (0.5 + 0.5 * np.sin(2 * math.pi * tt / period + rng.uniform(0, 6.28))) ** 1.4
        mix += v * swell * rng.uniform(0.55, 1.0)
        acts.append((env > 0.08) & (swell > 0.45))
    mix = lowpass(mix)
    mix = reverb(mix, rng, wet=0.12, rt=0.25)
    if report and acts:
        count = np.sum(acts, axis=0)
        print(f"   speakers audible at once: mean {count.mean():.1f}; solo {np.mean(count == 1) * 100:.0f}%, "
              f"2+ overlapping {np.mean(count >= 2) * 100:.0f}%, silent {np.mean(count == 0) * 100:.0f}%")
    return mix / (np.max(np.abs(mix)) + 1e-9) * 0.8


def write_wav(path, x, rng):
    d = (rng.random(len(x)) - rng.random(len(x))) * 0.5 / 127.0   # tiny dither
    pcm = np.clip(np.round((x + d) * 127 + 128), 0, 255).astype(np.uint8)
    with wave.open(str(path), "wb") as w:
        w.setnchannels(1); w.setsampwidth(1); w.setframerate(FS)
        w.writeframes(pcm.tobytes())


def stats(name, x):
    rms = np.sqrt(np.mean(x ** 2))
    win = FS // 50
    lev = np.sqrt(np.mean(x[: len(x) // win * win].reshape(-1, win) ** 2, axis=1))
    quiet = np.mean(lev < 0.02) * 100
    sp = np.abs(np.fft.rfft(x)) ** 2
    fr = np.fft.rfftfreq(len(x), 1 / FS)
    tot = sp[(fr > 50)].sum()
    bands = [sp[(fr > a) & (fr <= b)].sum() / tot * 100 for a, b in [(50, 500), (500, 1500), (1500, 3000), (3000, 4000)]]
    print(f"{name}: rms={rms:.3f} quiet={quiet:.0f}%  energy 50-500/500-1500/1500-3000/3000-4000 Hz = "
          + "/".join(f"{b:.0f}%" for b in bands))


if __name__ == "__main__":
    if len(sys.argv) > 1:
        out, secs, seed, voices = sys.argv[1], float(sys.argv[2]), int(sys.argv[3]), int(sys.argv[4])
        ps = float(sys.argv[5]) if len(sys.argv) > 5 else 1.0
        x = crowd(secs, seed, voices, ps, report=True)
        write_wav(out, x, np.random.default_rng(seed + 1)); stats(out, x)
    else:
        outdir = Path("docs/murmur_ideas"); outdir.mkdir(parents=True, exist_ok=True)
        for name, secs, seed, voices, ps, lp in [("v2_overlap_6voices.wav", 40, 11, 6, 0.55, 0.04),
                                                  ("v2_overlap_4voices.wav", 40, 44, 4, 0.7, 0.05),
                                                  ("v2_one_voice.wav", 15, 33, 1, 1.0, 0.06)]:
            x = crowd(secs, seed, voices, ps, lp, report=True)
            write_wav(outdir / name, x, np.random.default_rng(seed + 1)); stats(name, x)
