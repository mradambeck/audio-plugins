#!/usr/bin/env python3
"""Phase 6: validation. Renders the actual IntruderAudioProcessor (via IntruderRenderIR, not a
re-implementation) at settings matched to each of the 19 reference captures in ir-captures/, then
compares plugin output against the real hardware capture on more than just amplitude/envelope:

  - Envelope shape & echo density (reused from ../../common/tools/compare_wavs.py)
  - EQ / tonal balance: log-spectral distance + per-band spectral balance (reused from
    compare_wavs.py) - "is it too bright/dark, and where"
  - Compression / dynamics: windowed crest factor (peak/RMS in dB) over the decay - a reverb
    that's been squashed (e.g. by an overly-dense FDN, or too much smoothing) reads as a LOWER,
    flatter crest factor than the reference even when the RMS envelope shape matches
  - Harmonic/resonant character: spectral flatness (Wiener entropy) over the tail - a delay-network
    reverb with too few/too-aligned lines reads as comb-filtered (lower flatness, more tonal/peaky)
    versus a real diffuse tail's higher, noise-like flatness, even when the broadband EQ matches

Mapping from a reference filename's (decay label, H, Tighter) to plugin parameters:
  - decaySeconds: the file's own measured full_decay_rt60_s (from features.json) - a wide-window
    (-6..-70dB) linear fit to the RMS envelope, NOT schroeder_rt60_s. The first validation pass
    used schroeder_rt60_s and found the plugin decaying much faster than every reference capture
    even at matched "RT60" (see validation_report.md's finding #2): schroeder_rt60_s is fit only
    over the narrow -5..-25dB region, which these non-exponential (hold-then-decay) captures'
    early portion dominates, so it badly underestimates how long the capture actually keeps
    decaying - visibly out past -70dB on every envelope plot. full_decay_rt60_s fits that much
    wider, more representative range instead (see analyze_irs.py's full_decay_rt60()).
  - tiltDb: the H value directly - the plugin's Tilt parameter is defined in the same units
  - tighter: 0 or 100 depending on the filename's Tighter flag

Requires ir-captures/, analysis/features.json (run analyze_irs.py first), and a built
IntruderRenderIR (../build/IntruderRenderIR_artefacts/Release/IntruderRenderIR - build it first,
console target only, never the AU/VST3 plugin target).
"""
import json
import os
import re
import subprocess
import sys

import numpy as np
import soundfile as sf
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "common", "tools"))
import compare_wavs as cw

HERE = os.path.dirname(__file__)
IR_DIR = os.path.join(HERE, "..", "ir-captures")
FEATURES_PATH = os.path.join(HERE, "features.json")
RENDER_IR_BIN = os.path.join(HERE, "..", "build", "IntruderRenderIR_artefacts", "Release", "IntruderRenderIR")
RENDER_DIR = os.path.join(HERE, "validation_renders")
PLOTS_DIR = os.path.join(HERE, "validation_plots")
REPORT_PATH = os.path.join(HERE, "validation_report.md")

NAME_RE = re.compile(
    r"^NonLin_(?P<decay>[\d.]+)s_(?P<h>-?\d+)H(?:_(?P<variant>\d+))?(?:_(?P<tighter>Tighter))?\.wav$"
)

os.makedirs(RENDER_DIR, exist_ok=True)
os.makedirs(PLOTS_DIR, exist_ok=True)


def tail_segment(data, sampleRate, startFrac=0.3, endFrac=0.9):
    n = len(data)
    return data[int(n * startFrac) : int(n * endFrac)]


def parse_filename(name):
    m = NAME_RE.match(name)
    return {
        "decay_label_s": float(m.group("decay")),
        "h_db": int(m.group("h")),
        "tighter": m.group("tighter") is not None,
    }


def render_plugin(decay_seconds, h_db, tighter, seconds, out_path):
    cmd = [
        RENDER_IR_BIN,
        "--out", out_path,
        "--seconds", str(seconds),
        "--decaySeconds", str(decay_seconds),
        "--tiltDb", str(h_db),
        "--tighter", "100" if tighter else "0",
        "--mixPercent", "100",
    ]
    subprocess.run(cmd, check=True, capture_output=True)


