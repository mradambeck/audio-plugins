#!/usr/bin/env python3
"""Phase B1/B2: inventory + feature extraction over the 65 AMS RMX16 Ambience IR captures.

Mirrors intruder-gated-reverb/analysis/analyze_irs.py's structure, built on core/io.py and
core/features.py instead of effect-local copies. Writes features.json (git-tracked) and one
diagnostic plot per capture under plots/ (gitignored) - hand-review these (findings.md) before
trusting any number, per the plan's B3 step.
"""
import json
import os

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

from core.features import (
    analyze_capture,
    echo_density,
    find_onset,
    hilbert_envelope_db,
    spectral_tilt_over_time,
)
from core.io import load_audio, load_manifest, print_inventory
from effects.ambience.capture_schema import AMBIENCE_SCHEMA

HERE = os.path.dirname(__file__)
CAPTURES_DIR = os.path.join(HERE, "captures")
PLOTS_DIR = os.path.join(HERE, "plots")
FEATURES_PATH = os.path.join(HERE, "features.json")


def make_plot(x: np.ndarray, sr: int, name: str, out_path: str) -> None:
    onset_idx = find_onset(x, sr)
    t_full = np.arange(len(x)) / sr
    env_db = hilbert_envelope_db(x, sr)
    env_db_norm = env_db - np.max(env_db)
    onset_time_s = onset_idx / sr

    tilt_t, tilt_db = spectral_tilt_over_time(x, sr)
    dens_t, dens_c = echo_density(x, sr, onset_idx)

    fig, axes = plt.subplots(3, 1, figsize=(9, 8))
    axes[0].plot(t_full, env_db_norm, lw=0.8)
    axes[0].axhline(-10, color="gray", lw=0.5, ls="--")
    axes[0].axhline(-20, color="gray", lw=0.5, ls="--")
    axes[0].axhline(-30, color="gray", lw=0.5, ls="--")
    axes[0].axvline(onset_time_s, color="red", lw=0.5)
    axes[0].set_ylim(-80, 5)
    axes[0].set_title(f"{name} - Hilbert envelope (dB rel peak)")
    axes[0].set_xlabel("s")

    axes[1].plot(tilt_t, tilt_db, lw=0.8, color="orange")
    axes[1].axvline(onset_time_s, color="red", lw=0.5)
    axes[1].set_title("spectral tilt: 10*log10(HF energy / LF energy), split 2kHz")
    axes[1].set_xlabel("s")

    axes[2].plot(dens_t, dens_c, lw=0.8, color="green")
    axes[2].set_title("echo density (peaks per 10ms window)")
    axes[2].set_xlabel("s")

    fig.tight_layout()
    fig.savefig(out_path, dpi=110)
    plt.close(fig)


def main() -> None:
    captures = load_manifest(CAPTURES_DIR, AMBIENCE_SCHEMA)
    print_inventory(captures)

    os.makedirs(PLOTS_DIR, exist_ok=True)
    results = []
    for capture in captures:
        name = os.path.basename(capture.path)
        print(f"analyzing {name} ...")
        x, sr = load_audio(capture.path)
        feat = analyze_capture(x, sr)
        feat["filename"] = name
        feat["params"] = capture.params
        results.append(feat)
        make_plot(x, sr, name, os.path.join(PLOTS_DIR, name.replace(".wav", ".png")))

    with open(FEATURES_PATH, "w") as fh:
        json.dump(results, fh, indent=2)
    print(f"\nWrote {FEATURES_PATH}")
    print(f"Wrote plots to {PLOTS_DIR}/")


if __name__ == "__main__":
    main()
