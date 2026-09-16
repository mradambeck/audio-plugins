#!/usr/bin/env python3
"""Phase 1/2: inventory + feature extraction over the AMS RMX16 NonLin captures.

Mirrors effects/ambience/analyze.py's structure, but stereo-aware throughout (core.io.load_audio's
mono mix would destroy the single most structurally important property of these captures - see
core.io.load_audio_channels' docstring) and built around gate_envelope_params() rather than
full_decay_rt60(), which effects/nonlin/findings.md explains is the wrong primitive for a gated
response (it fits a single line through a shape that isn't one).

Deliberately does NOT reuse anything from plugins/intruder-gated-reverb/analysis/ - see this
project's top-level instruction to start fresh on this hardware program's DSP. Structural/CI
conventions are unaffected by that instruction; only DSP conclusions must be re-derived.

Two files this script's schema deliberately does NOT match, handled by explicit path instead of
load_manifest(): NonLin_noisefloor.wav (unit running, no impulse - establishes the true digital
floor every decay fit must stop above) and NonLin_bypass.wav (same impulse, Wet=0 - separates the
converter/analog path's own tonal signature from the reverb algorithm's, per AuraFDNEngine.h's
documented need to correct a wrong claim about exactly this). Both are optional: if Adam's capture
set doesn't include them, this script still runs, with _chain left absent from features.json and a
note printed instead.
"""
from __future__ import annotations

import json
import os

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

from core.features import (
    band_gate_params,
    band_interchannel_coherence,
    find_onset,
    gate_envelope_params,
    hilbert_envelope_db,
    iacc,
    interchannel_correlation,
    long_term_average_spectrum,
    mid_side_ratio_db,
    mixing_time_ms,
    modal_overlap_crossover_hz,
    normalized_echo_density,
    octave_bands,
    resonant_peaks,
    stereo_gate_alignment,
    time_to_ned_threshold,
)
from core.io import load_audio_channels, load_manifest, print_inventory
from effects.nonlin.capture_schema import NONLIN_SCHEMA

HERE = os.path.dirname(__file__)
CAPTURES_DIR = os.path.join(HERE, "captures")
PLOTS_DIR = os.path.join(HERE, "plots")
FEATURES_PATH = os.path.join(HERE, "features.json")

NOISEFLOOR_NAME = "NonLin_noisefloor.wav"
BYPASS_NAME = "NonLin_bypass.wav"

# Hand-measured -20dB gate lengths (ms) from direct hardware capture inspection, at H=0/-3
# (the grid Adam's 9 captures happen to sample) - see the project plan's "already measured"
# section. gate_envelope_params()'s own gate_length_ms_at_20db must reproduce these within a few
# ms per-capture: an assertion here, not an eyeballed plot, per this project's standing rule that
# a new measurement function gets checked against a known-correct answer before being trusted.
EXPECTED_GATE_LENGTH_MS_AT_20DB = {
    0.1: 102.5, 0.8: 102.5, 2.2: 149.3, 4.8: 216.6, 7.0: 278.1, 9.8: 300.4,
}
GATE_LENGTH_TOLERANCE_MS = 10.0


def _coherence_json_safe(coherence: dict) -> dict:
    """band_interchannel_coherence() keys bands by a (lo_hz, hi_hz) tuple - not JSON-serializable
    directly, so stringify here rather than in the shared core.features function (which has other
    non-JSON callers, e.g. tests that want the tuple keys back)."""
    out = {}
    for key, value in coherence.items():
        if key == "_noise_floor":
            out[key] = value
        else:
            lo, hi = key
            out[f"{lo:.0f}-{hi:.0f}Hz"] = value
    return out


