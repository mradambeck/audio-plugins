#!/usr/bin/env python3
"""Phase D: validation. Renders the actual AuraAudioProcessor (via AuraRenderIR, not a
re-implementation) at settings matched to each of the 65 reference captures in
ml-toolkit/effects/ambience/captures/, then compares plugin output against the real hardware
capture on more than just amplitude/envelope:

  - Envelope shape (reused from ../../common/tools/compare_wavs.py)
  - EQ / tonal balance: log-spectral distance + per-band spectral balance (reused from
    compare_wavs.py) - "is it too bright/dark, and where" - reported both per-file AND as an
    aggregate mean/median across all 65, specifically to answer "is there a systematic EQ bias"
    as opposed to just whether the tone controls shift things in the right direction.
  - Compression/dynamics: windowed crest factor (peak/RMS in dB) over the decay.
  - Harmonic/resonant character: spectral flatness (Wiener entropy) over the tail - comb-filtered/
    too-few-lines reads as low flatness, a real diffuse tail reads high/noise-like.

Mapping from a reference filename's (Time, Low, High) to plugin parameters: Time and High map
directly (Aura's Time parameter IS the hardware's own label - unlike Intruder's Decay, findings.md
found Time close to literal seconds, so the fitted curves in AuraReferenceData.h are keyed by the
label value directly, not a measured RT60). Low is passed through for completeness but has NO
effect on the render (AuraParameterMap deliberately doesn't wire it - see its own comment) - the
per-file results table flags which captures have nonzero Low so that expected/unmodeled error is
visible rather than silently averaged away.

Requires the 65 captures in ../../ml-toolkit/effects/ambience/captures/ and a built AuraRenderIR
(../build/AuraRenderIR_artefacts/Release/AuraRenderIR - build it first, console target only, never
the AU/VST3 plugin target).
"""
import json
import os
import re
import subprocess
import sys

import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "common", "tools"))
import compare_wavs as cw

HERE = os.path.dirname(__file__)
CAPTURES_DIR = os.path.join(HERE, "..", "..", "ml-toolkit", "effects", "ambience", "captures")
RENDER_IR_BIN = os.path.join(HERE, "..", "build", "AuraRenderIR_artefacts", "Release", "AuraRenderIR")
RENDER_DIR = os.path.join(HERE, "validation_renders")
PLOTS_DIR = os.path.join(HERE, "validation_plots")
REPORT_PATH = os.path.join(HERE, "validation_report.md")

NAME_RE = re.compile(r"^Ambience_(?P<time>[\d.]+)s?_(?P<low>[+-]?\d+)L(?P<high>[+-]?\d+)H\.wav$")

os.makedirs(RENDER_DIR, exist_ok=True)
os.makedirs(PLOTS_DIR, exist_ok=True)


def tail_segment(data, sampleRate, startFrac=0.3, endFrac=0.9):
    n = len(data)
    return data[int(n * startFrac) : int(n * endFrac)]


def parse_filename(name):
    m = NAME_RE.match(name)
    return {"time": float(m.group("time")), "low": int(m.group("low")), "high": int(m.group("high"))}


def render_plugin(time_seconds, high_db, seconds, out_path):
    cmd = [
        RENDER_IR_BIN,
        "--out", out_path,
        "--seconds", str(seconds),
        "--timeSeconds", str(time_seconds),
        "--highDb", str(high_db),
        "--mixPercent", "100",
    ]
    subprocess.run(cmd, check=True, capture_output=True)