def compare_pair(ref_path, render_path, sample_rate_hint):
    refData, refRate = cw.load_mono(ref_path)
    rendData, rendRate = cw.load_mono(render_path)

    targetRate = max(refRate, rendRate)
    refData = cw.resample_to(refData, refRate, targetRate)
    rendData = cw.resample_to(rendData, rendRate, targetRate)

    refData = refData[cw.find_onset(refData):]
    rendData = rendData[cw.find_onset(rendData):]

    refRms = float(np.sqrt(np.mean(refData.astype(np.float64) ** 2)))
    rendRms = float(np.sqrt(np.mean(rendData.astype(np.float64) ** 2)))
    if rendRms > 0.0:
        rendDataMatched = rendData * (refRms / rendRms)
    else:
        rendDataMatched = rendData

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
    crestDiff = (crestRendMean - crestRefMean) if (crestRef is not None and crestRefMean is not None and crestRendMean is not None) else None

    flatRef = cw.spectral_flatness_db(tail_segment(refData, targetRate), targetRate)
    flatRend = cw.spectral_flatness_db(tail_segment(rendDataMatched, targetRate), targetRate)
    flatDiff = (flatRend - flatRef) if (flatRef is not None and flatRend is not None) else None

    return {
        "envelope_correlation": round(correlation, 4),
        "log_spectral_distance_db": round(lsd, 3),
        "spectral_band_diff_db": {k: (round(v, 2) if v is not None else None) for k, v in bandDiffs.items()},
        "crest_factor_ref_db": round(crestRefMean, 2) if crestRefMean is not None else None,
        "crest_factor_render_db": round(crestRendMean, 2) if crestRendMean is not None else None,
        "crest_factor_diff_db": round(crestDiff, 2) if crestDiff is not None else None,
        "spectral_flatness_ref_db": round(flatRef, 2) if flatRef is not None else None,
        "spectral_flatness_render_db": round(flatRend, 2) if flatRend is not None else None,
        "spectral_flatness_diff_db": round(flatDiff, 2) if flatDiff is not None else None,
    }, (refData, rendDataMatched, targetRate, rmsRef, rmsRend, crestRef, crestRend)


def main():
    if not os.path.exists(RENDER_IR_BIN):
        print(f"IntruderRenderIR not found at {RENDER_IR_BIN} - build it first (console target only).", file=sys.stderr)
        sys.exit(1)

    features = {f["filename"]: f for f in json.load(open(FEATURES_PATH))}

    files = sorted(f for f in os.listdir(IR_DIR) if f.endswith(".wav"))
    results = []

    for name in files:
        meta = parse_filename(name)
        feat = features[name]
        ref_path = os.path.join(IR_DIR, name)
        render_path = os.path.join(RENDER_DIR, name)

        render_plugin(
            decay_seconds=max(0.1, feat["full_decay_rt60_s"] or feat["schroeder_rt60_s"] or 0.5),
            h_db=meta["h_db"],
            tighter=meta["tighter"],
            seconds=max(1.0, feat["duration_s"] + 0.2),
            out_path=render_path,
        )

        metrics, arrays = compare_pair(ref_path, render_path, None)
        metrics["filename"] = name
        metrics["decay_label_s"] = meta["decay_label_s"]
        metrics["h_db"] = meta["h_db"]
        metrics["tighter"] = meta["tighter"]
        metrics["ref_rt60_s"] = feat["schroeder_rt60_s"]
        metrics["ref_full_decay_rt60_s"] = feat["full_decay_rt60_s"]
        results.append(metrics)
        print(f"{name}: envCorr={metrics['envelope_correlation']:.3f} "
              f"LSD={metrics['log_spectral_distance_db']:.2f}dB "
              f"crestDiff={metrics['crest_factor_diff_db']}dB "
              f"flatnessDiff={metrics['spectral_flatness_diff_db']}dB")

        refData, rendData, sr, refRms, rendRms, crestRef, crestRend = arrays
        fig, axes = plt.subplots(2, 1, figsize=(9, 6))
        t_ref = np.arange(len(crestRef)) * 0.03
        t_rend = np.arange(len(crestRend)) * 0.03
        axes[0].plot(t_ref, crestRef, label="reference")
        axes[0].plot(t_rend, crestRend, label="plugin")
        axes[0].set_title(f"{name} - crest factor over time (30ms windows)")
        axes[0].set_ylabel("dB")
        axes[0].legend()

        env_ref = cw.windowed_rms(refData, max(1, int(sr * 0.02)))
        env_rend = cw.windowed_rms(rendData, max(1, int(sr * 0.02)))
        t2_ref = np.arange(len(env_ref)) * 0.02
        t2_rend = np.arange(len(env_rend)) * 0.02
        axes[1].plot(t2_ref, 20 * np.log10(env_ref + 1e-9), label="reference")
        axes[1].plot(t2_rend, 20 * np.log10(env_rend + 1e-9), label="plugin")
        axes[1].set_title("RMS envelope (dB)")
        axes[1].set_ylabel("dB")
        axes[1].legend()
        fig.tight_layout()
        fig.savefig(os.path.join(PLOTS_DIR, name.replace(".wav", ".png")), dpi=100)
        plt.close(fig)

    with open(os.path.join(HERE, "validation_results.json"), "w") as fh:
        json.dump(results, fh, indent=2)

    write_report(results)
    print(f"\nWrote {REPORT_PATH}")