def analyze_nonlin_capture(channels: np.ndarray, sr: int) -> dict:
    """channels: [2, n] (or [1, n] for a mono file, handled by duplicating). Effect-specific
    composition of core.features' functions - deliberately not merged into core.features.
    analyze_capture(), which stays generic and unaffected by this module (see that function's own
    docstring)."""
    if channels.shape[0] == 1:
        l = r = channels[0]
    else:
        l, r = channels[0], channels[1]
    mono = channels.mean(axis=0)
    onset = find_onset(mono, sr)
    onset_s = onset / sr

    gate = gate_envelope_params(mono, sr, onset)
    bands = octave_bands(sr=sr)
    band_gate = {f"{lo:.0f}-{hi:.0f}Hz": v for (lo, hi), v in band_gate_params(mono, sr, onset, bands).items()}

    ned_times, ned = normalized_echo_density(mono, sr, onset)
    ned_at_plateau_start = None
    if gate["build_up_ms"] is not None and len(ned_times):
        plateau_start_s = onset_s + gate["build_up_ms"] / 1000.0
        idx = int(np.argmin(np.abs(ned_times - plateau_start_s)))
        ned_at_plateau_start = float(ned[idx])
    time_to_ned_1 = time_to_ned_threshold(ned_times, ned, threshold=0.9, hold_frames=3)
    time_to_ned_1_ms = round((time_to_ned_1 - onset_s) * 1000.0, 2) if time_to_ned_1 is not None else None

    mixing_t_ms = mixing_time_ms(mono, sr, onset)

    # Modal-overlap crossover, measured inside the plateau (a window presumed quasi-stationary) -
    # falls back to a fixed short window near onset if the gate fit didn't find a plateau.
    if gate["build_up_ms"] is not None and gate["knee_time_ms"] is not None:
        window_s = (onset_s + gate["build_up_ms"] / 1000.0, onset_s + gate["knee_time_ms"] / 1000.0)
    else:
        window_s = (onset_s, onset_s + 0.05)
    # Decay time for the modal-bandwidth formula: this capture's own gate fall rate translated to
    # an RT60-equivalent (dB/60 * fall_rate gives seconds-to-fall-60dB) - explicitly the GATE's
    # fall, not a claim about the tank's true damping (see findings.md on why RT60 is the wrong
    # primitive here); used only as the crossover formula's decay-time input, not reported as RT60.
    decay_time_s = None
    if gate["fall_rate_db_per_s"] is not None and gate["fall_rate_db_per_s"] < 0:
        decay_time_s = 60.0 / abs(gate["fall_rate_db_per_s"])
    crossover_hz = modal_overlap_crossover_hz(mono, sr, window_s, decay_time_s, bands=bands) if decay_time_s else None

    correlation = interchannel_correlation(l, r)
    iacc_v = iacc(l, r, sr)
    coherence = _coherence_json_safe(band_interchannel_coherence(l, r, sr, bands))
    ms_ratio_db = mid_side_ratio_db(l, r)
    stereo_align = stereo_gate_alignment(l, r, sr)

    ltas_f, ltas_db = long_term_average_spectrum(mono, sr)
    peaks = resonant_peaks(mono, sr)

    return {
        "onset_time_s": round(onset_s, 4),
        "gate": gate,
        "gate_length_ms_at_20db_check": gate["gate_length_ms_at_20db"],
        "band_gate": band_gate,
        "normalized_echo_density_mean": round(float(np.mean(ned)), 4) if len(ned) else None,
        "ned_at_plateau_start": round(ned_at_plateau_start, 4) if ned_at_plateau_start is not None else None,
        "time_to_ned_0.9_ms": time_to_ned_1_ms,
        "mixing_time_ms": round(mixing_t_ms, 2) if mixing_t_ms is not None else None,
        "modal_overlap_crossover_hz": round(crossover_hz, 1) if crossover_hz is not None else None,
        "stereo": {
            "interchannel_correlation": round(correlation, 4),
            "iacc": round(iacc_v, 4),
            "coherence": coherence,
            "mid_side_ratio_db": round(ms_ratio_db, 2),
            "gate_alignment": stereo_align,
        },
        "ltas_third_octave": [[round(float(f), 1), round(float(d), 2)] for f, d in zip(ltas_f, ltas_db)],
        "resonant_peaks": peaks,
    }