def compare_pair(ref_path, render_path):
    refData, refRate = cw.load_mono(ref_path)
    rendData, rendRate = cw.load_mono(render_path)

    targetRate = max(refRate, rendRate)
    refData = cw.resample_to(refData, refRate, targetRate)
    rendData = cw.resample_to(rendData, rendRate, targetRate)

    refData = refData[cw.find_onset(refData):]
    rendData = rendData[cw.find_onset(rendData):]

    # Trim BOTH to the same (shorter) length before any level-matching or spectral comparison -
    # AuraRenderIR is asked to render longer than short-Time references need (their own capture
    # is much shorter than time+3.0s), and compare_wavs.py's RMS-based level match uses each
    # signal's own full length: an untrimmed, mostly-quiet-tail render has a much lower average
    # RMS than a reference whose short capture is dominated by its loud onset, so "matching"
    # levels against the wrong duration over-boosts the actually-comparable portion. Confirmed
    # directly: LSD dropped from 11.2dB to 3.3dB on Ambience_0.1s_0L0H.wav just from this trim,
    # with band diffs +11.98/+11.91/+8.87dB shrinking to +3.74/+3.68/+0.64dB - most of what looked
    # like a severe tonal mismatch was this length-mismatch artifact, not the DSP. Decay-length
    # mismatches are already covered separately (envelope correlation here, plus the Python-side
    # RT60 comparison in cross_validation_report.md) - this trim keeps the EQ check from
    # conflating "rendered extra tail" with "wrong tone".
    sharedLength = min(len(refData), len(rendData))
    refData = refData[:sharedLength]
    rendData = rendData[:sharedLength]

    refRms = float(np.sqrt(np.mean(refData.astype(np.float64) ** 2)))
    rendRms = float(np.sqrt(np.mean(rendData.astype(np.float64) ** 2)))
    rendDataMatched = rendData * (refRms / rendRms) if rendRms > 0.0 else rendData

    windowSize = max(1, int(targetRate * 0.02))
    rmsRef = cw.windowed_rms(refData, windowSize)
    rmsRend = cw.windowed_rms(rendDataMatched, windowSize)
    correlation = cw.envelope_correlation(rmsRend, rmsRef)

    rawFreqs, psdRend, psdRef = cw.averaged_psd(rendDataMatched, refData, targetRate)
    freqs, dbRend = cw.smooth_to_fractional_octave(rawFreqs, psdRend)
    _, dbRef = cw.smooth_to_fractional_octave(rawFreqs, psdRef)
    lsd = cw.log_spectral_distance(dbRend, dbRef)

    spectrumDiff = dbRend - dbRef
    bandDiffs = {}
    for label, lo, hi in [("low", 20, 500), ("mid", 500, 4000), ("high", 4000, freqs[-1] if len(freqs) else 20000)]:
        inBand = (freqs >= lo) & (freqs <= hi)
        bandDiffs[label] = float(np.mean(spectrumDiff[inBand])) if np.any(inBand) else None

    crestRef = cw.crest_factor_db_over_time(refData, targetRate)
    crestRend = cw.crest_factor_db_over_time(rendDataMatched, targetRate)
    crestRefMean = float(np.nanmean(crestRef)) if len(crestRef) else None
    crestRendMean = float(np.nanmean(crestRend)) if len(crestRend) else None
    crestDiff = (crestRendMean - crestRefMean) if (crestRefMean is not None and crestRendMean is not None) else None

    flatRef = cw.spectral_flatness_db(tail_segment(refData, targetRate), targetRate)
    flatRend = cw.spectral_flatness_db(tail_segment(rendDataMatched, targetRate), targetRate)
    flatDiff = (flatRend - flatRef) if (flatRef is not None and flatRend is not None) else None

    return {
        "envelope_correlation": round(correlation, 4),
        "log_spectral_distance_db": round(lsd, 3),
        "spectral_band_diff_db": {k: (round(v, 2) if v is not None else None) for k, v in bandDiffs.items()},
        "crest_factor_diff_db": round(crestDiff, 2) if crestDiff is not None else None,
        "spectral_flatness_diff_db": round(flatDiff, 2) if flatDiff is not None else None,
    }, (refData, rendDataMatched, targetRate, rmsRef, rmsRend)


def main():
    if not os.path.exists(RENDER_IR_BIN):
        print(f"AuraRenderIR not found at {RENDER_IR_BIN} - build it first (console target only).", file=sys.stderr)
        sys.exit(1)

    files = sorted(f for f in os.listdir(CAPTURES_DIR) if f.endswith(".wav"))
    results = []

    for name in files:
        meta = parse_filename(name)
        ref_path = os.path.join(CAPTURES_DIR, name)
        render_path = os.path.join(RENDER_DIR, name)

        render_plugin(meta["time"], meta["high"], seconds=meta["time"] + 3.0, out_path=render_path)

        metrics, arrays = compare_pair(ref_path, render_path)
        metrics["filename"] = name
        metrics.update(meta)
        results.append(metrics)
        print(f"{name}: envCorr={metrics['envelope_correlation']:.3f} "
              f"LSD={metrics['log_spectral_distance_db']:.2f}dB "
              f"band(L/M/H)={metrics['spectral_band_diff_db']} "
              f"crestDiff={metrics['crest_factor_diff_db']}dB "
              f"flatnessDiff={metrics['spectral_flatness_diff_db']}dB")

    with open(os.path.join(HERE, "validation_results.json"), "w") as fh:
        json.dump(results, fh, indent=2)

    write_report(results)
    print(f"\nWrote {REPORT_PATH}")


