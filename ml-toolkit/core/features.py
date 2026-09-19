"""Effect-agnostic acoustic feature extraction, for both hand-review (findings.md-style analysis)
and empirical validation (Phase D) - the same functions are used in both places on purpose, so a
fitted/rendered result is judged by the identical yardstick used to characterize the real captures.

Ported near-verbatim from plugins/intruder-gated-reverb/analysis/analyze_irs.py, with two hard-won fixes
kept intact rather than rediscovered:

  - full_decay_rt60() uses a wide (-6..-70dB) window, not just Schroeder's narrow -5..-25dB
    region: Intruder's own findings.md found the narrow-window Schroeder fit badly UNDERESTIMATES
    decay on short/gated/non-fully-decayed captures, since that narrow region is often dominated
    by an envelope's early hold/knee rather than its true decay trend. Use full_decay_rt60 as the
    default "how long does this actually ring" measurement; schroeder_rt60 is kept for reference/
    cross-checking only.
  - hilbert_envelope_db() zero-pads before the FFT-based Hilbert transform: a loud onset followed
    by a tail that doesn't return to zero creates a large implied discontinuity under the FFT's
    periodicity assumption, which otherwise leaks into a spurious envelope spike near the buffer's
    end (this produced a false "trailing blip" finding in Intruder's own analysis history - see
    plugins/intruder-gated-reverb/analysis/findings.md's "Method notes").

Plotting is deliberately NOT here (unlike the original analyze_irs.py, which entangled the two) -
each effect's own analyze.py owns its plot layout, since that's somewhat effect-specific.

Functions added for effects/nonlin (gate envelope shape, normalized echo density, modal-overlap
crossover, stereo, tonal-character helpers) stay effect-agnostic like everything above them, but are
NOT wired into analyze_capture() below - that function's output shape is exactly what
effects/ambience's features.json/findings.md/fitted_raw.json/cross_validation_report.md are keyed
to, and this module has no way to know which future callers depend on it staying fixed. Each new
function is called directly from effects/nonlin/analyze.py instead.
"""
from __future__ import annotations

import numpy as np
from scipy.signal import butter, coherence as _coherence, find_peaks, hilbert, sosfiltfilt, welch
from scipy.stats import kurtosis as _kurtosis


def db(x: np.ndarray, floor: float = 1e-9) -> np.ndarray:
    return 20.0 * np.log10(np.maximum(np.abs(x), floor))


def find_onset(x: np.ndarray, sr: int) -> int:
    """Index of the first sample that crosses -20dB relative to the signal's peak."""
    peak = np.max(np.abs(x))
    if peak <= 0:
        return 0
    thresh = peak * 10 ** (-20 / 20)
    idx = np.argmax(np.abs(x) >= thresh)
    return int(idx)


def hilbert_envelope_db(x: np.ndarray, sr: int, pad_ms: float = 50) -> np.ndarray:
    """Hilbert transform is FFT-based over the whole buffer, which implicitly treats the signal
    as periodic - a loud onset followed by a tail that doesn't return to zero (as here) causes a
    large onset-to-end discontinuity under that periodicity assumption, which leaks into a
    spurious envelope spike near the tail. Zero-padding before the transform and trimming back
    afterward avoids that wraparound artifact."""
    pad = int(sr * pad_ms / 1000)
    env = np.abs(hilbert(np.concatenate([x, np.zeros(pad)])))[: len(x)]
    return db(env)


def rms_envelope_db(x: np.ndarray, sr: int, win_ms: float = 20, hop_ms: float = 5) -> tuple[np.ndarray, np.ndarray]:
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


def crossing_time(t: np.ndarray, env_db: np.ndarray, level_db: float, start_idx: int):
    """First time index after start_idx where env_db drops to/below level_db, else None."""
    below = np.where(env_db[start_idx:] <= level_db)[0]
    if below.size == 0:
        return None
    return t[start_idx + below[0]]


def schroeder_rt60(x: np.ndarray, sr: int, onset_idx: int):
    """Schroeder backward-integration RT60 estimate from onset to end of signal.
    Returns (rt60_seconds_or_None, slope_db_per_s, r_squared, t, curve_db) using whatever linear
    region exists between -5dB and -25dB of the integrated curve. See module docstring: this
    narrow window can badly underestimate decay on short/gated captures - prefer full_decay_rt60
    as the default measurement; use this one for cross-checking only."""
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


def full_decay_rt60(x: np.ndarray, sr: int, onset_idx: int, lo_db: float = -6, hi_db: float = -70):
    """RT60 estimate via linear fit to the RMS envelope (dB rel. peak) over a WIDE window
    (-6..-70dB by default), extrapolated to -60dB - unlike schroeder_rt60() above, which only
    sees a narrow -5..-25dB slice that's often dominated by an envelope's early hold/knee rather
    than its true decay trend. Stopping at -70dB (not lower) avoids fitting into the digital-
    silence noise floor each capture eventually hits, which would flatten the slope and inflate
    the RT60 estimate the opposite direction.

    Returns (rt60_seconds_or_None, slope_db_per_s, r_squared)."""
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


