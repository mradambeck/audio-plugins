#!/usr/bin/env python3
"""Offline fit for ShieldsFDNEngine's fixed output-stage EQ (lowShelf/highShelf/midPeak constants
in Source/ShieldsFDNEngine.h) against the tonal gap analysis/validate.py measures.

Why this can be done offline, with no rebuild loop: the three shelf/peak stages are LTI (linear
time-invariant) biquads applied unconditionally to the wet output, after everything nonlinear
(bit-depth quantization) - see ShieldsFDNEngine::processStereo(). Changing them shifts the render's
long-term average spectrum by EXACTLY the difference between the old and new EQ's own magnitude
responses, in dB, at every frequency - nothing else in the signal path needs to change or be
re-rendered to predict the effect of a different set of shelf/peak constants. So the fit target is
computed directly from validate.py's own current-defaults render:

    target_eq_response_db(f) = current_eq_response_db(f) - avg_ltas_error_db(f)

(avg_ltas_error_db = render-minus-reference, averaged across both presets - see below). If the new
EQ hits that target response exactly, avg_ltas_error_db becomes 0 at every fitted frequency, i.e.
the tonal gap closes.

The Biquad formulas below are transcribed directly from ShieldsFDNEngine.cpp's own
setLowShelf/setHighShelf/setPeak (RBJ Audio EQ Cookbook, shelf slope S=1) - NOT re-derived - so the
predicted response here matches what the real C++ engine will actually produce bit-for-bit modulo
float32-vs-float64 rounding.

Usage: run after analysis/validate.py (needs its validation_results.json for the current error
curve). Prints the fitted constants to paste into ShieldsFDNEngine.h, plus a residual-error
sanity check.
"""
from __future__ import annotations

import json
import os
import sys

import numpy as np
from scipy.optimize import least_squares

HERE = os.path.dirname(__file__)
sys.path.insert(0, os.path.join(HERE, "..", "..", "..", "ml-toolkit"))
sys.path.insert(0, os.path.join(HERE, "..", "..", "common", "tools"))

from core.features import long_term_average_spectrum  # noqa: E402
import compare_wavs as cw  # noqa: E402

REFERENCE_DIR = os.path.join(HERE, "..", "reference-irs")
RENDER_PATH = os.path.join(HERE, "validation_renders", "defaults.wav")
REFERENCES = ["preset-45.wav", "preset-49.wav"]

FIT_FREQ_MIN_HZ = 60.0
FIT_FREQ_MAX_HZ = 16000.0

# Current constants, copied from ShieldsFDNEngine.h - the fit's starting point AND the response
# the target is computed relative to (see module docstring).
CURRENT_LOW_SHELF = {"freq_hz": 350.0, "gain_db": 7.0}
CURRENT_HIGH_SHELF = {"freq_hz": 7000.0, "gain_db": -5.0}
CURRENT_MID_PEAK = {"freq_hz": 1200.0, "gain_db": 0.0, "q": 0.7}
SAMPLE_RATE_HZ = 44100.0  # matches ShieldsRenderIR's own default; shelf freqs are well under Nyquist


def biquad_low_shelf_response_db(freqs_hz: np.ndarray, freq_hz: float, gain_db: float, sr: float) -> np.ndarray:
    """RBJ low-shelf, S=1 - transcribed from ShieldsFDNEngine::Biquad::setLowShelf()."""
    A = 10.0 ** (gain_db / 40.0)
    w0 = 2.0 * np.pi * freq_hz / sr
    cosw0, sinw0 = np.cos(w0), np.sin(w0)
    alpha = sinw0 * 0.5 * np.sqrt((A + 1.0 / A) * (1.0 / 1.0 - 1.0) + 2.0)
    two_sqrtA_alpha = 2.0 * np.sqrt(A) * alpha

    b0 = A * ((A + 1.0) - (A - 1.0) * cosw0 + two_sqrtA_alpha)
    b1 = 2.0 * A * ((A - 1.0) - (A + 1.0) * cosw0)
    b2 = A * ((A + 1.0) - (A - 1.0) * cosw0 - two_sqrtA_alpha)
    a0 = (A + 1.0) + (A - 1.0) * cosw0 + two_sqrtA_alpha
    a1 = -2.0 * ((A - 1.0) + (A + 1.0) * cosw0)
    a2 = (A + 1.0) + (A - 1.0) * cosw0 - two_sqrtA_alpha
    return _biquad_response_db(freqs_hz, b0, b1, b2, a0, a1, a2, sr)