def write_report(results):
    def avg(key):
        vals = [r[key] for r in results if r.get(key) is not None]
        return sum(vals) / len(vals) if vals else None

    lines = []
    lines.append("# Phase 6 validation report\n")
    lines.append("Plugin renders (via IntruderRenderIR) vs. the 19 reference captures in `ir-captures/`, "
                  "mapping each reference's Decay to its own measured RT60, H directly to Tilt, and "
                  "Tighter to 0/100%.\n")
    lines.append("Metrics: envelope correlation and log-spectral distance / per-band EQ balance are "
                  "reused from `../../common/tools/compare_wavs.py`. Crest factor (compression/dynamics) "
                  "and spectral flatness (harmonic/resonant character) are new, added per Adam's request "
                  "not to validate on amplitude alone.\n")

    lines.append("## Summary (averaged across all 19 settings)\n")
    lines.append(f"- Envelope correlation: {avg('envelope_correlation'):.3f} (1.0 = identical shape)")
    lines.append(f"- Log-spectral distance: {avg('log_spectral_distance_db'):.2f} dB (lower = closer EQ match)")
    lines.append(f"- Crest factor diff (plugin - reference): {avg('crest_factor_diff_db'):+.2f} dB "
                  "(negative = plugin sounds more compressed/squashed than the real hardware; "
                  "positive = plugin is peakier/less dense than the reference)")
    lines.append(f"- Spectral flatness diff (plugin - reference): {avg('spectral_flatness_diff_db'):+.2f} dB "
                  "(negative = plugin tail is more tonal/comb-filtered than the reference's diffuse "
                  "character; near 0 = matched)")
    lines.append("")

    lines.append("## Per-setting results\n")
    lines.append("| file | schroeder RT60 (s) | full-decay RT60 (s, used as Decay) | env corr | LSD (dB) | EQ low/mid/high (dB) | crest diff (dB) | flatness diff (dB) |")
    lines.append("|---|---|---|---|---|---|---|---|")
    for r in results:
        band = r["spectral_band_diff_db"]
        band_str = f"{band['low']:+.1f} / {band['mid']:+.1f} / {band['high']:+.1f}" if all(v is not None for v in band.values()) else "n/a"
        lines.append(
            f"| {r['filename']} | {r['ref_rt60_s']} | {r['ref_full_decay_rt60_s']} | {r['envelope_correlation']:.3f} | "
            f"{r['log_spectral_distance_db']:.2f} | {band_str} | {r['crest_factor_diff_db']} | "
            f"{r['spectral_flatness_diff_db']} |"
        )
    lines.append("")

    with open(REPORT_PATH, "w") as fh:
        fh.write("\n".join(lines))


if __name__ == "__main__":
    main()
