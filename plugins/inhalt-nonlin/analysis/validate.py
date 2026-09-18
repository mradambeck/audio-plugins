#!/usr/bin/env python3
"""Phase 6: validation. Renders the actual InhaltAudioProcessor (via InhaltRenderIR, not a
re-implementation) at settings matched to each of the real captures in
ml-toolkit/effects/nonlin/captures/, then compares plugin output against the real hardware
capture using the SAME core.features functions ml-toolkit's own analyze.py uses to characterize
the captures in the first place - a fitted/rendered result is judged by the identical yardstick
used on the real audio, not a separate ad hoc metric.

This IS the "A/B against a convolution reverb" check Adam asked about: Inhalt convolves a
synthesized IR, and a real hardware capture convolved with an impulse reproduces itself exactly
(that's what convolution with a delta function does) - so the reference audio here already
represents "what a convolution reverb loaded with the real captured response would produce" with
no extra convolution engine needed. Comparing InhaltRenderIR's synthesized-IR render against the
raw capture directly answers "how close is Inhalt's model/fit to the real hardware's actual
impulse response."

Measures, per capture AND aggregated (all / High=0 / High!=0, to expose systematic bias rather
than average it away - same convention as aura-reverb/analysis/validate.py):
  - Gate envelope shape (gate_envelope_params) - build-up, plateau, knee, fall, all signed error
  - Per-octave-band plateau droop and fall rate (band_gate_params)
  - Tonal balance: log-spectral distance + per-band balance (reused from ../../common/tools/
    compare_wavs.py) - "is it too bright/dark, and where"
  - Diffusion: normalized echo density time-to-1.0 and mixing time
  - Stereo: IACC, per-band coherence (with the noise floor printed), mid/side ratio
  - Crest factor and spectral flatness over the plateau region (compare_wavs.py)

Adopts aura-reverb/analysis/validate.py's hard-won trim fix: both signals are trimmed to the
SHORTER length before any level-matching or spectral comparison - that fix alone dropped Aura's
own LSD from 11.2dB to 3.3dB, i.e. most of an apparent tonal mismatch there was a length artifact,
not a real one.

Requires the real captures in ../../../ml-toolkit/effects/nonlin/captures/ and a built
InhaltRenderIR (../build/InhaltRenderIR_artefacts/Release/InhaltRenderIR - build it first,
console target only, never the AU/VST3 plugin target).
"""
from __future__ import annotations

import json
import os
import re
import subprocess
import sys

import numpy as np

HERE = os.path.dirname(__file__)
sys.path.insert(0, os.path.join(HERE, "..", "..", "..", "ml-toolkit"))
sys.path.insert(0, os.path.join(HERE, "..", "..", "common", "tools"))

from core.features import (  # noqa: E402
    band_interchannel_coherence,
    band_gate_params,
    find_onset,
    gate_envelope_params,
    iacc,
    interchannel_correlation,
    mid_side_ratio_db,
    mixing_time_ms,
    normalized_echo_density,
    octave_bands,
    onset_echo_density,
    time_to_ned_threshold,
)
from core.io import load_audio_channels  # noqa: E402
import compare_wavs as cw  # noqa: E402

CAPTURES_DIR = os.path.join(HERE, "..", "..", "..", "ml-toolkit", "effects", "nonlin", "captures")
RENDER_IR_BIN = os.path.join(HERE, "..", "build", "InhaltRenderIR_artefacts", "Release", "InhaltRenderIR")
RENDER_DIR = os.path.join(HERE, "validation_renders")
REPORT_PATH = os.path.join(HERE, "validation_report.md")
RESULTS_PATH = os.path.join(HERE, "validation_results.json")

NAME_RE = re.compile(r"^NonLin_(?P<time>[\d.]+)s?_(?P<high>[+-]?\d+)H\.wav$")

os.makedirs(RENDER_DIR, exist_ok=True)