def biquad_high_shelf_response_db(freqs_hz: np.ndarray, freq_hz: float, gain_db: float, sr: float) -> np.ndarray:
    """RBJ high-shelf, S=1 - transcribed from ShieldsFDNEngine::Biquad::setHighShelf()."""
    A = 10.0 ** (gain_db / 40.0)
    w0 = 2.0 * np.pi * freq_hz / sr
    cosw0, sinw0 = np.cos(w0), np.sin(w0)
    alpha = sinw0 * 0.5 * np.sqrt((A + 1.0 / A) * (1.0 / 1.0 - 1.0) + 2.0)
    two_sqrtA_alpha = 2.0 * np.sqrt(A) * alpha

    b0 = A * ((A + 1.0) + (A - 1.0) * cosw0 + two_sqrtA_alpha)
    b1 = -2.0 * A * ((A - 1.0) + (A + 1.0) * cosw0)
    b2 = A * ((A + 1.0) + (A - 1.0) * cosw0 - two_sqrtA_alpha)
    a0 = (A + 1.0) - (A - 1.0) * cosw0 + two_sqrtA_alpha
    a1 = 2.0 * ((A - 1.0) - (A + 1.0) * cosw0)
    a2 = (A + 1.0) - (A - 1.0) * cosw0 - two_sqrtA_alpha
    return _biquad_response_db(freqs_hz, b0, b1, b2, a0, a1, a2, sr)


def biquad_peak_response_db(freqs_hz: np.ndarray, freq_hz: float, gain_db: float, q: float, sr: float) -> np.ndarray:
    """RBJ peaking EQ (bell) - transcribed from ShieldsFDNEngine::Biquad::setPeak()."""
    A = 10.0 ** (gain_db / 40.0)
    w0 = 2.0 * np.pi * freq_hz / sr
    cosw0 = np.cos(w0)
    alpha = np.sin(w0) / (2.0 * q)

    b0 = 1.0 + alpha * A
    b1 = -2.0 * cosw0
    b2 = 1.0 - alpha * A
    a0 = 1.0 + alpha / A
    a1 = -2.0 * cosw0
    a2 = 1.0 - alpha / A
    return _biquad_response_db(freqs_hz, b0, b1, b2, a0, a1, a2, sr)


def _biquad_response_db(freqs_hz: np.ndarray, b0, b1, b2, a0, a1, a2, sr: float) -> np.ndarray:
    """|H(e^jw)| in dB for a normalized (b/a0, a/a0) direct-form-1 biquad, evaluated at freqs_hz."""
    b0, b1, b2 = b0 / a0, b1 / a0, b2 / a0
    a1n, a2n = a1 / a0, a2 / a0
    w = 2.0 * np.pi * freqs_hz / sr
    z1 = np.exp(-1j * w)
    num = b0 + b1 * z1 + b2 * z1 ** 2
    den = 1.0 + a1n * z1 + a2n * z1 ** 2
    return 20.0 * np.log10(np.abs(num / den) + 1e-12)


def cascaded_eq_response_db(freqs_hz: np.ndarray, low_shelf: dict, high_shelf: dict, mid_peak: dict, sr: float) -> np.ndarray:
    """Cascading biquads MULTIPLIES their linear responses, i.e. ADDS their dB responses -
    matches ShieldsFDNEngine::processStereo()'s own lowShelf -> highShelf -> midPeak chain order
    (order doesn't affect the magnitude response of a cascade, only phase, which this fit doesn't
    need)."""
    return (
        biquad_low_shelf_response_db(freqs_hz, low_shelf["freq_hz"], low_shelf["gain_db"], sr)
        + biquad_high_shelf_response_db(freqs_hz, high_shelf["freq_hz"], high_shelf["gain_db"], sr)
        + biquad_peak_response_db(freqs_hz, mid_peak["freq_hz"], mid_peak["gain_db"], mid_peak["q"], sr)
    )


