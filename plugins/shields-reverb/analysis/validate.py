#!/usr/bin/env python3
"""Re-tuning validation. Renders the actual ShieldsAudioProcessor (via ShieldsRenderIR, not a
re-implementation) at its current defaults, then compares against both real Midiverb II captures
in ../reference-irs/ using ml-toolkit's core/features.py measurements PLUS ../../common/tools/
compare_wavs.py's own envelope-correlation/log-spectral-distance pair (kept so this report stays
comparable with the historical "~0.94 correlation" figure the README/PluginProcessor comments
already cite).

Shields only has two reference captures (not a knob-swept grid the way Ambience/NonLin do), both
at the plugin's own tuned defaults - so there's no per-setting render loop here, just one render
compared against both files.

Why this exists on top of compare_wavs.py alone: an ml-toolkit diagnostic pass (see this repo's own
scratch analysis) found real gaps that envelope correlation and broadband log-spectral distance
don't surface -

  - **Decay rate**: envelope correlation can score two exponential-ish decays with different TIME
    CONSTANTS fairly well if their overall shape still tracks loosely - it does not directly compare
    slope. Measured directly here as a linear fit to the RMS envelope over a fixed 1.0-3.0s window
    (NOT full_decay_rt60's automatic -6..-70dB window - see below).
  - **Attack/peak timing**: neither compare_wavs.py metric isolates WHEN the buildup peaks.
  - **Per-band tonal balance**: compare_wavs.py's 3-band split (Low/Mid/High) is too coarse to show
    that the real gap is a gradient bottoming out at 500Hz-1kHz, not a flat "mid" cut - measured here
    at 8 finer 1/3-octave-smoothed bands via core.features.long_term_average_spectrum.
  - **Resonant peak character**: ShieldsFDNEngine.h's own tuning comments describe a historical
    "peaks up to 30-50dB above the noise floor" finding (from before the mutually-prime-ms fix was
    applied) - measured here as each render's resonant_peaks() height above its OWN LTAS median (a
    crude floor estimate), so the two engines' absolute levels don't need matching first.

IMPORTANT CAVEAT, found while building this: both reference captures fall off a cliff in their last
~200-300ms (preset-45: -35dB at 3.25s to -51dB at the file's own last-300ms average; preset-49
similar) that is far steeper than the decay rate everywhere else in the file - almost certainly a
capture trim/fade artifact, not the real hardware's own tail actually stopping that abruptly (see
../reference-irs/README.md, which already asks for exactly this kind of quirk to be noted). Every
decay-rate measurement in this script is windowed to end at 3.0s specifically to stay clear of it -
core.features.full_decay_rt60's own automatic -6..-70dB window is NOT used here for that reason (it
would fit straight into the cliff on the reference side).

Requires a built ShieldsRenderIR (../build/ShieldsRenderIR_artefacts/Release/ShieldsRenderIR -
build it first, console target only, per this repo's build-installs-silently convention: never
build the _AU/_VST3 targets just to check something).
"""
from __future__ import annotations

import json
import os
import subprocess
import sys

import numpy as np

HERE = os.path.dirname(__file__)
sys.path.insert(0, os.path.join(HERE, "..", "..", "..", "ml-toolkit"))
sys.path.insert(0, os.path.join(HERE, "..", "..", "common", "tools"))

from core.features import long_term_average_spectrum, resonant_peaks, rms_envelope_db  # noqa: E402
import compare_wavs as cw  # noqa: E402

RENDER_IR_BIN = os.path.join(HERE, "..", "build", "ShieldsRenderIR_artefacts", "Release", "ShieldsRenderIR")
REFERENCE_DIR = os.path.join(HERE, "..", "reference-irs")
RENDER_DIR = os.path.join(HERE, "validation_renders")
REPORT_PATH = os.path.join(HERE, "validation_report.md")
RESULTS_PATH = os.path.join(HERE, "validation_results.json")

REFERENCES = ["preset-45.wav", "preset-49.wav"]

# The window a decay-rate linear fit is taken over, in seconds post-onset. Lower bound skips the
# burst-comb attack (still rising/settling, not yet in the tank's own decay regime); upper bound
# stays clear of both references' own end-of-capture cliff (see module docstring).
DECAY_FIT_WINDOW_S = (1.0, 3.0)