def find_captures() -> list[dict]:
    captures = []
    if not os.path.isdir(CAPTURES_DIR):
        return captures
    for name in sorted(os.listdir(CAPTURES_DIR)):
        m = NAME_RE.match(name)
        if not m:
            continue
        captures.append({
            "filename": name,
            "path": os.path.join(CAPTURES_DIR, name),
            "time": float(m.group("time")),
            "high": int(m.group("high")),
        })
    return captures


def render_for(capture: dict) -> str:
    out_path = os.path.join(RENDER_DIR, capture["filename"])
    subprocess.run([
        RENDER_IR_BIN, "--out", out_path,
        "--seconds", "1.5",
        "--timeKnob", str(capture["time"]), "--high", str(capture["high"]),
        "--dry", "0", "--wet", "100",
    ], check=True, capture_output=True)
    return out_path


def trim_to_shorter(a: np.ndarray, b: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
    """Trims both stereo arrays ([2, n]) to the shorter length BEFORE any level-matching or
    spectral comparison - see module docstring on why this matters (Aura's own history)."""
    n = min(a.shape[-1], b.shape[-1])
    return a[..., :n], b[..., :n]


def signed_error(rendered, reference):
    if rendered is None or reference is None:
        return None
    return round(rendered - reference, 4)


def compare_one(capture: dict, render_path: str) -> dict:
    render_ch, sr_r = load_audio_channels(render_path)
    ref_ch, sr_ref = load_audio_channels(capture["path"])
    render_ch, ref_ch = trim_to_shorter(render_ch, ref_ch)

    render_mono = render_ch.mean(axis=0)
    ref_mono = ref_ch.mean(axis=0)
    render_l, render_r = render_ch[0], render_ch[1]
    ref_l, ref_r = ref_ch[0], ref_ch[1]

    onset_r = find_onset(render_mono, sr_r)
    onset_ref = find_onset(ref_mono, sr_ref)

    gate_r = gate_envelope_params(render_mono, sr_r, onset_r)
    gate_ref = gate_envelope_params(ref_mono, sr_ref, onset_ref)
    gate_errors = {k: signed_error(gate_r.get(k), gate_ref.get(k)) for k in gate_r if gate_r.get(k) is not None}

    bands = octave_bands(sr=sr_r)
    band_r = band_gate_params(render_mono, sr_r, onset_r, bands)
    band_ref = band_gate_params(ref_mono, sr_ref, onset_ref, bands)
    band_errors = {}
    for key in band_r:
        droop_r = band_r[key]["plateau_droop_db_per_s"]
        droop_ref = band_ref.get(key, {}).get("plateau_droop_db_per_s")
        fall_r = band_r[key]["fall_rate_db_per_s"]
        fall_ref = band_ref.get(key, {}).get("fall_rate_db_per_s")
        band_errors[f"{key[0]:.0f}-{key[1]:.0f}Hz"] = {
            "droop_error": signed_error(droop_r, droop_ref),
            "fall_rate_error": signed_error(fall_r, fall_ref),
            # Raw values, not just the signed error - a sign MISMATCH (render rising when real
            # decays, or vice versa) is a qualitatively different, more severe failure than "same
            # direction, wrong magnitude", and the error alone can't distinguish the two (see
            # _band_sign_mismatches' own docstring for the real bug this exists to catch).
            "droop_rendered": droop_r, "droop_reference": droop_ref,
            "fall_rate_rendered": fall_r, "fall_rate_reference": fall_ref,
        }

    # Onset-specific density (fine window/hop, first 20ms only) - distinct from the coarser
    # normalized_echo_density comparison below, which measures TIME-TO-full-diffuseness, not
    # instantaneous density at the onset itself. Added after a real, ear-caught gap on this
    # plugin's first render (a thin/sparse initial attack that the coarser metric's own aggregate
    # didn't surface until checked at this finer resolution) - see core.features.
    # onset_echo_density's own docstring and core.fit.onset_density_loss, the fit-time
    # differentiable counterpart meant to catch this during the ML phase rather than after.
    onset_density_r = onset_echo_density(render_mono, sr_r, onset_r)
    onset_density_ref = onset_echo_density(ref_mono, sr_ref, onset_ref)
    onset_ned_mean_error = signed_error(onset_density_r["onset_ned_mean"], onset_density_ref["onset_ned_mean"])
    onset_ned_first_window_error = signed_error(
        onset_density_r["onset_ned_first_window"], onset_density_ref["onset_ned_first_window"]
    )

    ned_times_r, ned_r = normalized_echo_density(render_mono, sr_r, onset_r)
    ned_times_ref, ned_ref = normalized_echo_density(ref_mono, sr_ref, onset_ref)
    t_ned_r = time_to_ned_threshold(ned_times_r, ned_r)
    t_ned_ref = time_to_ned_threshold(ned_times_ref, ned_ref)
    time_to_ned_error_ms = signed_error(
        (t_ned_r - onset_r / sr_r) * 1000.0 if t_ned_r is not None else None,
        (t_ned_ref - onset_ref / sr_ref) * 1000.0 if t_ned_ref is not None else None,
    )
    mixing_r = mixing_time_ms(render_mono, sr_r, onset_r)
    mixing_ref = mixing_time_ms(ref_mono, sr_ref, onset_ref)
    mixing_error_ms = signed_error(mixing_r, mixing_ref)

    iacc_r = iacc(render_l, render_r, sr_r)
    iacc_ref = iacc(ref_l, ref_r, sr_ref)
    coherence_r = band_interchannel_coherence(render_l, render_r, sr_r, bands)
    coherence_ref = band_interchannel_coherence(ref_l, ref_r, sr_ref, bands)
    ms_ratio_r = mid_side_ratio_db(render_l, render_r)
    ms_ratio_ref = mid_side_ratio_db(ref_l, ref_r)

    # compare_wavs.py's LSD/crest/flatness, mono, with its own trim-and-level-match convention -
    # reused directly rather than re-implemented (see module docstring).
    render_mono64 = render_mono.astype(np.float64)
    ref_mono64 = ref_mono.astype(np.float64)
    render_rms = float(np.sqrt(np.mean(render_mono64 ** 2)))
    ref_rms = float(np.sqrt(np.mean(ref_mono64 ** 2)))
    level_matched_render = render_mono64 * (ref_rms / render_rms) if render_rms > 0 else render_mono64
    raw_freqs, psd_r, psd_ref = cw.averaged_psd(level_matched_render, ref_mono64, sr_r)
    spec_freqs, spec_db_r = cw.smooth_to_fractional_octave(raw_freqs, psd_r)
    _, spec_db_ref = cw.smooth_to_fractional_octave(raw_freqs, psd_ref)
    lsd = cw.log_spectral_distance(spec_db_r, spec_db_ref) if len(spec_db_r) else None

    crest_r = cw.crest_factor_db_over_time(render_mono64, sr_r)
    crest_ref = cw.crest_factor_db_over_time(ref_mono64, sr_ref)
    crest_error = signed_error(
        float(np.nanmean(crest_r)) if len(crest_r) else None,
        float(np.nanmean(crest_ref)) if len(crest_ref) else None,
    )
    flatness_r = cw.spectral_flatness_db(render_mono64, sr_r)
    flatness_ref = cw.spectral_flatness_db(ref_mono64, sr_ref)
    flatness_error = signed_error(flatness_r, flatness_ref)

    return {
        "filename": capture["filename"],
        "time": capture["time"],
        "high": capture["high"],
        "gate_errors": gate_errors,
        "band_errors": band_errors,
        "onset_ned_mean_rendered": onset_density_r["onset_ned_mean"],
        "onset_ned_mean_reference": onset_density_ref["onset_ned_mean"],
        "onset_ned_mean_error": onset_ned_mean_error,
        "onset_ned_first_window_error": onset_ned_first_window_error,
        "time_to_ned_0.9_error_ms": time_to_ned_error_ms,
        "mixing_time_error_ms": mixing_error_ms,
        "iacc_rendered": round(iacc_r, 4),
        "iacc_reference": round(iacc_ref, 4),
        "coherence_noise_floor_rendered": coherence_r.get("_noise_floor"),
        "coherence_noise_floor_reference": coherence_ref.get("_noise_floor"),
        "mid_side_ratio_error_db": signed_error(ms_ratio_r, ms_ratio_ref),
        "log_spectral_distance_db": round(lsd, 2) if lsd is not None else None,
        "crest_factor_error_db": crest_error,
        "spectral_flatness_error_db": flatness_error,
    }


# Interpretation thresholds - flags a concerning AGGREGATE value explicitly in the report instead
# of leaving it sitting quietly in a table for someone to notice (or not) by eye. Added after this
# plugin's first real render passed every existing metric's "looks plausible" bar while still
# having an audible, ear-caught onset-density/tonal/texture gap - the numbers were all THERE in
# validation_report.md, just not called out. Thresholds are a first pass, not a formal statistical
# bound - picked from the magnitude of the real gap this plugin actually had (see
# ml-toolkit/effects/nonlin/findings.md and this file's own onset_echo_density addition), meant to
# catch a comparably-sized regression, not to certify perfection.
CONCERN_THRESHOLDS: list[tuple[str, list[str], float, str]] = [
    ("Onset NED mean error (0-20ms)", ["onset_ned_mean_error"], 0.15,
     "the render's initial-attack density measurably diverges from the real hardware's - the "
     "exact 'thin/sparse attack' gap found by ear on this plugin's first pass."),
    ("Spectral flatness error (dB)", ["spectral_flatness_error_db"], 1.5,
     "the render's plateau reads noticeably smoother/more 'open' (or grittier/more resonant) than "
     "the real hardware - the qualitative 'openness vs. grit' complaint."),
    ("Crest factor error (dB)", ["crest_factor_error_db"], 2.0,
     "a texture mismatch consistent with the flatness gap above (a smoother/less peaky signal "
     "usually has a lower crest factor too)."),
    ("Log-spectral distance (dB, unsigned)", ["log_spectral_distance_db"], 4.0,
     "overall tonal balance is audibly off, not just a narrow band - see the per-band table for "
     "where."),
    ("Knee time error (ms)", ["gate_errors", "knee_time_ms"], 30.0,
     "the gate's fall doesn't land where the real hardware's does - audible as the wrong overall "
     "gate length."),
    ("Fall rate error (dB/s)", ["gate_errors", "fall_rate_db_per_s"], 300.0,
     "the gate's post-knee fall is audibly too fast or too slow relative to the real hardware."),
]

# Per-band magnitude thresholds - a SEPARATE check from CONCERN_THRESHOLDS above (which only
# looks at broadband/aggregate numbers), added after a real, ear-caught "huffy, low-mid resonance"
# complaint that the broadband checks completely missed: the render's per-band plateau droop was
# POSITIVE (rising, not decaying) in several bands while the broadband average still looked
# reasonable, because positive and negative per-band errors cancel out in a single mean. band_errors
# has carried this data in validation_results.json since the metric was added (see Phase 6 of the
# original project plan: "Report per-octave-band plateau droop and fall rate"), but nothing ever
# actually checked it - the exact "the numbers were all there, just not called out" pattern
# CONCERN_THRESHOLDS' own docstring above describes, one level deeper. Threshold picked from the
# real gap this complaint traced to (median per-band droop error ~41dB/s, mean ~106dB/s across all
# 81 band/capture combinations at the time this was added) - meant to catch a comparably-sized
# regression, not to certify perfection.
BAND_CONCERN_THRESHOLDS: list[tuple[str, str, float, str]] = [
    ("Per-band plateau droop error (dB/s, mean |error|)", "droop_error", 25.0,
     "at least one octave band's mid-plateau decay is measurably off from the real hardware's own "
     "- check the per-band table for which band(s), and whether any show a SIGN mismatch below "
     "(a far more severe failure than a magnitude-only miss)."),
    ("Per-band fall rate error (dB/s, mean |error|)", "fall_rate_error", 25.0,
     "at least one octave band's post-knee fall rate is measurably off from the real hardware's "
     "own - check the per-band table for which band(s)."),
]


def _flag_band_magnitude_concerns(results: list[dict], group_label: str) -> list[str]:
    """Mirrors _flag_concerns, but for BAND_CONCERN_THRESHOLDS - aggregates the mean absolute
    error across every octave band in every capture in `results` (not per-band, since with only
    9 captures there isn't enough data to trust a per-band-alone aggregate; this is a coarse
    tripwire, not a diagnosis - the per-band table is where the actual diagnosis happens)."""
    flags = []
    for label, key, threshold, interpretation in BAND_CONCERN_THRESHOLDS:
        errors = [
            vals[key] for r in results for vals in r.get("band_errors", {}).values()
            if vals.get(key) is not None
        ]
        if not errors:
            continue
        mean_abs = round(float(np.mean(np.abs(errors))), 3)
        if mean_abs > threshold:
            flags.append(f"**{label} ({group_label})**: {mean_abs} exceeds {threshold} - {interpretation}")
    return flags


def _band_sign_mismatches(results: list[dict], real_floor_db_per_s: float = 10.0) -> list[str]:
    """Every (capture, band, metric) where the render and the real capture DISAGREE ON DIRECTION -
    the render rising (or flat) where the real hardware clearly decays, or vice versa. Qualitatively
    different from, and more severe than, "right direction, wrong magnitude" (what the mean-|error|
    check above catches) - a sign flip means the render's per-band character is not just imprecise
    but backwards, which a magnitude-only aggregate can hide entirely if positive and negative
    per-band errors happen to cancel out.

    `real_floor_db_per_s` guards against flagging a real value that's already near zero (where
    "which sign" is dominated by measurement noise, not a real hardware property) - only a real
    decay/rise steeper than this floor counts. No threshold on the render's own magnitude: a
    render sitting at +0.01dB/s while real decays at -50dB/s is still a real, audible sign flip
    (essentially not decaying at all where it clearly should be), not a rounding difference.

    Added after a real "huffy, low-mid resonance... rings out longer than the IR's" complaint
    traced to exactly this - core.features.band_gate_params showed several bands (varying by Time
    setting, e.g. 354-1414Hz at Time=9.8/High=0, 88-707Hz at Time=7.0/High=0) with a positive
    (rising) render droop against a clearly-decaying real target, invisible in every existing
    broadband/aggregate check. Found to affect 26 of 162 band/capture/metric combinations (9
    bands x 9 captures x 2 metrics) at the time this was added - not a rare edge case, and not
    one-directional either (some bands/captures show the opposite mismatch - real hardware rising
    where the render decays)."""
    lines = []
    for r in results:
        for band, vals in r.get("band_errors", {}).items():
            for metric, rendered_key, reference_key in (
                ("plateau droop", "droop_rendered", "droop_reference"),
                ("fall rate", "fall_rate_rendered", "fall_rate_reference"),
            ):
                rendered, reference = vals.get(rendered_key), vals.get(reference_key)
                if rendered is None or reference is None:
                    continue
                if abs(reference) < real_floor_db_per_s:
                    continue
                if (rendered > 0) != (reference > 0):
                    lines.append(
                        f"Time={r['time']} High={r['high']} {band} {metric}: render={rendered:.2f}dB/s "
                        f"vs. real={reference:.2f}dB/s - opposite direction"
                    )
    return lines


def _flag_concerns(results: list[dict], group_label: str) -> list[str]:
    """Returns human-readable flag lines for every CONCERN_THRESHOLDS metric whose aggregate
    |value| exceeds its threshold on this group of results - empty list if nothing is flagged."""
    flags = []
    for label, path, threshold, interpretation in CONCERN_THRESHOLDS:
        value = _aggregate(results, path)
        if value is not None and abs(value) > threshold:
            flags.append(f"**{label} ({group_label})**: {value} exceeds +/-{threshold} - {interpretation}")
    return flags


def _aggregate(results: list[dict], field_path: list[str]) -> float | None:
    values = []
    for r in results:
        v = r
        for key in field_path:
            v = v.get(key) if isinstance(v, dict) else None
            if v is None:
                break
        if v is not None:
            values.append(v)
    return round(float(np.mean(values)), 3) if values else None


def _compute_all_flags(results: list[dict]) -> tuple[list[str], list[str]]:
    """Single source of truth for every flagged concern - CONCERN_THRESHOLDS, BAND_CONCERN_
    THRESHOLDS, and the per-band sign-mismatch summary line - shared between write_report() and
    main()'s own console summary. Previously duplicated inline in both places, which is exactly
    how the sign-mismatch/magnitude checks could have silently existed in one but not the other -
    the same class of drift this whole addition exists to catch, one level up. Returns
    (flags, sign_mismatch_detail_lines) - the caller decides how to render each."""
    h0_results = [r for r in results if r["high"] == 0]
    hneg_results = [r for r in results if r["high"] != 0]
    flags = (
        _flag_concerns(results, "all")
        + _flag_concerns(h0_results, "High=0")
        + _flag_concerns(hneg_results, "High!=0")
        + _flag_band_magnitude_concerns(results, "all")
        + _flag_band_magnitude_concerns(h0_results, "High=0")
        + _flag_band_magnitude_concerns(hneg_results, "High!=0")
    )
    sign_mismatches = _band_sign_mismatches(results)
    if sign_mismatches:
        flags.append(
            f"Per-band sign mismatches (all): {len(sign_mismatches)} band/capture/metric "
            f"combination(s) where the render decays in the OPPOSITE direction from the real "
            f"hardware - a more severe failure than a magnitude-only miss. See validation_report."
            f"md's own 'Per-band sign mismatches' section for the full list."
        )
    return flags, sign_mismatches


def write_report(results: list[dict]) -> None:
    all_results = results
    h0_results = [r for r in results if r["high"] == 0]
    hneg_results = [r for r in results if r["high"] != 0]

    lines = ["# Inhalt validation report", ""]
    lines.append(f"{len(results)} captures compared. Split all / High=0 / High!=0 to expose "
                  "systematic bias rather than average it away.")
    lines.append("")
    lines.append("**Standing rule: this aggregate table is not the final word on anything.** "
                  "Check what's actually driving any number before concluding a change helped or "
                  "hurt - see ml-toolkit/effects/ambience's own history of this metric getting "
                  "worse while the DSP got more correct, twice.")
    lines.append("")

    lines.append("## Aggregate (mean absolute-value-blind signed error, unless noted)")
    lines.append("")
    lines.append("| Metric | All | High=0 | High!=0 |")
    lines.append("|---|---|---|---|")
    metrics = [
        ("Knee time error (ms)", ["gate_errors", "knee_time_ms"]),
        ("Fall rate error (dB/s)", ["gate_errors", "fall_rate_db_per_s"]),
        ("Plateau droop error (dB/s)", ["gate_errors", "plateau_droop_db_per_s"]),
        ("Build-up error (ms)", ["gate_errors", "build_up_ms"]),
        ("Onset NED mean error (0-20ms)", ["onset_ned_mean_error"]),
        ("Onset NED first-window error", ["onset_ned_first_window_error"]),
        ("Time-to-NED=0.9 error (ms)", ["time_to_ned_0.9_error_ms"]),
        ("Mixing time error (ms)", ["mixing_time_error_ms"]),
        ("Mid/side ratio error (dB)", ["mid_side_ratio_error_db"]),
        ("Log-spectral distance (dB, unsigned)", ["log_spectral_distance_db"]),
        ("Crest factor error (dB)", ["crest_factor_error_db"]),
        ("Spectral flatness error (dB)", ["spectral_flatness_error_db"]),
    ]
    for label, path in metrics:
        row = [label]
        for group in (all_results, h0_results, hneg_results):
            row.append(str(_aggregate(group, path)) if group else "-")
        lines.append("| " + " | ".join(row) + " |")
    lines.append("")

    lines.append("## Flagged concerns")
    lines.append("")
    all_flags, sign_mismatches = _compute_all_flags(all_results)
    if all_flags:
        lines.append("The following aggregate values exceed a threshold picked from the magnitude "
                      "of a real, previously-found gap (see CONCERN_THRESHOLDS in this script) - "
                      "worth listening to, not just noting:")
        lines.append("")
        for flag in all_flags:
            lines.append(f"- {flag}")
    else:
        lines.append("None of the tracked metrics exceed their interpretation threshold.")
    lines.append("")

    lines.append("## Per-band sign mismatches")
    lines.append("")
    lines.append("Every octave band/capture/metric where the render decays in the OPPOSITE "
                  "direction from the real hardware (see `_band_sign_mismatches`' own docstring) - "
                  "a more severe failure than the magnitude-only per-band check above, since a sign "
                  "flip can hide entirely inside a broadband average that still looks reasonable.")
    lines.append("")
    if sign_mismatches:
        for line in sign_mismatches:
            lines.append(f"- {line}")
    else:
        lines.append("None.")
    lines.append("")

    lines.append("## Stereo (IACC, rendered vs. reference, per capture)")
    lines.append("")
    lines.append("| File | IACC rendered | IACC reference | Coherence floor (r/ref) |")
    lines.append("|---|---|---|---|")
    for r in results:
        lines.append(f"| {r['filename']} | {r['iacc_rendered']} | {r['iacc_reference']} | "
                      f"{r['coherence_noise_floor_rendered']} / {r['coherence_noise_floor_reference']} |")
    lines.append("")

    lines.append("## Per-capture gate errors")
    lines.append("")
    lines.append("| File | Knee error (ms) | Fall rate error (dB/s) | Droop error (dB/s) | Build-up error (ms) | LSD (dB) |")
    lines.append("|---|---|---|---|---|---|")
    for r in results:
        g = r["gate_errors"]
        lines.append(f"| {r['filename']} | {g.get('knee_time_ms')} | {g.get('fall_rate_db_per_s')} | "
                      f"{g.get('plateau_droop_db_per_s')} | {g.get('build_up_ms')} | {r['log_spectral_distance_db']} |")

    with open(REPORT_PATH, "w") as fh:
        fh.write("\n".join(lines))


def main() -> None:
    captures = find_captures()
    print(f"Found {len(captures)} real captures")
    if not captures:
        print("No captures found - nothing to validate.")
        return
    if not os.path.exists(RENDER_IR_BIN):
        print(f"InhaltRenderIR not built at {RENDER_IR_BIN} - build it first:\n"
              f"  cd ../.. && cmake --build build --config Release --target InhaltRenderIR")
        return

    results = []
    for capture in captures:
        print(f"Rendering + comparing {capture['filename']} (Time={capture['time']}, High={capture['high']}) ...")
        render_path = render_for(capture)
        result = compare_one(capture, render_path)
        results.append(result)

    with open(RESULTS_PATH, "w") as fh:
        json.dump(results, fh, indent=2)
    write_report(results)
    print(f"\nWrote {RESULTS_PATH}")
    print(f"Wrote {REPORT_PATH}")

    all_flags, _ = _compute_all_flags(results)
    if all_flags:
        print(f"\n{len(all_flags)} concern(s) flagged (see validation_report.md's own section):")
        for flag in all_flags:
            print(f"  - {flag}")
    else:
        print("\nNo concerns flagged against the current thresholds.")


if __name__ == "__main__":
    main()
