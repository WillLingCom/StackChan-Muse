#!/usr/bin/env python3
"""Two-slot mic analysis for a K151 recording: levels, how alike the slots are, and the inter-slot delay
(GCC-PHAT on loud frames, sub-sample by parabolic peak). python3 scripts/mic_analyze.py private/audio/X.wav ..."""
import sys, wave
import numpy as np

RATE = 16000
for path in sys.argv[1:]:
    with wave.open(path) as w:
        x = np.frombuffer(w.readframes(w.getnframes()), dtype="<i2").astype(np.float64).reshape(-1, 2)
    x -= x.mean(axis=0)
    L, R = x[:, 0], x[:, 1]
    db = lambda s: 20 * np.log10(np.sqrt(np.mean(s ** 2)) / 32768 + 1e-12)
    corr = float(np.corrcoef(L, R)[0, 1])
    diff_db = db(L - R) - max(db(L), db(R))
    # loud frames (top 30% by energy) only
    F = 512
    n = len(L) // F
    e = np.array([np.sum(L[i * F:(i + 1) * F] ** 2 + R[i * F:(i + 1) * F] ** 2) for i in range(n)])
    keep = np.where(e >= np.percentile(e, 70))[0]
    lags, ild = [], []
    for i in keep:
        a, b = L[i * F:(i + 1) * F] * np.hanning(F), R[i * F:(i + 1) * F] * np.hanning(F)
        A, B = np.fft.rfft(a, 2 * F), np.fft.rfft(b, 2 * F)
        G = A * np.conj(B)
        cc = np.fft.irfft(G / (np.abs(G) + 1e-12))
        cc = np.concatenate((cc[-16:], cc[:17]))        # lags -16..16 samples
        k = int(np.argmax(cc))
        if 0 < k < len(cc) - 1:
            y0, y1, y2 = cc[k - 1], cc[k], cc[k + 1]
            k = k + 0.5 * (y0 - y2) / (y0 - 2 * y1 + y2 + 1e-12)
        lags.append(k - 16)
        ild.append(db(L[i * F:(i + 1) * F]) - db(R[i * F:(i + 1) * F]))
    lags = np.array(lags)
    print(f"{path.split('/')[-1]}: L {db(L):.1f} dBFS, R {db(R):.1f} dBFS, corr {corr:.3f}, "
          f"L-R residual {diff_db:.1f} dB | loud frames {len(keep)}: delay median {np.median(lags):+.2f} samples "
          f"({np.median(lags) / RATE * 1e6:+.0f} us), IQR {np.percentile(lags, 25):+.2f}..{np.percentile(lags, 75):+.2f}, "
          f"level L-R median {np.median(ild):+.1f} dB")