# 1/3-octave-ish bands, finer than compare_wavs.py's own 3-band split - see module docstring for
# why the coarser split hid where the tonal gap actually sits.
LTAS_BANDS = [
    (20, 125, "20-125Hz"), (125, 250, "125-250Hz"), (250, 500, "250-500Hz"),
    (500, 1000, "500-1000Hz"), (1000, 2000, "1000-2000Hz"), (2000, 4000, "2000-4000Hz"),
    (4000, 8000, "4000-8000Hz"), (8000, 16000, "8000-16000Hz"),
]

os.makedirs(RENDER_DIR, exist_ok=True)


def render_defaults() -> str:
    """Renders ShieldsRenderIR with NO parameter flags at all, so it captures whatever
    PluginProcessor::createParameterLayout() actually defaults to (see Source/Tools/RenderIR.cpp's
    own setParamIfPresent() comment - it used to hardcode a second copy of every default, which
    could silently drift from the real one)."""
    out_path = os.path.join(RENDER_DIR, "defaults.wav")
    subprocess.run([RENDER_IR_BIN, "--out", out_path, "--seconds", "4.0"], check=True, capture_output=True)
    return out_path


def signed_error(rendered, reference):
    if rendered is None or reference is None:
        return None
    return round(rendered - reference, 4)


def decay_slope_db_per_s(x: np.ndarray, sr: int, onset_idx: int, window_s: tuple[float, float]) -> float | None:
    idxs, rms_db = rms_envelope_db(x[onset_idx:], sr)
    if len(rms_db) == 0:
        return None
    t = idxs / sr
    rel = rms_db - np.max(rms_db)
    mask = (t >= window_s[0]) & (t <= window_s[1])
    if mask.sum() < 5:
        return None
    slope, _ = np.polyfit(t[mask], rel[mask], 1)
    return float(slope)


def peak_timing(x: np.ndarray, sr: int, onset_idx: int) -> dict:
    idxs, rms_db = rms_envelope_db(x[onset_idx:], sr)
    if len(rms_db) == 0:
        return {"peak_time_s": None, "level_at_0.5s_db": None, "level_at_1.0s_db": None}
    t = idxs / sr
    rel = rms_db - np.max(rms_db)
    peak_time_s = float(t[np.argmax(rel)])
    return {
        "peak_time_s": round(peak_time_s, 4),
        "level_at_0.5s_db": round(float(np.interp(0.5, t, rel)), 2) if t[-1] >= 0.5 else None,
        "level_at_1.0s_db": round(float(np.interp(1.0, t, rel)), 2) if t[-1] >= 1.0 else None,
    }


def ltas_band_levels(x: np.ndarray, sr: int) -> dict:
    f, db = long_term_average_spectrum(x, sr, bands_per_octave=3)
    out = {}
    for lo, hi, name in LTAS_BANDS:
        mask = (f >= lo) & (f < hi)
        out[name] = float(np.mean(db[mask])) if mask.sum() else None
    return out


def resonant_peak_summary(x: np.ndarray, sr: int) -> tuple[float | None, float | None]:
    """Returns (max_db_above_own_floor, floor_db) - see module docstring on why "above own floor"
    rather than resonant_peaks()'s own raw absolute level_db."""
    f, db = long_term_average_spectrum(x, sr, bands_per_octave=24)
    if len(db) == 0:
        return None, None
    floor = float(np.median(db))
    peaks = resonant_peaks(x, sr)
    if not peaks:
        return None, floor
    return max(p["level_db"] - floor for p in peaks), floor