def measure_avg_ltas_error_db() -> tuple[np.ndarray, np.ndarray]:
    """RMS-matched, 1/3-octave-smoothed LTAS diff (render minus reference), averaged across both
    presets - same convention as validate.py's own ltas_band_levels(), but at long_term_average_
    spectrum()'s own natural band centers (bands_per_octave=3) rather than validate.py's coarser
    8-band grouping, since the fit needs a usably fine frequency axis, not a human-readable table.
    Returns (freqs_hz, avg_error_db), restricted to [FIT_FREQ_MIN_HZ, FIT_FREQ_MAX_HZ]."""
    render_x_native, sr_render_native = cw.load_mono(RENDER_PATH)
    errors = []
    common_freqs = None
    for name in REFERENCES:
        ref_x_native, sr_ref_native = cw.load_mono(os.path.join(REFERENCE_DIR, name))
        sr = max(sr_render_native, sr_ref_native)
        render_x = cw.resample_to(render_x_native, sr_render_native, sr)
        ref_x = cw.resample_to(ref_x_native, sr_ref_native, sr)

        onset_r = cw.find_onset(render_x)
        onset_ref = cw.find_onset(ref_x)
        render_trim = render_x[onset_r:]
        ref_trim = ref_x[onset_ref:]

        render_rms = float(np.sqrt(np.mean(render_trim.astype(np.float64) ** 2)))
        ref_rms = float(np.sqrt(np.mean(ref_trim.astype(np.float64) ** 2)))
        level_matched = render_trim * (ref_rms / render_rms) if render_rms > 0 else render_trim

        f_r, db_r = long_term_average_spectrum(level_matched, sr, bands_per_octave=3)
        f_ref, db_ref = long_term_average_spectrum(ref_trim, sr, bands_per_octave=3)
        # Both signals are at the same (resampled) sr and go through the identical
        # long_term_average_spectrum() band-center formula, so their band centers already match
        # exactly - no interpolation needed, just a length/identity sanity check.
        assert len(f_r) == len(f_ref) and np.allclose(f_r, f_ref), "band centers diverged unexpectedly"
        mask = (f_r >= FIT_FREQ_MIN_HZ) & (f_r <= FIT_FREQ_MAX_HZ)
        if common_freqs is None:
            common_freqs = f_r[mask]
        errors.append((db_r - db_ref)[mask])

    return common_freqs, np.mean(errors, axis=0)


def find_outlier_bands(avg_error_db: np.ndarray, threshold_db: float = 8.0) -> np.ndarray:
    """Boolean mask (True = outlier) for bands whose |error| exceeds threshold_db outright - meant
    to catch a narrow resonant spike, not a real broadband tonal-balance gap. Found necessary
    directly: an unweighted/soft_l1-weighted fit both still let the ~81-102Hz spike (+13.6dB/
    +9.6dB, almost certainly the SAME resonant low-band peak clustering resonant_peak_summary()
    already measures at 96-362Hz - Step 4's problem, not a broadband EQ's) visibly drag several
    perfectly-fine neighboring bands measurably worse just by being in the target at all - a
    smooth shelf/peak EQ fundamentally cannot cancel a narrow resonance without also disturbing
    everything around it.

    A plain magnitude threshold, not a local-median deviation check: an earlier version compared
    each band against a rolling median of its neighbors, which INCORRECTLY flagged 256Hz/406Hz too
    - the 150-650Hz region has a real, legitimate alternating-sign comb-filtering ripple (-1.76,
    -0.30, +3.26, -3.14, +3.11, +4.34, -3.88...) that a 5-point local median is too noisy to tell
    apart from a genuine outlier. threshold_db=8.0 was picked by looking at the sorted |error|
    list directly: 81Hz/102Hz (13.64dB/9.63dB) sit in a class of their own, decisively separated
    from the NEXT largest error anywhere in the spectrum (813Hz at 6.31dB) - not a threshold tuned
    to hit a target count."""
    return np.abs(avg_error_db) > threshold_db