def spectral_tilt_over_time(x: np.ndarray, sr: int, win_ms: float = 20, hop_ms: float = 10, split_hz: float = 2000):
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


def echo_density(x: np.ndarray, sr: int, onset_idx: int, win_ms: float = 10, hop_ms: float = 5,
                  threshold_ref: str = "window"):
    """Peaks-per-window as a rough diffusion proxy.

    KNOWN BUG, kept as the default rather than silently changed: threshold_ref="window" (the
    original, still-default behaviour) sets each window's threshold to 15% of THAT WINDOW's own
    peak, so a sparse, spiky window (few, large echoes) and a dense, uniform one can report similar
    peak counts even though only one of them is actually diffuse - the threshold moves with the
    thing being measured. effects/ambience's findings.md already flagged this. threshold_ref="global"
    fixes it (15% of the WHOLE tail's peak, held fixed across every window) but is not the default
    here because effects/ambience/features.json, findings.md and cross_validation_report.md are all
    keyed to the buggy behaviour - changing the default would silently invalidate them. New code
    should prefer normalized_echo_density() below instead, which has no threshold at all."""
    win = int(sr * win_ms / 1000)
    hop = int(sr * hop_ms / 1000)
    win = max(win, 32)
    hop = max(hop, 16)
    tail = x[onset_idx:]
    n_frames = max(0, (len(tail) - win) // hop + 1)
    times = []
    counts = []
    global_peak = np.max(np.abs(tail)) + 1e-12 if len(tail) else 1e-12
    for i in range(n_frames):
        seg = tail[i * hop : i * hop + win]
        if len(seg) < 3:
            continue
        if threshold_ref == "global":
            thresh = 0.15 * global_peak
        else:
            thresh = 0.15 * (np.max(np.abs(seg)) + 1e-12)
        peaks, _ = find_peaks(np.abs(seg), height=thresh)
        counts.append(len(peaks))
        times.append((onset_idx + i * hop + win / 2) / sr)
    return np.array(times), np.array(counts)


def normalized_echo_density(x: np.ndarray, sr: int, onset_idx: int, win_ms: float = 24, hop_ms: float = 6):
    """Abel-Huang normalized echo density (NED): per window, the fraction of samples exceeding
    that window's own standard deviation, divided by erfc(1/sqrt(2)) (~0.3173, the fraction a
    Gaussian-distributed signal is expected to exceed). NED=1 means the window is statistically
    indistinguishable from Gaussian noise (fully diffuse); NED<<1 means a sparse impulse train.

    Threshold-free and scale-invariant, unlike echo_density() above - replaces it for effects/nonlin
    rather than fixing it in place, since Ambience's own numbers must not move.
    Returns (frame_center_times_s, ned)."""
    from scipy.special import erfc

    win = max(3, int(sr * win_ms / 1000))
    hop = max(1, int(sr * hop_ms / 1000))
    tail = x[onset_idx:]
    n_frames = max(0, (len(tail) - win) // hop + 1)
    norm = erfc(1 / np.sqrt(2))
    times = np.empty(n_frames)
    ned = np.empty(n_frames)
    for i in range(n_frames):
        seg = tail[i * hop : i * hop + win]
        sigma = np.std(seg)
        frac = np.mean(np.abs(seg) > sigma) if sigma > 0 else 0.0
        ned[i] = frac / norm
        times[i] = (onset_idx + i * hop + win / 2) / sr
    return times, ned


def onset_echo_density(x: np.ndarray, sr: int, onset_idx: int, win_ms: float = 10, hop_ms: float = 2,
                        duration_ms: float = 20, pre_emphasis: float = 0.95):
    """NED specifically over the first `duration_ms` after onset, at a much finer window/hop than
    normalized_echo_density's own defaults (24ms/6ms win/hop) - tuned to resolve density WITHIN the
    first 10-20ms, which is where a gated reverb's initial-attack density (or the lack of it) lives.
    A wider window there averages across the whole thing it's meant to resolve, so a genuinely
    sparse first few ms can be invisible in the coarser metric's own first frame or two.

    Written to catch the specific gap plugins/inhalt-nonlin's first render had: qualitatively
    described as "denser quality... in the initial attack" and "more gritty" missing from the
    render (real captures measured near-flat NED ~0.38-0.43 from the very first analysis frame;
    the render's own coarse NED only reached that range 80-120ms in). Meant to be called on both a
    render and its reference capture and compared directly - see
    plugins/inhalt-nonlin/analysis/validate.py and effects/nonlin/fit_nonlin.py's
    core.fit.onset_density_loss (the differentiable fit-time counterpart to this metric).

    `pre_emphasis` (a first-order pre-emphasis/whitening filter, y[n] = x[n] - pre_emphasis*x[n-1],
    applied before computing NED) exists because of a real, measured confound: a spectral tilt
    (High's own effect, or plugins/inhalt-nonlin's synthesized tilt filter) changes a short
    window's effective degrees of freedom (colored/correlated samples carry less independent
    information than white ones), which biases this window-relative-std statistic even when the
    underlying temporal diffuseness hasn't actually changed. Measured directly on plugins/
    inhalt-nonlin's own renders: with the tilt filter forced neutral, three High settings at the
    same Time gave IDENTICAL onset_ned_mean (0.513/0.513/0.513) - proof the un-whitened metric's
    own High=0-vs-High!=0 split (previously ~0.10 vs ~0.20-0.26 mean error) was substantially a
    tilt-driven artifact of the metric, not a genuine hardware effect (the real captures show no
    such clean split). Pre-emphasis (alpha=0.95, a standard whitening constant) shrinks that
    split's magnitude by roughly half without needing to touch the tilt filter itself or the
    signal chain - set to 0.0 to disable and reproduce the un-whitened (confounded) statistic.

    Returns a dict: `times_ms` (frame centers relative to onset), `ned` (the trajectory),
    `onset_ned_mean` (mean NED over the window - the single number most useful for a
    render-vs-reference table), and `onset_ned_first_window` (NED of just the very first window -
    the most attack-sensitive single number, since a mean can hide a thin start that fills in a
    few ms later)."""
    if pre_emphasis:
        x = np.append(x[0], x[1:] - pre_emphasis * x[:-1])
    times, ned = normalized_echo_density(x, sr, onset_idx, win_ms=win_ms, hop_ms=hop_ms)
    times_ms = (times - onset_idx / sr) * 1000.0
    mask = times_ms <= duration_ms
    windowed_ned = ned[mask]
    return {
        "times_ms": times_ms[mask],
        "ned": windowed_ned,
        "onset_ned_mean": float(np.mean(windowed_ned)) if len(windowed_ned) else None,
        "onset_ned_first_window": float(windowed_ned[0]) if len(windowed_ned) else None,
    }


def time_to_ned_threshold(times: np.ndarray, ned: np.ndarray, threshold: float = 0.9, hold_frames: int = 3):
    """First time NED reaches `threshold` and STAYS there for `hold_frames` consecutive frames
    (a single frame crossing is common noise even for a genuinely sparse signal - requiring a
    sustained crossing avoids reporting a spurious early hit)."""
    above = ned >= threshold
    for i in range(len(above) - hold_frames + 1):
        if above[i : i + hold_frames].all():
            return float(times[i])
    return None


def mixing_time_ms(x: np.ndarray, sr: int, onset_idx: int, win_ms: float = 10, hop_ms: float = 5,
                    kurtosis_threshold: float = 0.5):
    """Sliding-window excess kurtosis (scipy's default Fisher convention: 0 for a true Gaussian).
    Mixing time = first sustained (3-frame) point where excess kurtosis falls below
    `kurtosis_threshold` and stays there - an independent estimator of "when does this become
    statistically diffuse" from normalized_echo_density's threshold crossing; disagreement between
    the two is informative; agreement is corroboration, not redundancy, since they measure
    different statistical properties (amplitude distribution shape vs. peak density)."""
    win = max(8, int(sr * win_ms / 1000))
    hop = max(1, int(sr * hop_ms / 1000))
    tail = x[onset_idx:]
    n_frames = max(0, (len(tail) - win) // hop + 1)
    if n_frames == 0:
        return None
    times = np.empty(n_frames)
    kurt = np.empty(n_frames)
    for i in range(n_frames):
        seg = tail[i * hop : i * hop + win]
        kurt[i] = _kurtosis(seg, fisher=True, bias=False)
        times[i] = (onset_idx + i * hop + win / 2) / sr
    below = kurt <= kurtosis_threshold
    hold_frames = 3
    for i in range(len(below) - hold_frames + 1):
        if below[i : i + hold_frames].all():
            return float(times[i] * 1000.0)
    return None


# ---------------------------------------------------------------------------------------------
# Gate envelope shape (build-up / plateau / knee / fall) - the defining feature of a gated reverb
# like the RMX16's NonLin program. Nothing above this measures it: full_decay_rt60() fits a single
# line through a shape that isn't one (the gate truncates the response before any tank decay is
# visible), so on a gated capture it measures the GATE's fall rate, not the tank's damping. See
# effects/nonlin/findings.md for the case that RT60 is the wrong primitive for this effect.
# ---------------------------------------------------------------------------------------------

def gate_envelope_params(x: np.ndarray, sr: int, onset_idx: int, win_ms: float = 5, hop_ms: float = 1,
                          floor_db: float = -70):
    """Build-up time, plateau level/ripple/droop, knee time, fall rate, via a swept-breakpoint
    two-segment piecewise-linear fit on a FINE RMS envelope (5ms/1ms default - the 20ms/5ms default
    of rms_envelope_db() would smear a ~20ms build-up into nothing).

    Segment A is fit from the build-up crossing to a candidate breakpoint; segment B from the
    breakpoint to the first -70dB (floor_db) crossing or the end of the capture. The breakpoint
    minimizing the SUM of both segments' squared residuals is taken as the knee. This is a
    standard changepoint-detection approach: no tuning constant decides "where the knee is" except
    floor_db (which only bounds the search region, not the answer).

    knee_r2 (1 - residual/total variance over the whole fitted region) is a fit-quality sanity
    check, NOT a "is there really a knee" check - a two-segment line fit is flexible enough to fit
    a PURE exponential decay well too (both segments end up nearly collinear), so knee_r2 stays
    high in both cases (empirically ~0.998-0.9997 either way). The actual "is this really gated,
    or just decaying" signal is abs(plateau_droop_db_per_s - fall_rate_db_per_s): a real gate has
    a near-flat plateau and a steep fall (measured ~300dB/s apart on a synthetic test case), while
    a plain exponential has both segments agree closely (~10dB/s apart on the same test) because
    there is only one real slope to find. Use knee_r2 to catch a genuinely bad fit (very low
    value); use the slope difference to judge whether the shape is gated.

    Returns a dict; every field is None if there isn't enough data on either side of onset_idx."""
    empty = {
        "build_up_ms": None, "plateau_level_db": None, "plateau_ripple_db": None,
        "plateau_droop_db_per_s": None, "knee_time_ms": None, "fall_rate_db_per_s": None,
        "knee_r2": None, "gate_length_ms_at_20db": None,
    }
    idxs, rms_db = rms_envelope_db(x, sr, win_ms=win_ms, hop_ms=hop_ms)
    if len(rms_db) == 0:
        return empty
    t = idxs / sr
    peak_db = np.max(rms_db)
    env = rms_db - peak_db
    onset_t = onset_idx / sr

    mask = t >= onset_t
    if mask.sum() < 5:
        return empty
    tt = t[mask] - onset_t
    ee = env[mask]

    below20 = np.where(ee <= -20.0)[0]
    gate_length_ms_at_20db = float(tt[below20[0]] * 1000.0) if below20.size else None

    plateau_mask = ee >= -6.0
    if plateau_mask.sum() >= 3:
        plateau_level_db = float(np.median(ee[plateau_mask]))
        plateau_ripple_db = float(np.std(ee[plateau_mask]))
    else:
        plateau_level_db = float(ee.max())
        plateau_ripple_db = 0.0

    build_thresh = plateau_level_db - 3.0
    above = np.where(ee >= build_thresh)[0]
    build_up_idx = int(above[0]) if above.size else 0
    build_up_ms = float(tt[build_up_idx] * 1000.0)

    below_floor = np.where(ee[build_up_idx:] <= floor_db)[0]
    floor_idx = build_up_idx + int(below_floor[0]) if below_floor.size else len(ee) - 1

    region_t = tt[build_up_idx : floor_idx + 1]
    region_e = ee[build_up_idx : floor_idx + 1]
    min_seg = 4
    n = len(region_t)
    if n < 2 * min_seg:
        return {
            "build_up_ms": round(build_up_ms, 3),
            "plateau_level_db": round(plateau_level_db, 2),
            "plateau_ripple_db": round(plateau_ripple_db, 3),
            "plateau_droop_db_per_s": None, "knee_time_ms": None, "fall_rate_db_per_s": None,
            "knee_r2": None,
            "gate_length_ms_at_20db": round(gate_length_ms_at_20db, 2) if gate_length_ms_at_20db is not None else None,
        }

    best_resid = np.inf
    best_k = min_seg
    best_params = None
    for k in range(min_seg, n - min_seg):
        ta, ea = region_t[:k], region_e[:k]
        tb, eb = region_t[k:], region_e[k:]
        sa, ia = np.polyfit(ta, ea, 1)
        sb, ib = np.polyfit(tb, eb, 1)
        resid = float(np.sum((ea - (sa * ta + ia)) ** 2) + np.sum((eb - (sb * tb + ib)) ** 2))
        if resid < best_resid:
            best_resid = resid
            best_k = k
            best_params = (sa, sb)

    sa, sb = best_params
    knee_time_ms = float(region_t[best_k] * 1000.0)
    ss_tot = float(np.sum((region_e - region_e.mean()) ** 2))
    knee_r2 = (1.0 - best_resid / ss_tot) if ss_tot > 0 else None

    return {
        "build_up_ms": round(build_up_ms, 3),
        "plateau_level_db": round(plateau_level_db, 2),
        "plateau_ripple_db": round(plateau_ripple_db, 3),
        "plateau_droop_db_per_s": round(float(sa), 3),
        "knee_time_ms": round(knee_time_ms, 3),
        "fall_rate_db_per_s": round(float(sb), 2),
        "knee_r2": round(float(knee_r2), 4) if knee_r2 is not None else None,
        "gate_length_ms_at_20db": round(gate_length_ms_at_20db, 2) if gate_length_ms_at_20db is not None else None,
    }


def post_knee_excess_db(x: np.ndarray, sr: int, onset_idx: int, knee_time_ms: float,
                         fall_rate_db_per_s: float, probe_ms: float = 20.0,
                         win_ms: float = 5, hop_ms: float = 1) -> float | None:
    """How far the envelope's level `probe_ms` after the knee sits from where a straight-line
    extrapolation of fall_rate_db_per_s (gate_envelope_params()'s own whole-segment-average post-
    knee slope) would place it - a signed dB value, negative meaning the real fall is STEEPER
    right at the knee than its own long-run average suggests (the real captures' own actual
    shape), positive meaning shallower.

    Exists because fall_rate_db_per_s, by construction, is a SINGLE average slope over the whole
    post-knee segment (knee to the -70dB floor) - correct for matching the long-run/deep-tail
    decay character (see InhaltParameterMap's own fallRateDbPerSec calibration), but real NonLin
    captures' post-knee fall is measurably CURVED, not a single constant rate: checked directly on
    all 9 real captures, 7 of 9 show a steeper-than-average initial drop (this function returns
    -1 to -3.6dB at 20ms) while the two captures with the shallowest knee (closest to the peak,
    Time=7.0/9.8 at High=0) show the OPPOSITE, a shallower-than-average start (+1.8 to +2.1dB) -
    a real, heterogeneous property of the hardware, not noise, which is why this needs its own
    directly-measured, per-Time/High-calibrated correction term (InhaltIRSynth's own
    earlyExcessDb) rather than a single universal constant.

    Returns None if knee_time_ms or fall_rate_db_per_s is None, or there isn't enough audio past
    the probe point."""
    if knee_time_ms is None or fall_rate_db_per_s is None:
        return None
    idxs, rms_db = rms_envelope_db(x, sr, win_ms=win_ms, hop_ms=hop_ms)
    if len(rms_db) == 0:
        return None
    t_ms = (idxs / sr - onset_idx / sr) * 1000.0
    env = rms_db - np.max(rms_db)
    if t_ms[-1] < knee_time_ms + probe_ms:
        return None
    level_at_knee = float(np.interp(knee_time_ms, t_ms, env))
    probe_time_ms = knee_time_ms + probe_ms
    level_at_probe = float(np.interp(probe_time_ms, t_ms, env))
    asymptotic_level_at_probe = level_at_knee + fall_rate_db_per_s * (probe_ms / 1000.0)
    return round(level_at_probe - asymptotic_level_at_probe, 3)


# Octave bands, centre frequencies 63Hz-16kHz - the standard ISO set within this unit's
# documented ~20Hz-18kHz bandwidth. Edges are centre * 2**(+-0.5) (one full octave wide).
OCTAVE_BAND_CENTERS_HZ = (63.0, 125.0, 250.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0, 16000.0)


def octave_bands(centers_hz=OCTAVE_BAND_CENTERS_HZ, sr: int = 44100):
    """[(lo_hz, hi_hz), ...] one octave wide around each center, clipped to (1Hz, Nyquist)."""
    nyquist = sr / 2.0
    bands = []
    for c in centers_hz:
        lo = max(1.0, c / (2 ** 0.5))
        hi = min(nyquist - 1.0, c * (2 ** 0.5))
        if hi > lo:
            bands.append((lo, hi))
    return bands


def band_gate_params(x: np.ndarray, sr: int, onset_idx: int, bands=None):
    """gate_envelope_params() computed independently per octave band (zero-phase filtered via
    sosfiltfilt - a causal filter would smear the knee edge, which is exactly the thing being
    measured). Returns {(lo_hz, hi_hz): gate_envelope_params dict}.

    This is how per-band damping is actually measured for a gated reverb: the only window where
    tank damping is visible uncontaminated by the gate is the PLATEAU - real in-loop HF damping
    would show as the high band's plateau_droop_db_per_s being more negative than the low band's,
    while their fall_rate_db_per_s (dominated by the gate, not the tank) stays similar."""
    if bands is None:
        bands = octave_bands(sr=sr)
    out = {}
    for lo, hi in bands:
        sos = butter(4, [lo, hi], btype="bandpass", fs=sr, output="sos")
        band_sig = sosfiltfilt(sos, x)
        out[(round(lo, 1), round(hi, 1))] = gate_envelope_params(band_sig, sr, onset_idx)
    return out


# ---------------------------------------------------------------------------------------------
# Modal density / "Schroeder frequency" substitute. The textbook f_s = 2000*sqrt(T60/V) needs a
# room volume, and there is no room here - the RMX16 is a digital algorithm. What's measured
# instead is the physically-meaningful thing the formula is actually a proxy for: the frequency
# above which modal overlap exceeds 1 (multiple modes' -3dB skirts overlap, so the response looks
# statistically dense rather than like discrete resonances). See effects/nonlin/findings.md for
# the full derivation - report this, never a number computed from an invented volume.
# ---------------------------------------------------------------------------------------------

def modal_peak_spacing(x: np.ndarray, sr: int, window_s: tuple[float, float], bands):
    """FFT magnitude over a window presumed quasi-stationary (e.g. inside the gate's plateau, where
    the response isn't yet collapsing under the knee), mean adjacent spectral-peak spacing per band.
    Returns {(lo_hz, hi_hz): mean_spacing_hz_or_None}."""
    start = max(0, int(window_s[0] * sr))
    end = min(len(x), int(window_s[1] * sr))
    seg = x[start:end]
    out = {}
    if len(seg) < 32:
        return {b: None for b in bands}
    spec = np.abs(np.fft.rfft(seg * np.hanning(len(seg))))
    freqs = np.fft.rfftfreq(len(seg), 1 / sr)
    for lo, hi in bands:
        mask = (freqs >= lo) & (freqs < hi)
        if mask.sum() < 8:
            out[(lo, hi)] = None
            continue
        f_band, s_band = freqs[mask], spec[mask]
        prominence = 0.1 * (s_band.max() + 1e-20)
        peaks, _ = find_peaks(s_band, prominence=prominence)
        if len(peaks) < 2:
            out[(lo, hi)] = None
            continue
        out[(lo, hi)] = float(np.mean(np.diff(f_band[peaks])))
    return out


def modal_overlap(band_center_hz: float, decay_time_s, mean_spacing_hz):
    """Modal overlap M = modal -3dB bandwidth / mean mode spacing. Modal bandwidth follows from
    decay time via the standard relation Delta_f_3dB = 2.2 / T. M >= 1 is the classic "Schroeder"
    threshold for statistically dense; this module reports the M>=3 crossing (see
    modal_overlap_crossover_hz) as a more conservative "clearly dense, not just barely" point."""
    if not mean_spacing_hz or mean_spacing_hz <= 0 or not decay_time_s or decay_time_s <= 0:
        return None
    bandwidth_3db = 2.2 / decay_time_s
    return float(bandwidth_3db / mean_spacing_hz)


def modal_overlap_crossover_hz(x: np.ndarray, sr: int, window_s: tuple[float, float], decay_time_s,
                                bands=None, overlap_threshold: float = 3.0):
    """Lowest band's low edge at which modal overlap first reaches `overlap_threshold`, or None if
    it never does in the bands checked. This is the measurable analogue of the "Schroeder
    frequency" - report it, don't report the room-volume formula."""
    if bands is None:
        bands = octave_bands(sr=sr)
    spacing = modal_peak_spacing(x, sr, window_s, bands)
    for lo, hi in bands:
        center = (lo * hi) ** 0.5
        overlap = modal_overlap(center, decay_time_s, spacing.get((lo, hi)))
        if overlap is not None and overlap >= overlap_threshold:
            return float(lo)
    return None


# ---------------------------------------------------------------------------------------------
# Stereo. core.io.load_audio() mono-mixes everything, so these operate on the [n_channels, n]
# array from core.io.load_audio_channels() (or two separate 1-D channel arrays) - never on the
# mono mix, which would hide the property they exist to measure.
# ---------------------------------------------------------------------------------------------

def interchannel_correlation(l: np.ndarray, r: np.ndarray):
    """Zero-lag normalized cross-correlation. A fixed inter-channel delay can read as ~0 here
    while being highly correlated at its own lag - cross-check against iacc() below before
    concluding two channels are genuinely decorrelated, not just delayed relative to each other."""
    l = l - l.mean()
    r = r - r.mean()
    denom = np.linalg.norm(l) * np.linalg.norm(r)
    if denom <= 0:
        return 0.0
    return float(np.dot(l, r) / denom)


def iacc(l: np.ndarray, r: np.ndarray, sr: int, max_lag_ms: float = 1.0):
    """Max normalized cross-correlation over +-max_lag_ms (interaural cross-correlation, the
    standard measure this borrows its name from). The guard interchannel_correlation() alone
    can't provide: a signal delayed 0.5ms between channels reads as ~0 correlation at zero lag but
    ~1 here, correctly identifying it as still highly correlated rather than decorrelated."""
    l = l - l.mean()
    r = r - r.mean()
    max_lag = max(1, int(sr * max_lag_ms / 1000))
    full = np.correlate(l, r, mode="full")
    mid = len(l) - 1
    lo = max(0, mid - max_lag)
    hi = min(len(full), mid + max_lag + 1)
    window = full[lo:hi]
    denom = np.linalg.norm(l) * np.linalg.norm(r)
    if denom <= 0:
        return 0.0
    return float(np.max(np.abs(window)) / denom)


def dominant_lag_correlation(l: np.ndarray, r: np.ndarray, sr: int, max_lag_ms: float = 50.0):
    """Like iacc() but returns the (lag_ms, signed correlation) pair at whichever lag has the
    largest |correlation| within +-max_lag_ms, instead of just the magnitude at a fixed +-1ms
    window. Built for a wider net than iacc()'s own ear-inspired +-1ms: a real, unexpected finding
    on NonLin's own short-Time captures (see effects/nonlin/findings.md) needed this - their two
    channels read as near-zero at zero lag (interchannel_correlation) AND near-zero within iacc()'s
    own +-1ms window, yet are 97-98% correlated at a fixed ~2.5ms lag, well outside that window.
    Positive lag_ms means l's content arrives that many ms AFTER the matching content in r (l
    lags r, i.e. r leads); negative means the reverse (r lags l) - verified against a synthetic
    delayed-noise pair, not just reasoned from np.correlate()'s own convention."""
    l = l - l.mean()
    r = r - r.mean()
    max_lag = max(1, int(sr * max_lag_ms / 1000))
    full = np.correlate(l, r, mode="full")
    mid = len(l) - 1
    lo = max(0, mid - max_lag)
    hi = min(len(full), mid + max_lag + 1)
    window = full[lo:hi]
    denom = np.linalg.norm(l) * np.linalg.norm(r)
    if denom <= 0 or len(window) == 0:
        return 0.0, 0.0
    normed = window / denom
    peak_idx = int(np.argmax(np.abs(normed)))
    lag_ms = (lo + peak_idx - mid) / sr * 1000.0
    return float(lag_ms), float(normed[peak_idx])


def correlation_at_lag(l: np.ndarray, r: np.ndarray, sr: int, lag_ms: float) -> float:
    """Signed normalized cross-correlation at ONE specific lag (not a search over a window like
    dominant_lag_correlation/iacc) - same sign convention as dominant_lag_correlation (positive
    lag_ms means l lags r).

    Exists for comparing a render against a reference on a KNOWN, expected lag rather than each
    signal's own independently-found dominant peak: dominant_lag_correlation's own argmax search
    can lock onto an unrelated, coincidentally-stronger peak elsewhere in a wide search window
    (confirmed on two real NonLin renders at extreme negative-High settings, where the intended
    ~2.5ms-lag narrowing effect was correctly applied - and measurably close to target when
    checked AT that lag directly - but a stronger unrelated low-frequency periodicity elsewhere in
    +-50ms made dominant_lag_correlation report the wrong lag/sign for the summary metric). The
    fix for a comparison is to fix the lag from the REFERENCE's own measurement and evaluate both
    signals there, not let the render's own search wander."""
    lag_samples = int(round(lag_ms * sr / 1000.0))
    l = l - l.mean()
    r = r - r.mean()
    if lag_samples >= 0:
        a = l[lag_samples:]
        b = r[:len(r) - lag_samples] if lag_samples > 0 else r
    else:
        a = l[:len(l) + lag_samples]
        b = r[-lag_samples:]
    n = min(len(a), len(b))
    if n <= 0:
        return 0.0
    a, b = a[:n], b[:n]
    denom = np.linalg.norm(a) * np.linalg.norm(b)
    if denom <= 0:
        return 0.0
    return float(np.dot(a, b) / denom)


def band_interchannel_coherence(l: np.ndarray, r: np.ndarray, sr: int, bands=None):
    """Per-band scipy.signal.coherence, WITH the coherence noise floor for this capture's own
    length included as "_noise_floor" - two genuinely independent signals give approximately that
    floor (~1/sqrt(number of independent segments averaged)), not exactly zero. Without printing
    it, a small nonzero coherence reads as ambiguous between "slightly correlated" and "exactly
    what independent channels of this length produce"."""
    if bands is None:
        bands = octave_bands(sr=sr)
    nperseg = min(len(l), 2048)
    if nperseg < 16:
        return {"_noise_floor": None}
    f, cxy = _coherence(l, r, fs=sr, nperseg=nperseg)
    duration_s = len(l) / sr
    bandwidth_hz = sr / nperseg
    n_segments_equiv = max(1.0, duration_s * bandwidth_hz)
    noise_floor = float(1.0 / np.sqrt(n_segments_equiv))
    out = {"_noise_floor": round(noise_floor, 4)}
    for lo, hi in bands:
        mask = (f >= lo) & (f < hi)
        out[(round(lo, 1), round(hi, 1))] = float(np.mean(cxy[mask])) if mask.sum() else None
    return out


def mid_side_ratio_db(l: np.ndarray, r: np.ndarray):
    """20*log10(side_rms / mid_rms). 0dB = as much side energy as mid; very negative = mono-like."""
    mid = (l + r) / 2.0
    side = (l - r) / 2.0
    mid_rms = np.sqrt(np.mean(mid ** 2) + 1e-24)
    side_rms = np.sqrt(np.mean(side ** 2) + 1e-24)
    return float(20 * np.log10((side_rms + 1e-12) / (mid_rms + 1e-12)))


def stereo_gate_alignment(l: np.ndarray, r: np.ndarray, sr: int):
    """gate_envelope_params() run independently on each channel, plus the knee-time difference
    between them. Decides a model-architecture question: if knee_time_diff_ms stays small and
    consistent across every Time setting, one shared gate envelope over two decorrelated tanks is
    the right model; if it drifts with Time, the gate itself is per-channel and both channels'
    gate parameters need to be fit independently."""
    onset_l = find_onset(l, sr)
    onset_r = find_onset(r, sr)
    gate_l = gate_envelope_params(l, sr, onset_l)
    gate_r = gate_envelope_params(r, sr, onset_r)
    diff_ms = None
    if gate_l["knee_time_ms"] is not None and gate_r["knee_time_ms"] is not None:
        diff_ms = round(float(gate_l["knee_time_ms"] - gate_r["knee_time_ms"]), 3)
    return {"left": gate_l, "right": gate_r, "knee_time_diff_ms": diff_ms}


# ---------------------------------------------------------------------------------------------
# Tonal character
# ---------------------------------------------------------------------------------------------

def long_term_average_spectrum(x: np.ndarray, sr: int, bands_per_octave: int = 3):
    """Welch PSD, smoothed to `bands_per_octave` bands per octave (third-octave by default).
    Returns (band_center_hz, band_level_db), sorted ascending, one entry per populated band -
    not one entry per FFT bin, since the point is a smoothed tonal-character summary."""
    nperseg = min(len(x), 4096)
    if nperseg < 16:
        return np.array([]), np.array([])
    f, pxx = welch(x, fs=sr, nperseg=nperseg)
    mask = f > 0
    f, pxx = f[mask], pxx[mask]
    if len(f) == 0:
        return np.array([]), np.array([])
    bin_width_oct = 1.0 / bands_per_octave
    bin_idx = np.round(np.log2(f) / bin_width_oct).astype(int)
    uniq = np.unique(bin_idx)
    band_f = 2.0 ** (uniq * bin_width_oct)
    band_db = np.array([10 * np.log10(np.mean(pxx[bin_idx == b]) + 1e-20) for b in uniq])
    order = np.argsort(band_f)
    return band_f[order], band_db[order]


def spectral_centroid_over_time(x: np.ndarray, sr: int, win_ms: float = 20, hop_ms: float = 10):
    win = max(64, int(sr * win_ms / 1000))
    hop = max(32, int(sr * hop_ms / 1000))
    n_frames = max(0, (len(x) - win) // hop + 1)
    freqs = np.fft.rfftfreq(win, 1 / sr)
    times = np.empty(n_frames)
    centroids = np.full(n_frames, np.nan)
    for i in range(n_frames):
        seg = x[i * hop : i * hop + win] * np.hanning(win)
        mag = np.abs(np.fft.rfft(seg))
        total = mag.sum()
        times[i] = (i * hop + win / 2) / sr
        if total > 0:
            centroids[i] = float(np.sum(freqs * mag) / total)
    return times, centroids


def tilt_fit(freqs: np.ndarray, spec_a_db: np.ndarray, spec_b_db: np.ndarray, pivot_hz: float = 1000.0):
    """Fits Delta(f) = gain + slope*log2(f/pivot_hz) to (spec_a_db - spec_b_db) - e.g. two LTAS
    curves at different High settings - and solves for where Delta crosses zero. Independently
    re-derives a tilt's pivot frequency from measurement rather than assuming one.
    Returns (gain_db, slope_db_per_octave, zero_crossing_hz_or_None)."""
    freqs = np.asarray(freqs, dtype=float)
    diff = np.asarray(spec_a_db, dtype=float) - np.asarray(spec_b_db, dtype=float)
    mask = freqs > 0
    if mask.sum() < 3:
        return None, None, None
    log_f = np.log2(freqs[mask] / pivot_hz)
    slope, gain = np.polyfit(log_f, diff[mask], 1)
    zero_crossing_hz = float(pivot_hz * (2 ** (-gain / slope))) if slope != 0 else None
    return float(gain), float(slope), zero_crossing_hz


def resonant_peaks(x: np.ndarray, sr: int, band: tuple[float, float] = (20.0, 20000.0), max_peaks: int = 10):
    """Prominent narrow LTAS peaks with an estimated Q (center / -3dB bandwidth), inside `band`.
    A short gated tank plausibly has real comb/modal structure a smoothed tilt measurement alone
    wouldn't show - this looks for it directly."""
    f, spec_db = long_term_average_spectrum(x, sr, bands_per_octave=24)
    mask = (f >= band[0]) & (f <= band[1])
    if mask.sum() < 5:
        return []
    f_band, db_band = f[mask], spec_db[mask]
    peaks, _ = find_peaks(db_band, prominence=3.0)
    out = []
    for idx in peaks:
        f0, level = f_band[idx], db_band[idx]
        half = level - 3.0
        lo_i = idx
        while lo_i > 0 and db_band[lo_i] > half:
            lo_i -= 1
        hi_i = idx
        while hi_i < len(db_band) - 1 and db_band[hi_i] > half:
            hi_i += 1
        bandwidth = f_band[hi_i] - f_band[lo_i] if hi_i > lo_i else None
        q = float(f0 / bandwidth) if bandwidth and bandwidth > 0 else None
        out.append({"freq_hz": round(float(f0), 1), "level_db": round(float(level), 2),
                     "q": round(q, 2) if q is not None else None})
    out.sort(key=lambda d: -d["level_db"])
    return out[:max_peaks]


def early_reflection_taps(x: np.ndarray, sr: int, onset_idx: int, window_ms: tuple[float, float] = (0, 150), max_taps: int = 12):
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
        t_ms = idx / sr * 1000
        gain_db = db(np.array([seg[idx]]))[0]
        taps.append({"time_ms": round(float(t_ms), 3), "gain_db": round(float(gain_db), 2)})
    taps.sort(key=lambda d: d["time_ms"])
    return taps


def analyze_capture(x: np.ndarray, sr: int) -> dict:
    """Effect-agnostic feature extraction over one capture's raw samples. Pure computation, no
    file I/O or plotting (both are the caller's concern - see each effect's own analyze.py)."""
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

    rt60, slope, r2, _sch_t, _sch_curve = schroeder_rt60(x, sr, onset_idx)
    full_rt60, full_slope, full_r2 = full_decay_rt60(x, sr, onset_idx)

    rms_idxs, rms_db = rms_envelope_db(x, sr)
    rms_t = rms_idxs / sr
    rms_db_norm = rms_db - np.max(rms_db)
    below60 = np.where(rms_db_norm <= -60)[0]
    rms_time_to_minus60db_s = float(rms_t[below60[0]] - onset_time_s) if below60.size else None

    tilt_t, tilt_db = spectral_tilt_over_time(x, sr)
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

    return {
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