def compare_one(render_path: str, reference_path: str) -> dict:
    render_x, sr_r_native = cw.load_mono(render_path)
    ref_x, sr_ref_native = cw.load_mono(reference_path)

    # Resample both to a common rate BEFORE any comparison - matches compare_wavs.py's own CLI
    # convention (main() does the identical `targetRate = max(...)` resample). Shields's reference
    # captures are natively 48kHz while ShieldsRenderIR renders at 44.1kHz; every function below
    # takes a SINGLE sample_rate argument, so feeding it two differently-rated arrays under one
    # nominal rate silently mis-scales every frequency-domain measurement on whichever side wasn't
    # actually at that rate (Welch PSD bins off by ~9%, RMS window length off by the same amount) -
    # found while building the fit_output_eq.py fit, which is exactly the step most sensitive to an
    # accurate frequency axis. Every function from here on uses this single `sr`.
    sr = max(sr_r_native, sr_ref_native)
    render_x = cw.resample_to(render_x, sr_r_native, sr)
    ref_x = cw.resample_to(ref_x, sr_ref_native, sr)

    onset_r = cw.find_onset(render_x)
    onset_ref = cw.find_onset(ref_x)
    render_trim = render_x[onset_r:]
    ref_trim = ref_x[onset_ref:]

    # compare_wavs.py's own two headline metrics, computed the identical way its CLI does (RMS
    # envelope correlation is scale-invariant already; LSD needs the level match compare_wavs.py's
    # own main() applies before any spectral comparison).
    window_size = max(1, int(sr * 50.0 / 1000.0))
    rms_r = cw.windowed_rms(render_trim, window_size)
    rms_ref = cw.windowed_rms(ref_trim, window_size)
    envelope_correlation = cw.envelope_correlation(rms_r, rms_ref)

    render_rms_full = float(np.sqrt(np.mean(render_trim.astype(np.float64) ** 2)))
    ref_rms_full = float(np.sqrt(np.mean(ref_trim.astype(np.float64) ** 2)))
    level_matched = render_trim * (ref_rms_full / render_rms_full) if render_rms_full > 0 else render_trim

    raw_freqs, psd_r, psd_ref = cw.averaged_psd(level_matched, ref_trim, sr)
    spec_freqs, spec_db_r = cw.smooth_to_fractional_octave(raw_freqs, psd_r)
    _, spec_db_ref = cw.smooth_to_fractional_octave(raw_freqs, psd_ref)
    lsd = cw.log_spectral_distance(spec_db_r, spec_db_ref) if len(spec_db_r) else None

    # New measurements (see module docstring for why each one exists).
    decay_r = decay_slope_db_per_s(render_x, sr, onset_r, DECAY_FIT_WINDOW_S)
    decay_ref = decay_slope_db_per_s(ref_x, sr, onset_ref, DECAY_FIT_WINDOW_S)

    timing_r = peak_timing(render_x, sr, onset_r)
    timing_ref = peak_timing(ref_x, sr, onset_ref)

    ltas_r = ltas_band_levels(level_matched, sr)
    ltas_ref = ltas_band_levels(ref_trim, sr)
    band_errors = {
        name: {"rendered": ltas_r.get(name), "reference": ltas_ref.get(name),
               "error": signed_error(ltas_r.get(name), ltas_ref.get(name))}
        for _, _, name in LTAS_BANDS
    }

    peak_height_r, floor_r = resonant_peak_summary(render_trim, sr)
    peak_height_ref, floor_ref = resonant_peak_summary(ref_trim, sr)

    return {
        "reference": os.path.basename(reference_path),
        "envelope_correlation": round(envelope_correlation, 4),
        "log_spectral_distance_db": round(lsd, 2) if lsd is not None else None,
        "decay_slope_db_per_s_rendered": round(decay_r, 2) if decay_r is not None else None,
        "decay_slope_db_per_s_reference": round(decay_ref, 2) if decay_ref is not None else None,
        "decay_slope_error_db_per_s": signed_error(decay_r, decay_ref),
        "peak_time_s_rendered": timing_r["peak_time_s"],
        "peak_time_s_reference": timing_ref["peak_time_s"],
        "peak_time_error_s": signed_error(timing_r["peak_time_s"], timing_ref["peak_time_s"]),
        "level_at_0.5s_error_db": signed_error(timing_r["level_at_0.5s_db"], timing_ref["level_at_0.5s_db"]),
        "level_at_1.0s_error_db": signed_error(timing_r["level_at_1.0s_db"], timing_ref["level_at_1.0s_db"]),
        "band_errors": band_errors,
        "resonant_peak_height_db_rendered": round(peak_height_r, 2) if peak_height_r is not None else None,
        "resonant_peak_height_db_reference": round(peak_height_ref, 2) if peak_height_ref is not None else None,
        "resonant_peak_height_error_db": signed_error(peak_height_r, peak_height_ref),
    }


