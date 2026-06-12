#!/usr/bin/env python3
"""
make_default_kit.py — synthesize the four default drum WAVs.

Generates kick.wav / snare.wav / hat.wav / clap.wav at 44100 Hz mono
16-bit in this directory, then run:

    python wav2header.py kick.wav snare.wav hat.wav clap.wav -o kit_default.h

These are classic analog-style synthesis recipes so the machine always
has usable sounds with zero recorded material. Replace any of them later
with real WAVs (or mic recordings on the device itself).
"""

import math
import random
import struct
import wave

# 44100 so the M1 fixed-rate placeholder player sounds correct;
# from M4 on the ResamplingPlayer corrects any rate, so 22050 would also work.
RATE = 44100


def write_wav(name, samples):
    clipped = [max(-32767, min(32767, int(s * 32767))) for s in samples]
    with wave.open(name, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(struct.pack(f"<{len(clipped)}h", *clipped))
    print(f"{name}: {len(clipped)} samples ({len(clipped)/RATE:.3f}s)")


def kick(dur=0.35):
    n = int(dur * RATE)
    out, phase = [], 0.0
    for i in range(n):
        t = i / RATE
        freq = 45 + 160 * math.exp(-t / 0.028)          # pitch sweep
        phase += 2 * math.pi * freq / RATE
        amp = math.exp(-t / 0.11)
        click = 0.5 * math.exp(-t / 0.004)               # attack transient
        s = math.sin(phase) * amp + click * (random.random() * 2 - 1) * 0.3
        out.append(math.tanh(s * 1.6) * 0.95)            # soft saturation
    return out


def snare(dur=0.22):
    n = int(dur * RATE)
    out, phase = [], 0.0
    random.seed(2)
    for i in range(n):
        t = i / RATE
        freq = 180 + 60 * math.exp(-t / 0.02)
        phase += 2 * math.pi * freq / RATE
        body = math.sin(phase) * math.exp(-t / 0.045) * 0.5
        noise = (random.random() * 2 - 1) * math.exp(-t / 0.07) * 0.6
        out.append((body + noise) * 0.9)
    return out


def hat(dur=0.09):
    n = int(dur * RATE)
    out = []
    random.seed(3)
    hp_prev_in, hp_prev_out = 0.0, 0.0
    for i in range(t := 0, n):
        tt = i / RATE
        x = (random.random() * 2 - 1) * math.exp(-tt / 0.025)
        # one-pole highpass to brighten the noise
        hp = 0.95 * (hp_prev_out + x - hp_prev_in)
        hp_prev_in, hp_prev_out = x, hp
        out.append(hp * 0.8)
    return out


def clap(dur=0.25):
    n = int(dur * RATE)
    out = []
    random.seed(4)
    bursts = [0.0, 0.012, 0.025, 0.04]                   # multi-burst onset
    for i in range(n):
        t = i / RATE
        env = 0.0
        for b in bursts:
            if t >= b:
                env += math.exp(-(t - b) / 0.008) * 0.4
        env += math.exp(-t / 0.08) * 0.3 if t > 0.04 else 0.0
        x = (random.random() * 2 - 1)
        # band-ish shaping: average a couple of noise samples (crude lowpass)
        out.append(x * min(env, 1.0) * 0.85)
    return out


if __name__ == "__main__":
    write_wav("kick.wav", kick())
    write_wav("snare.wav", snare())
    write_wav("hat.wav", hat())
    write_wav("clap.wav", clap())
    print("Now run: python wav2header.py kick.wav snare.wav hat.wav clap.wav -o kit_default.h")
