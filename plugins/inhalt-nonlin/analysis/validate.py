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
        droop_err = signed_error(band_r[key]["plateau_droop_db_per_s"], band_ref.get(key, {}).get("plateau_droop_db_per_s"))
        fall_err = signed_error(band_r[key]["fall_rate_db_per_s"], band_ref.get(key, {}).get("fall_rate_db_per_s"))
        band_errors[f"{key[0]:.0f}-{key[1]:.0f}Hz"] = {"droop_error": droop_err, "fall_rate_error": fall_err}

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


if __name__ == "__main__":
    main()