# Interpretation thresholds - flags a concerning aggregate value explicitly in the report rather
# than leaving it sitting in a table. Picked from the magnitude of the real, currently-open gaps
# this validation script was built to characterize (see the diagnostic pass that preceded this
# script and the plan it fed into), meant to catch a comparably-sized regression - not a formal
# statistical bound, and not yet "certified accurate" the moment every flag clears.
CONCERN_THRESHOLDS: list[tuple[str, str, float, str]] = [
    ("Decay slope error (dB/s)", "decay_slope_error_db_per_s", 1.5,
     "the render's 1.0-3.0s decay rate diverges from the real hardware's own - a positive value "
     "means the render decays MORE SLOWLY (tail rings longer) than the real unit. Threshold set "
     "just below the ~1.86-2.04dB/s gap this script measured against the shipped Feedback=99% "
     "default - see analysis/validate.py's own git history / the PR that added this script."),
    ("Peak timing error (s)", "peak_time_error_s", 0.1,
     "the buildup peaks at a measurably different time than the real hardware's own - a positive "
     "value means the render's peak arrives LATE relative to the reference."),
    ("Log-spectral distance (dB, unsigned)", "log_spectral_distance_db", 4.0,
     "overall tonal balance is audibly off, not just a narrow band - see the per-band table for "
     "where."),
    ("Resonant peak height error (dB)", "resonant_peak_height_error_db", 3.0,
     "the render's loudest resonant peak (relative to its own spectral floor) is measurably more "
     "or less prominent than the real hardware's own."),
    # Added after the 2026-09 baseAttackMs/burstFloor re-tune: peak_time_error alone passed even
    # when the render stayed audibly louder than the references (relative to its own peak) for a
    # while AFTER the peak - a real, sizeable gap (+4.4dB/+4.5dB averaged across both references
    # before that re-tune) that sat fully visible in this script's own level_at_0.5s/1.0s columns
    # the whole time without ever being flagged. Exactly the failure mode ml-toolkit/README.md's own
    # "every module's own validate.py needs a permanent regression guard" section warns about -
    # added here rather than left as a one-time observation. Thresholds set just above the residual
    # gap remaining after that re-tune (2.4dB/0.9dB averaged), so today's numbers pass but a
    # comparably-sized regression (or the pre-re-tune state) would flag again.
    ("Level at 0.5s post-peak error (dB)", "level_at_0.5s_error_db", 3.0,
     "the render's envelope, relative to its own peak, sits measurably above/below the real "
     "hardware's own at 0.5s post-peak - the burst bank's post-peak decay is shaped differently "
     "than the real hardware's, not just timed differently (see peak_time_error above)."),
    ("Level at 1.0s post-peak error (dB)", "level_at_1.0s_error_db", 2.0,
     "same as the 0.5s check above, one checkpoint later - a gap that only shows up here and not "
     "at 0.5s indicates the mismatch is in how fast the burst decay ITSELF falls, not in its "
     "timing relative to the peak."),
]

BAND_CONCERN_THRESHOLD_DB = 2.0


def _flag_concerns(results: list[dict]) -> list[str]:
    flags = []
    for label, key, threshold, interpretation in CONCERN_THRESHOLDS:
        values = [r[key] for r in results if r.get(key) is not None]
        if not values:
            continue
        mean_value = round(float(np.mean(values)), 3)
        if abs(mean_value) > threshold:
            flags.append(f"**{label}**: {mean_value} exceeds +/-{threshold} - {interpretation}")

    band_errors_by_name: dict[str, list[float]] = {}
    for r in results:
        for name, vals in r.get("band_errors", {}).items():
            if vals.get("error") is not None:
                band_errors_by_name.setdefault(name, []).append(vals["error"])
    for name, errors in band_errors_by_name.items():
        mean_abs = round(float(np.mean(np.abs(errors))), 2)
        if mean_abs > BAND_CONCERN_THRESHOLD_DB:
            flags.append(
                f"**LTAS band error ({name})**: mean |error| {mean_abs}dB exceeds "
                f"+/-{BAND_CONCERN_THRESHOLD_DB}dB - see the per-band table for the signed direction."
            )
    return flags