def main() -> None:
    if not os.path.exists(RENDER_PATH):
        print(f"No render at {RENDER_PATH} - run analysis/validate.py first.")
        return

    freqs, avg_error_db = measure_avg_ltas_error_db()
    current_response_db = cascaded_eq_response_db(freqs, CURRENT_LOW_SHELF, CURRENT_HIGH_SHELF, CURRENT_MID_PEAK, SAMPLE_RATE_HZ)
    target_response_db = current_response_db - avg_error_db

    outlier_mask = find_outlier_bands(avg_error_db)
    if outlier_mask.any():
        print(f"Excluding {outlier_mask.sum()} outlier band(s) from the fit target (not from the "
              f"report): " + ", ".join(f"{f:.0f}Hz ({e:+.2f}dB)" for f, e in zip(freqs[outlier_mask], avg_error_db[outlier_mask])))
    fit_freqs = freqs[~outlier_mask]
    fit_target_response_db = target_response_db[~outlier_mask]

    print(f"Fitting over {len(fit_freqs)} of {len(freqs)} bands, {freqs[0]:.0f}Hz-{freqs[-1]:.0f}Hz")
    print(f"Current avg |LTAS error|: {np.mean(np.abs(avg_error_db)):.2f}dB (max {np.max(np.abs(avg_error_db)):.2f}dB)")

    # Free params: [low_freq, low_gain, high_freq, high_gain, mid_freq, mid_gain, mid_q].
    # Bounds picked from what a broadband output-stage correction should plausibly need, not from
    # the fit's own preferred optimum - the mid peak's lower gain bound is 0 (not negative) since
    # the gap it's fitted against is a BROAD DIP needing a boost, not a narrow resonance needing a
    # cut, and letting it go negative would just let the optimizer fight the shelves instead of
    # complementing them.
    x0 = np.array([
        CURRENT_LOW_SHELF["freq_hz"], CURRENT_LOW_SHELF["gain_db"],
        CURRENT_HIGH_SHELF["freq_hz"], CURRENT_HIGH_SHELF["gain_db"],
        CURRENT_MID_PEAK["freq_hz"], CURRENT_MID_PEAK["gain_db"] + 0.01, CURRENT_MID_PEAK["q"],
    ])
    # A manual in-C++ sweep (see ShieldsFDNEngine.h's own comment on this stage) found activating
    # this mid peak AT ALL - regardless of its center frequency (700-2500Hz all tested), gain (2dB
    # and up), or Q - costs the render's broadband RMS envelope's PEAK TIMING a near-constant
    # ~0.055-0.06s (confirmed via analysis/validate.py's own peak_time_error_s, before vs. after):
    # it tips which of the burst bank's several close-competing local maxima wins the argmax
    # contest, something this offline LTI fit has no visibility into at all (it only ever sees the
    # time-AVERAGED spectrum). Since the cost is constant regardless of frequency, constraining this
    # bound would only make the SPECTRAL fit worse for no timing benefit - left at its original,
    # only lightly-opinionated range instead. See that same comment for why the timing cost is
    # accepted rather than chased further.
    lower = np.array([100.0, 0.0, 3000.0, -12.0, 400.0, 0.0, 0.3])
    upper = np.array([800.0, 14.0, 14000.0, 6.0, 3000.0, 10.0, 2.0])

    def residuals(x):
        low_shelf = {"freq_hz": x[0], "gain_db": x[1]}
        high_shelf = {"freq_hz": x[2], "gain_db": x[3]}
        mid_peak = {"freq_hz": x[4], "gain_db": x[5], "q": x[6]}
        predicted = cascaded_eq_response_db(fit_freqs, low_shelf, high_shelf, mid_peak, SAMPLE_RATE_HZ)
        return predicted - fit_target_response_db

    # x_scale matters here: freq (100s-1000s Hz), gain (single-digit dB) and Q (0.3-2) span wildly
    # different magnitudes, and without an explicit per-parameter scale the default Jacobian-based
    # scaling converged to a measurably worse point (mean |residual| 3.86dB -> 6.50dB, i.e. WORSE
    # than the unfitted starting point) - caught by a direct debug comparison of residual norms at
    # x0 vs. result.x, which is what any future change to this fit should re-check if the reported
    # "after" number ever looks suspiciously worse than "before" again.
    # A plain L2 fit (loss="linear") chased the narrow ~81-102Hz spike in avg_error_db (+13.6dB/
    # +9.6dB - almost certainly the same resonant low-band peak clustering resonant_peak_summary()
    # already measures, per this plugin's own known "peaks cluster at 96-362Hz" finding, not a real
    # broadband tonal gap a smooth shelf/peak filter could ever fix) at the direct expense of
    # several NEARBY, previously-fine bands (64/128/161Hz all got measurably worse chasing it,
    # confirmed by comparing this script's own before/after per-band printout with loss="linear").
    # soft_l1 (a standard robust loss) downweights residuals beyond f_scale quadratically-then-
    # linearly instead of chasing them at full weight - f_scale picked at roughly this fit's own
    # "normal" residual magnitude (see the un-fitted avg_error_db's own inter-quartile spread),
    # so genuinely broadband bands still drive the fit while the one narrow outlier doesn't.
    x_scale = np.array([300.0, 7.0, 3000.0, 6.0, 1000.0, 5.0, 0.5])
    result = least_squares(residuals, x0, bounds=(lower, upper), x_scale=x_scale, loss="soft_l1", f_scale=3.0)

    # Sign-check on the RAW (unrounded) optimizer output before anything gets rounded for display -
    # avg_error_db + (predicted - current_response_db) must equal residuals(x) exactly (evaluated
    # over the same fit_freqs domain residuals() itself uses, i.e. outliers excluded), since they're
    # the same formula rearranged (target_response_db = current_response_db - avg_error_db). See the
    # x_scale comment above for the real bug this class of check caught previously.
    raw_predicted_fit_domain = cascaded_eq_response_db(fit_freqs, {"freq_hz": result.x[0], "gain_db": result.x[1]},
                                                         {"freq_hz": result.x[2], "gain_db": result.x[3]},
                                                         {"freq_hz": result.x[4], "gain_db": result.x[5], "q": result.x[6]},
                                                         SAMPLE_RATE_HZ)
    current_response_fit_domain = current_response_db[~outlier_mask]
    avg_error_fit_domain = avg_error_db[~outlier_mask]
    assert np.allclose(avg_error_fit_domain + (raw_predicted_fit_domain - current_response_fit_domain), residuals(result.x)), "sign-check failed"
    fitted = {
        "low_shelf": {"freq_hz": round(float(result.x[0]), 1), "gain_db": round(float(result.x[1]), 2)},
        "high_shelf": {"freq_hz": round(float(result.x[2]), 1), "gain_db": round(float(result.x[3]), 2)},
        "mid_peak": {"freq_hz": round(float(result.x[4]), 1), "gain_db": round(float(result.x[5]), 2), "q": round(float(result.x[6]), 3)},
    }

    print("\nFitted constants (paste into ShieldsFDNEngine.h):")
    print(f"  lowShelfFreqHz  = {fitted['low_shelf']['freq_hz']}f   (was {CURRENT_LOW_SHELF['freq_hz']})")
    print(f"  lowShelfGainDb  = {fitted['low_shelf']['gain_db']}f   (was {CURRENT_LOW_SHELF['gain_db']})")
    print(f"  highShelfFreqHz = {fitted['high_shelf']['freq_hz']}f   (was {CURRENT_HIGH_SHELF['freq_hz']})")
    print(f"  highShelfGainDb = {fitted['high_shelf']['gain_db']}f   (was {CURRENT_HIGH_SHELF['gain_db']})")
    print(f"  midPeakFreqHz   = {fitted['mid_peak']['freq_hz']}f   (was {CURRENT_MID_PEAK['freq_hz']})")
    print(f"  midPeakGainDb   = {fitted['mid_peak']['gain_db']}f   (was {CURRENT_MID_PEAK['gain_db']})")
    print(f"  midPeakQ        = {fitted['mid_peak']['q']}f   (was {CURRENT_MID_PEAK['q']})")

    # new_render_LTAS - ref_LTAS = (pre_eq_LTAS + new_response) - (pre_eq_LTAS + current_response -
    # avg_error_db) = avg_error_db + (new_response - current_response) - i.e. PLUS, not minus (an
    # earlier version of this had the sign backwards and reported a bogus "got worse" result - see
    # the x_scale comment and the sign-check assert above, which verified this same formula against
    # the UNROUNDED optimizer output). Recomputed here from the ROUNDED fitted constants (what
    # actually gets pasted into the C++ header), so this number may drift a hundredth of a dB or so
    # from the assert above - that's rounding, not a bug.
    predicted_new_response = cascaded_eq_response_db(
        freqs, fitted["low_shelf"], fitted["high_shelf"], fitted["mid_peak"], SAMPLE_RATE_HZ
    )
    predicted_new_error = avg_error_db + (predicted_new_response - current_response_db)
    print(f"\nPredicted avg |LTAS error| after this EQ: {np.mean(np.abs(predicted_new_error)):.2f}dB "
          f"(max {np.max(np.abs(predicted_new_error)):.2f}dB) - vs. {np.mean(np.abs(avg_error_db)):.2f}dB now")

    with open(os.path.join(HERE, "fit_output_eq_result.json"), "w") as fh:
        json.dump({
            "fitted": fitted,
            "freqs_hz": freqs.tolist(),
            "avg_error_db_before": avg_error_db.tolist(),
            "predicted_avg_error_db_after": predicted_new_error.tolist(),
        }, fh, indent=2)
    print(f"\nWrote {os.path.join(HERE, 'fit_output_eq_result.json')}")


if __name__ == "__main__":
    main()
