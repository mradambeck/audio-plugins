#!/usr/bin/env python3
"""Phase 1: IR analysis pipeline.

For each captured IR, extracts:
  - measured decay characteristics (Schroeder backward-integration RT60 where a clean
    exponential region exists, PLUS raw -Ndb crossing times, since these captures are
    short and may be gated/truncated rather than naturally decaying)
  - envelope shape (Hilbert envelope, log-amplitude), looking for holds/knees/cutoff slope
  - spectral tilt over time (hi/lo energy ratio in short-time windows)
  - early reflection taps (peak-pick 80-150ms window)
  - echo density buildup (peak count per time window)

Writes analysis/features.json and one plot per IR under analysis/plots/.
"""
import json
import os

import numpy as np
import soundfile as sf
from scipy.signal import hilbert, find_peaks
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

IR_DIR = os.path.join(os.path.dirname(__file__), "..", "ir-captures")
PLOTS_DIR = os.path.join(os.path.dirname(__file__), "plots")
OUT_PATH = os.path.join(os.path.dirname(__file__), "features.json")

os.makedirs(PLOTS_DIR, exist_ok=True)


def load_mono(path):
    data, sr = sf.read(path, always_2d=True)
    return data.mean(axis=1).astype(np.float64), sr


def rms_envelope_db(x, sr, win_ms=20, hop_ms=5):
    """Short-time RMS envelope (dB). Returns (frame_center_indices, rms_db)."""
    win = max(1, int(sr * win_ms / 1000))
    hop = max(1, int(sr * hop_ms / 1000))
    n_frames = max(0, (len(x) - win) // hop + 1)
    idxs = np.empty(n_frames, dtype=int)
    out = np.empty(n_frames)
    for i in range(n_frames):
        seg = x[i * hop : i * hop + win]
        rms = np.sqrt(np.mean(seg ** 2) + 1e-24)
        idxs[i] = i * hop + win // 2
        out[i] = 20 * np.log10(rms + 1e-12)
    return idxs, out


def db(x, floor=1e-9):
    return 20.0 * np.log10(np.maximum(np.abs(x), floor))


def find_onset(x, sr):
    """Index of the first sample that crosses -20dB relative to the signal's peak."""
    peak = np.max(np.abs(x))
    if peak <= 0:
        return 0
    thresh = peak * 10 ** (-20 / 20)
    idx = np.argmax(np.abs(x) >= thresh)
    return int(idx)


def hilbert_envelope_db(x, sr, pad_ms=50):
    """Hilbert transform is FFT-based over the whole buffer, which implicitly treats the
    signal as periodic - a loud onset followed by a tail that doesn't return to zero (as here)
    causes a large onset-to-end discontinuity under that periodicity assumption, which leaks
    into a spurious envelope spike near the tail. Zero-padding before the transform and
    trimming back afterward avoids that wraparound artifact.
    """
    pad = int(sr * pad_ms / 1000)
    env = np.abs(hilbert(np.concatenate([x, np.zeros(pad)])))[: len(x)]
    return db(env)


def crossing_time(t, env_db, level_db, start_idx):
    """First time index after start_idx where env_db drops to/below level_db, else None."""
    below = np.where(env_db[start_idx:] <= level_db)[0]
    if below.size == 0:
        return None
    return t[start_idx + below[0]]


def schroeder_rt60(x, sr, onset_idx):
    """Schroeder backward-integration RT60 estimate from onset to end of signal.
    Returns (rt60_seconds_or_None, slope_db_per_s, r_squared) using whatever linear
    region exists between -5dB and -25dB of the integrated curve (these captures are
    short, so a full -5/-35 window often isn't available).
    """
    tail = x[onset_idx:]
    energy = tail[::-1] ** 2
    cum = np.cumsum(energy)[::-1]
    cum = cum / (cum[0] + 1e-20)
    curve_db = 10 * np.log10(np.maximum(cum, 1e-12))

    t = np.arange(len(curve_db)) / sr
    mask = (curve_db <= -5) & (curve_db >= -25)
    if mask.sum() < 5:
        return None, None, None, t, curve_db
    ts = t[mask]
    cs = curve_db[mask]
    slope, intercept = np.polyfit(ts, cs, 1)
    pred = slope * ts + intercept
    ss_res = np.sum((cs - pred) ** 2)
    ss_tot = np.sum((cs - cs.mean()) ** 2)
    r2 = 1 - ss_res / ss_tot if ss_tot > 0 else None
    if slope >= 0:
        return None, slope, r2, t, curve_db
    rt60 = -60.0 / slope
    return rt60, slope, r2, t, curve_db


def full_decay_rt60(x, sr, onset_idx, lo_db=-6, hi_db=-70):
    """RT60 estimate via linear fit to the RMS envelope (dB rel. peak) over a WIDE window
    (-6..-70dB by default), extrapolated to -60dB - unlike schroeder_rt60() above, which only
    sees a narrow -5..-25dB slice.

    Phase 6 validation (analysis/validation_report.md) found schroeder_rt60 badly underestimates
    these captures' true decay length: the -5..-25dB region it fits is dominated by the envelope's
    early hold/knee (see findings.md), not the slower decay trend that's clearly still visible out
    to -70dB or more on every capture's RMS envelope plot. Stopping at -70dB (not lower) avoids
    fitting into the digital-silence noise floor each capture eventually hits, which would flatten
    the slope and inflate the RT60 estimate the opposite direction.
    """
    idxs, rms_db = rms_envelope_db(x[onset_idx:], sr)
    if len(rms_db) == 0:
        return None, None, None
    t = idxs / sr
    rel = rms_db - np.max(rms_db)

    mask = (rel <= lo_db) & (rel >= hi_db)
    if mask.sum() < 5:
        return None, None, None
    ts, cs = t[mask], rel[mask]
    slope, intercept = np.polyfit(ts, cs, 1)
    pred = slope * ts + intercept
    ss_res = np.sum((cs - pred) ** 2)
    ss_tot = np.sum((cs - cs.mean()) ** 2)
    r2 = 1 - ss_res / ss_tot if ss_tot > 0 else None

    if slope >= 0:
        return None, slope, r2
    return -60.0 / slope, slope, r2


def spectral_tilt_over_time(x, sr, win_ms=20, hop_ms=10, split_hz=2000):
    win = int(sr * win_ms / 1000)
    hop = int(sr * hop_ms / 1000)
    win = max(win, 64)
    hop = max(hop, 32)
    n_frames = max(0, (len(x) - win) // hop + 1)
    times = []
    tilts_db = []
    freqs = np.fft.rfftfreq(win, 1 / sr)
    lo_mask = freqs < split_hz
    hi_mask = freqs >= split_hz
    for i in range(n_frames):
        seg = x[i * hop : i * hop + win] * np.hanning(win)
        spec = np.abs(np.fft.rfft(seg)) ** 2
        lo_e = spec[lo_mask].sum() + 1e-20
        hi_e = spec[hi_mask].sum() + 1e-20
        tilts_db.append(10 * np.log10(hi_e / lo_e))
        times.append((i * hop + win / 2) / sr)
    return np.array(times), np.array(tilts_db)


def echo_density(x, sr, onset_idx, win_ms=10, hop_ms=5):
    win = int(sr * win_ms / 1000)
    hop = int(sr * hop_ms / 1000)
    win = max(win, 32)
    hop = max(hop, 16)
    tail = x[onset_idx:]
    n_frames = max(0, (len(tail) - win) // hop + 1)
    times = []
    counts = []
    for i in range(n_frames):
        seg = tail[i * hop : i * hop + win]
        if len(seg) < 3:
            continue
        thresh = 0.15 * (np.max(np.abs(seg)) + 1e-12)
        peaks, _ = find_peaks(np.abs(seg), height=thresh)
        counts.append(len(peaks))
        times.append((onset_idx + i * hop + win / 2) / sr)
    return np.array(times), np.array(counts)


def early_reflection_taps(x, sr, onset_idx, window_ms=(0, 150), max_taps=12):
    start = onset_idx + int(sr * window_ms[0] / 1000)
    end = onset_idx + int(sr * window_ms[1] / 1000)
    end = min(end, len(x))
    seg = x[start:end]
    if len(seg) == 0:
        return []
    peak = np.max(np.abs(seg)) + 1e-12
    peaks, props = find_peaks(np.abs(seg), height=0.05 * peak, distance=max(1, int(sr * 0.0005)))
    order = np.argsort(-props["peak_heights"])[:max_taps]
    taps = []
    for idx in peaks[order]:
        t_ms = (idx) / sr * 1000
        gain_db = db(np.array([seg[idx]]))[0]
        taps.append({"time_ms": round(float(t_ms), 3), "gain_db": round(float(gain_db), 2)})
    taps.sort(key=lambda d: d["time_ms"])
    return taps


def analyze_file(path, name):
    x, sr = load_mono(path)
    onset_idx = find_onset(x, sr)
    t_full = np.arange(len(x)) / sr

    env_db = hilbert_envelope_db(x, sr)
    peak_db = np.max(env_db)
    env_db_norm = env_db - peak_db

    c10 = crossing_time(t_full, env_db_norm, -10, onset_idx)
    c20 = crossing_time(t_full, env_db_norm, -20, onset_idx)
    c30 = crossing_time(t_full, env_db_norm, -30, onset_idx)
    c40 = crossing_time(t_full, env_db_norm, -40, onset_idx)
    end_db = float(env_db_norm[-1])
    duration_s = len(x) / sr
    onset_time_s = onset_idx / sr

    rt60, slope, r2, sch_t, sch_curve = schroeder_rt60(x, sr, onset_idx)
    full_rt60, full_slope, full_r2 = full_decay_rt60(x, sr, onset_idx)

    rms_idxs, rms_db = rms_envelope_db(x, sr)
    rms_t = rms_idxs / sr
    rms_db_norm = rms_db - np.max(rms_db)
    below60 = np.where(rms_db_norm <= -60)[0]
    rms_time_to_minus60db_s = float(rms_t[below60[0]] - onset_time_s) if below60.size else None

    tilt_t, tilt_db = spectral_tilt_over_time(x, sr)
    # tilt trend: linear fit of tilt vs time from onset to end
    tilt_mask = tilt_t >= onset_time_s
    tilt_slope = None
    if tilt_mask.sum() >= 5:
        tilt_slope = float(np.polyfit(tilt_t[tilt_mask], tilt_db[tilt_mask], 1)[0])

    dens_t, dens_c = echo_density(x, sr, onset_idx)
    dens_onset_time = None
    if len(dens_c) > 0:
        peak_density = dens_c.max()
        if peak_density > 0:
            half_idx = np.argmax(dens_c >= 0.5 * peak_density)
            dens_onset_time = float(dens_t[half_idx] - onset_time_s)

    taps = early_reflection_taps(x, sr, onset_idx)

    feat = {
        "filename": name,
        "duration_s": round(duration_s, 4),
        "onset_time_s": round(onset_time_s, 4),
        "peak_db_fs": round(float(peak_db), 2),
        "time_to_minus10db_s": round(c10 - onset_time_s, 4) if c10 is not None else None,
        "time_to_minus20db_s": round(c20 - onset_time_s, 4) if c20 is not None else None,
        "time_to_minus30db_s": round(c30 - onset_time_s, 4) if c30 is not None else None,
        "time_to_minus40db_s": round(c40 - onset_time_s, 4) if c40 is not None else None,
        "end_level_db_rel_peak": round(end_db, 2),
        "schroeder_rt60_s": round(rt60, 4) if rt60 is not None else None,
        "schroeder_slope_db_per_s": round(slope, 2) if slope is not None else None,
        "schroeder_r2": round(r2, 4) if r2 is not None else None,
        "full_decay_rt60_s": round(full_rt60, 4) if full_rt60 is not None else None,
        "full_decay_slope_db_per_s": round(full_slope, 2) if full_slope is not None else None,
        "full_decay_r2": round(full_r2, 4) if full_r2 is not None else None,
        "rms_time_to_minus60db_s": round(rms_time_to_minus60db_s, 4) if rms_time_to_minus60db_s is not None else None,
        "spectral_tilt_slope_db_per_s": round(tilt_slope, 2) if tilt_slope is not None else None,
        "spectral_tilt_start_db": round(float(tilt_db[tilt_mask][0]), 2) if tilt_mask.sum() else None,
        "spectral_tilt_end_db": round(float(tilt_db[tilt_mask][-1]), 2) if tilt_mask.sum() else None,
        "echo_density_half_rise_time_s": round(dens_onset_time, 4) if dens_onset_time is not None else None,
        "echo_density_peak_count": int(dens_c.max()) if len(dens_c) else None,
        "early_reflection_taps": taps,
        "num_er_taps": len(taps),
    }

    fig, axes = plt.subplots(3, 1, figsize=(9, 8), sharex=False)
    axes[0].plot(t_full, env_db_norm, lw=0.8, label="Hilbert env")
    axes[0].plot(rms_t, rms_db_norm, lw=1.4, color="black", label="20ms RMS env")
    axes[0].legend(fontsize=7, loc="upper right")
    axes[0].axhline(-10, color="gray", lw=0.5, ls="--")
    axes[0].axhline(-20, color="gray", lw=0.5, ls="--")
    axes[0].axhline(-30, color="gray", lw=0.5, ls="--")
    axes[0].axvline(onset_time_s, color="red", lw=0.5)
    axes[0].set_ylim(-80, 5)
    axes[0].set_title(f"{name} — Hilbert envelope (dB rel peak)")
    axes[0].set_xlabel("s")

    axes[1].plot(tilt_t, tilt_db, lw=0.8, color="orange")
    axes[1].axvline(onset_time_s, color="red", lw=0.5)
    axes[1].set_title("spectral tilt: 10*log10(HF energy / LF energy), split 2kHz")
    axes[1].set_xlabel("s")

    axes[2].plot(dens_t, dens_c, lw=0.8, color="green")
    axes[2].set_title("echo density (peaks per 10ms window)")
    axes[2].set_xlabel("s")

    fig.tight_layout()
    fig.savefig(os.path.join(PLOTS_DIR, name.replace(".wav", ".png")), dpi=110)
    plt.close(fig)

    return feat


def main():
    files = sorted(f for f in os.listdir(IR_DIR) if f.endswith(".wav"))
    results = []
    for f in files:
        print(f"analyzing {f} ...")
        feat = analyze_file(os.path.join(IR_DIR, f), f)
        results.append(feat)

    with open(OUT_PATH, "w") as fh:
        json.dump(results, fh, indent=2)
    print(f"\nWrote {OUT_PATH}")
    print(f"Wrote plots to {PLOTS_DIR}/")


if __name__ == "__main__":
    main()