def write_report(results: list[dict]) -> None:
    lines = ["# Shields validation report", ""]
    lines.append(f"{len(results)} reference capture(s) compared against one render of the "
                 "plugin's current defaults (see Source/PluginProcessor.cpp's createParameterLayout()).")
    lines.append("")
    lines.append("**Standing rule: this table is not the final word on anything.** Check what's "
                 "actually driving any number before concluding a change helped or hurt - see "
                 "ml-toolkit/README.md's own \"after any synthesis architecture change\" section.")
    lines.append("")

    lines.append("## Headline metrics")
    lines.append("")
    lines.append("| Reference | Envelope correlation | LSD (dB) | Decay slope error (dB/s) | "
                 "Peak time error (s) | Resonant peak height error (dB) |")
    lines.append("|---|---|---|---|---|---|")
    for r in results:
        lines.append(f"| {r['reference']} | {r['envelope_correlation']} | {r['log_spectral_distance_db']} | "
                     f"{r['decay_slope_error_db_per_s']} | {r['peak_time_error_s']} | "
                     f"{r['resonant_peak_height_error_db']} |")
    lines.append("")

    lines.append("## Decay and attack timing detail")
    lines.append("")
    lines.append("| Reference | Decay slope rendered | Decay slope reference | Peak time rendered | "
                 "Peak time reference | Level@0.5s error | Level@1.0s error |")
    lines.append("|---|---|---|---|---|---|---|")
    for r in results:
        lines.append(f"| {r['reference']} | {r['decay_slope_db_per_s_rendered']} | "
                     f"{r['decay_slope_db_per_s_reference']} | {r['peak_time_s_rendered']} | "
                     f"{r['peak_time_s_reference']} | {r['level_at_0.5s_error_db']} | "
                     f"{r['level_at_1.0s_error_db']} |")
    lines.append("")

    lines.append("## Flagged concerns")
    lines.append("")
    flags = _flag_concerns(results)
    if flags:
        lines.append("The following values exceed a threshold picked from the magnitude of a real, "
                     "currently-open gap (see CONCERN_THRESHOLDS in this script) - worth acting on, "
                     "not just noting:")
        lines.append("")
        for flag in flags:
            lines.append(f"- {flag}")
    else:
        lines.append("None of the tracked metrics exceed their interpretation threshold.")
    lines.append("")

    lines.append("## Per-band tonal balance (RMS-matched, 1/3-octave-smoothed LTAS, render minus reference)")
    lines.append("")
    header = "| Band | " + " | ".join(r["reference"] for r in results) + " |"
    lines.append(header)
    lines.append("|---|" + "---|" * len(results))
    for _, _, name in LTAS_BANDS:
        row = [name]
        for r in results:
            err = r["band_errors"].get(name, {}).get("error")
            row.append(f"{err:+.2f}" if err is not None else "-")
        lines.append("| " + " | ".join(row) + " |")
    lines.append("")

    lines.append("## Resonant peak height above own spectral floor")
    lines.append("")
    lines.append("| Reference | Rendered (dB above floor) | Reference (dB above floor) | Error |")
    lines.append("|---|---|---|---|")
    for r in results:
        lines.append(f"| {r['reference']} | {r['resonant_peak_height_db_rendered']} | "
                     f"{r['resonant_peak_height_db_reference']} | {r['resonant_peak_height_error_db']} |")

    with open(REPORT_PATH, "w") as fh:
        fh.write("\n".join(lines))


def main() -> None:
    if not os.path.exists(RENDER_IR_BIN):
        print(f"ShieldsRenderIR not built at {RENDER_IR_BIN} - build it first:\n"
              f"  cd .. && cmake --build build --config Release --target ShieldsRenderIR")
        return

    print("Rendering current defaults...")
    render_path = render_defaults()

    results = []
    for name in REFERENCES:
        reference_path = os.path.join(REFERENCE_DIR, name)
        if not os.path.exists(reference_path):
            print(f"Missing reference capture: {reference_path} - skipping")
            continue
        print(f"Comparing against {name}...")
        results.append(compare_one(render_path, reference_path))

    if not results:
        print("No reference captures found - nothing to validate.")
        return

    with open(RESULTS_PATH, "w") as fh:
        json.dump(results, fh, indent=2)
    write_report(results)
    print(f"\nWrote {RESULTS_PATH}")
    print(f"Wrote {REPORT_PATH}")

    flags = _flag_concerns(results)
    if flags:
        print(f"\n{len(flags)} concern(s) flagged (see validation_report.md's own section):")
        for flag in flags:
            print(f"  - {flag}")
    else:
        print("\nNo concerns flagged against the current thresholds.")


if __name__ == "__main__":
    main()