def write_report(results):
    def avg(key, subset=None):
        rows = subset if subset is not None else results
        vals = [r[key] for r in rows if r.get(key) is not None]
        return (sum(vals) / len(vals)) if vals else None

    def band_avg(band, subset=None):
        rows = subset if subset is not None else results
        vals = [r["spectral_band_diff_db"][band] for r in rows if r["spectral_band_diff_db"].get(band) is not None]
        return (sum(vals) / len(vals)) if vals else None

    zero_low = [r for r in results if r["low"] == 0]
    nonzero_low = [r for r in results if r["low"] != 0]

    lines = []
    lines.append("# Phase D validation report\n")
    lines.append(
        "Plugin renders (via AuraRenderIR) vs. all 65 reference captures in "
        "`ml-toolkit/effects/ambience/captures/`, mapping each reference's Time and High "
        "directly (Aura's Time IS the hardware label - see module docstring). Low is passed "
        "through the filename but has no DSP effect (unwired - see AuraParameterMap.h), so "
        "results are split into Low=0 (fully comparable) and Low!=0 (expected extra error from "
        "the unmodeled knob) subsets.\n"
    )

    lines.append("## Overall EQ / tonal balance (the headline answer to \"is there a systematic bias\")\n")
    for label, subset, note in [("All 65 captures", results, ""), ("Low=0 only", zero_low, " (apples-to-apples, Low has no confound here)"), ("Low!=0 only", nonzero_low, " (Low's unmodeled effect expected to show up here)")]:
        lines.append(f"**{label}{note}** (n={len(subset)}):")
        lines.append(f"- Low band (20-500Hz):  {band_avg('low', subset):+.2f} dB average")
        lines.append(f"- Mid band (500-4000Hz): {band_avg('mid', subset):+.2f} dB average")
        lines.append(f"- High band (4000Hz+):   {band_avg('high', subset):+.2f} dB average")
        lines.append(f"- Log-spectral distance: {avg('log_spectral_distance_db', subset):.2f} dB average")
        lines.append("")
    lines.append(
        "Positive = plugin has MORE energy than the reference in that band (too bright/present "
        "there); negative = less (too dark/thin there). This is the overall EQ character check - "
        "distinct from whether turning the Low/High knobs shifts things the right amount, which "
        "is what ml-toolkit/effects/ambience/findings.md and cross_validation_report.md already "
        "checked on the Python model directly.\n"
    )

    lines.append("## Other metrics (Low=0 subset)\n")
    lines.append(f"- Envelope correlation: {avg('envelope_correlation', zero_low):.3f} (1.0 = identical shape)")
    lines.append(f"- Crest factor diff (plugin - reference): {avg('crest_factor_diff_db', zero_low):+.2f} dB "
                  "(negative = plugin more compressed/squashed than the real hardware)")
    lines.append(f"- Spectral flatness diff (plugin - reference): {avg('spectral_flatness_diff_db', zero_low):+.2f} dB "
                  "(negative = plugin tail more tonal/comb-filtered than the reference's diffuse character)")
    lines.append("")

    lines.append("## Per-setting results\n")
    lines.append("| file | Time | Low | High | env corr | LSD (dB) | EQ low/mid/high (dB) | crest diff (dB) | flatness diff (dB) |")
    lines.append("|---|---|---|---|---|---|---|---|---|")
    for r in sorted(results, key=lambda r: (r["low"] != 0, r["low"], r["high"], r["time"])):
        band = r["spectral_band_diff_db"]
        band_str = f"{band['low']:+.1f} / {band['mid']:+.1f} / {band['high']:+.1f}" if all(v is not None for v in band.values()) else "n/a"
        low_flag = "" if r["low"] == 0 else " **"
        lines.append(
            f"| {r['filename']}{low_flag} | {r['time']} | {r['low']} | {r['high']} | {r['envelope_correlation']:.3f} | "
            f"{r['log_spectral_distance_db']:.2f} | {band_str} | {r['crest_factor_diff_db']} | "
            f"{r['spectral_flatness_diff_db']} |"
        )
    lines.append("\n** = nonzero Low, so a known-unmodeled variable is in play for that row.\n")

    with open(REPORT_PATH, "w") as fh:
        fh.write("\n".join(lines))

    # A single plot of the per-file spectral band diff, split by Low=0 vs Low!=0, to make a
    # systematic bias visually obvious rather than only readable from the averaged numbers.
    fig, ax = plt.subplots(figsize=(10, 5))
    for band, color in [("low", "tab:blue"), ("mid", "tab:orange"), ("high", "tab:green")]:
        zero_vals = [r["spectral_band_diff_db"][band] for r in zero_low]
        ax.scatter(range(len(zero_vals)), zero_vals, label=f"{band} (Low=0)", color=color, marker="o")
    ax.axhline(0.0, color="black", linewidth=0.8)
    ax.set_title("Per-capture spectral band diff (plugin - reference), Low=0 subset")
    ax.set_xlabel("capture index")
    ax.set_ylabel("dB")
    ax.legend()
    fig.tight_layout()
    fig.savefig(os.path.join(PLOTS_DIR, "band_diff_overview.png"), dpi=120)
    plt.close(fig)


if __name__ == "__main__":
    main()
