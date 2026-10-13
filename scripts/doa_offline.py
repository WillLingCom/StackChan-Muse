#!/usr/bin/env python3
"""Offline direction analysis of K151 two-mic recordings (private/audio/*.wav): speech-band (300-3400 Hz)
GCC-PHAT on frames >= FLOOR+6 dB, sub-sample peak. Positive delay = sound reached slot L (the viewer-left
hole = the robot's RIGHT) first; negative = the robot's LEFT. (Checked: a scratch at the viewer-left hole reads +1.37.) python3 scripts/doa_offline.py NAME [NAME ...]"""
import sys, wave
from pathlib import Path
import numpy as np

RATE, F, MAXLAG = 16000, 512, 3
ROOT = Path(__file__).resolve().parents[1] / "private" / "audio"


def load(n):
    with wave.open(str(ROOT / (n + ".wav"))) as w:
        x = np.frombuffer(w.readframes(w.getnframes()), dtype="<i2").astype(float).reshape(-1, 2)
    return x - x.mean(0)


def band(x):
    X = np.fft.rfft(x)
    f = np.fft.rfftfreq(len(x), 1 / RATE)
    X[(f < 300) | (f > 3400)] = 0
    return np.fft.irfft(X, len(x))


def fdb(s):
    return 10 * np.log10(np.mean(s ** 2) / 32768 ** 2 + 1e-15)


def frame_delays(name, floor):
    x = load(name)
    L, R = band(x[:, 0]), band(x[:, 1])
    out = []
    for i in range(len(L) // F):
        a, b = L[i * F:(i + 1) * F], R[i * F:(i + 1) * F]
        lvl = fdb(np.concatenate((a, b)))
        if lvl < floor + 6:
            continue
        A, B = np.fft.rfft(a * np.hanning(F), 2 * F), np.fft.rfft(b * np.hanning(F), 2 * F)
        G = A * np.conj(B)
        fr = np.fft.rfftfreq(2 * F, 1 / RATE)
        G[(fr < 300) | (fr > 3400)] = 0
        cc = np.fft.irfft(G / (np.abs(G) + 1e-9))
        cc = np.concatenate((cc[-MAXLAG:], cc[:MAXLAG + 1]))
        k = int(np.argmax(cc))
        if 0 < k < len(cc) - 1:
            y0, y1, y2 = cc[k - 1], cc[k], cc[k + 1]
            k = k + 0.5 * (y0 - y2) / (y0 - 2 * y1 + y2 + 1e-12)
        out.append((-(k - MAXLAG), lvl))   # + = slot L first (robot's right)
    return out


if __name__ == "__main__":
    q = load("quiet")
    qb = np.stack([band(q[:, 0]), band(q[:, 1])], 1)
    floor = float(np.median([fdb(qb[i * F:(i + 1) * F]) for i in range(len(qb) // F)]))
    for n in sys.argv[1:]:
        d = frame_delays(n, floor)
        if not d:
            print(f"{n}: no speech frames")
            continue
        lag = np.array([t for t, _ in d])
        lv = np.array([v for _, v in d])
        print(f"{n:16s} frames {len(d):3d} (of {len(load(n)) // F}), level med {np.median(lv):.1f} dBFS (SNR ~{np.median(lv) - floor:.0f} dB), "
              f"delay med {np.median(lag):+.2f} smp, IQR {np.percentile(lag, 25):+.2f}..{np.percentile(lag, 75):+.2f}")