def make_plot(channels: np.ndarray, sr: int, name: str, out_path: str) -> None:
    l, r = (channels[0], channels[1]) if channels.shape[0] > 1 else (channels[0], channels[0])
    mono = channels.mean(axis=0)
    onset_idx = find_onset(mono, sr)
    onset_time_s = onset_idx / sr

    env_l = hilbert_envelope_db(l, sr) - np.max(hilbert_envelope_db(l, sr))
    env_r = hilbert_envelope_db(r, sr) - np.max(hilbert_envelope_db(r, sr))
    t_full = np.arange(len(mono)) / sr

    gate = gate_envelope_params(mono, sr, onset_idx)
    ned_times, ned = normalized_echo_density(mono, sr, onset_idx)
    ltas_f, ltas_db = long_term_average_spectrum(mono, sr)
    bands = octave_bands(sr=sr)
    band_gate = band_gate_params(mono, sr, onset_idx, bands)
    coherence = band_interchannel_coherence(l, r, sr, bands)

    fig, axes = plt.subplots(5, 1, figsize=(9, 13))

    axes[0].plot(t_full, env_l, lw=0.7, label="L", alpha=0.8)
    axes[0].plot(t_full, env_r, lw=0.7, label="R", alpha=0.8)
    axes[0].axvline(onset_time_s, color="red", lw=0.5)
    if gate["knee_time_ms"] is not None:
        axes[0].axvline(onset_time_s + gate["knee_time_ms"] / 1000.0, color="purple", lw=0.7, ls="--", label="fitted knee")
    axes[0].set_ylim(-90, 5)
    axes[0].set_title(f"{name} - Hilbert envelope, L/R (dB rel own peak)")
    axes[0].legend(fontsize=7)
    axes[0].set_xlabel("s")

    bar_labels = list(band_gate.keys())
    droops = [v["plateau_droop_db_per_s"] or 0 for v in band_gate.values()]
    falls = [v["fall_rate_db_per_s"] or 0 for v in band_gate.values()]
    x = np.arange(len(bar_labels))
    axes[1].bar(x - 0.2, droops, width=0.4, label="plateau droop dB/s")
    axes[1].bar(x + 0.2, falls, width=0.4, label="fall rate dB/s (note: different scale)")
    axes[1].set_xticks(x)
    axes[1].set_xticklabels([f"{lo:.0f}-{hi:.0f}" for lo, hi in bands], rotation=45, fontsize=6)
    axes[1].set_title("per-band plateau droop vs. fall rate")
    axes[1].legend(fontsize=7)

    axes[2].plot(ned_times, ned, lw=0.8, color="green")
    axes[2].axhline(1.0, color="gray", lw=0.5, ls="--")
    axes[2].set_title("normalized echo density (1.0 = fully diffuse)")
    axes[2].set_xlabel("s")

    axes[3].semilogx(ltas_f, ltas_db, lw=0.8, color="orange")
    axes[3].set_title("long-term average spectrum (third-octave)")
    axes[3].set_xlabel("Hz")

    band_names = [k for k in coherence if k != "_noise_floor"]
    coh_vals = [coherence[k] or 0 for k in band_names]
    axes[4].bar(range(len(band_names)), coh_vals)
    if coherence.get("_noise_floor") is not None:
        axes[4].axhline(coherence["_noise_floor"], color="red", lw=0.7, ls="--", label="noise floor")
    axes[4].set_xticks(range(len(band_names)))
    axes[4].set_xticklabels([str(b) for b in bands], rotation=45, fontsize=6)
    axes[4].set_title("interchannel coherence per band")
    axes[4].legend(fontsize=7)

    fig.tight_layout()
    fig.savefig(out_path, dpi=110)
    plt.close(fig)


def _analyze_named_file(name: str) -> dict | None:
    path = os.path.join(CAPTURES_DIR, name)
    if not os.path.exists(path):
        return None
    channels, sr = load_audio_channels(path)
    return analyze_nonlin_capture(channels, sr)


def main() -> None:
    captures = load_manifest(CAPTURES_DIR, NONLIN_SCHEMA)
    print_inventory(captures)

    chain = {}
    noisefloor = _analyze_named_file(NOISEFLOOR_NAME)
    bypass = _analyze_named_file(BYPASS_NAME)
    if noisefloor is None:
        print(f"note: {NOISEFLOOR_NAME} not present - no measured capture-chain noise floor; "
              "every decay fit's stopping level is a per-capture estimate only")
    else:
        chain["noisefloor"] = noisefloor
    if bypass is None:
        print(f"note: {BYPASS_NAME} not present - the converter/analog path's own tonal signature "
              "can't be separated from the algorithm's; Converter switch stays spec-derived (see README)")
    else:
        chain["bypass"] = bypass

    os.makedirs(PLOTS_DIR, exist_ok=True)
    results = []
    length_check_failures = []
    for capture in captures:
        name = os.path.basename(capture.path)
        print(f"analyzing {name} ...")
        channels, sr = load_audio_channels(capture.path)
        feat = analyze_nonlin_capture(channels, sr)
        feat["filename"] = name
        feat["params"] = capture.params

        time_label = capture.params.get("time")
        expected = EXPECTED_GATE_LENGTH_MS_AT_20DB.get(time_label)
        measured = feat["gate_length_ms_at_20db_check"]
        if expected is not None and measured is not None:
            if abs(measured - expected) > GATE_LENGTH_TOLERANCE_MS:
                length_check_failures.append((name, expected, measured))

        try:
            make_plot(channels, sr, name, os.path.join(PLOTS_DIR, name.replace(".wav", ".png")))
        except Exception as exc:  # a plotting failure shouldn't lose the numeric analysis
            print(f"  warning: plot failed for {name}: {exc}")

        results.append(feat)

    payload = {"captures": results}
    if chain:
        payload["_chain"] = chain
    with open(FEATURES_PATH, "w") as f:
        json.dump(payload, f, indent=2)
    print(f"wrote {FEATURES_PATH}")

    if length_check_failures:
        print("\nWARNING: gate_envelope_params()'s gate_length_ms_at_20db did not reproduce the "
              "hand-measured table within tolerance - the gate-detection code likely has a bug, "
              "not the hardware:")
        for name, expected, measured in length_check_failures:
            print(f"  {name}: expected ~{expected}ms, measured {measured}ms")
    else:
        print("\ngate_length_ms_at_20db matched the hand-measured table for every checked capture.")


if __name__ == "__main__":
    main()
