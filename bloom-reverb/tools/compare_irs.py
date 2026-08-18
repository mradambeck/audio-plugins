#!/usr/bin/env python3
"""Compares a rendered Bloom impulse response against a reference Midiverb II capture.

Usage:
    python3 compare_irs.py <rendered.wav> <reference.wav> [--out report.png] [--window-ms 50]

Meant to be rerun after every parameter/topology change during tuning (build order step 3/4), not
a one-off script - it prints a small set of numbers to track across iterations and (unless
--no-plot is given) saves a PNG with the three visual comparisons called for in the plan:

  1. RMS/energy decay envelope overlay - the overall loudness contour of both tails.
  2. Echo-density-over-time overlay - count of resolvable peaks per time window, which per the
     Bloom spec is the actual signature of the buildup, not the loudness contour above (a network
     can have a fairly flat or falling envelope while its echo density still visibly increases -
     see BloomFDNEngineTests.cpp for the same measurement done as a C++ regression check).
  3. Spectrogram comparison - tonal/bandwidth matching (this is what the Bandwidth/Bit Depth
     parameters get tuned against).

Similarity is reported two ways rather than picking just one, since they capture different things:
  - Envelope correlation: normalised cross-correlation of the two RMS envelopes (0-1, higher is
    more similar) - a temporal/structural match ("does it swell and decay on the same timescale").
  - Log-spectral distance (dB): average |20*log10(magA) - 20*log10(magB)| across the analysis
    window's spectrum (lower is more similar) - a tonal match ("does it sound the same colour").
"""
import argparse
import sys

import numpy as np
from scipy.io import wavfile
from scipy.signal import resample_poly, spectrogram


def load_mono(path):
    """Loads a WAV file, mixes to mono, and returns (samples as float32 in [-1, 1], sample rate)."""
    sample_rate, data = wavfile.read(path)
    data = np.asarray(data)

    if data.dtype.kind == "i":
        data = data.astype(np.float32) / np.iinfo(data.dtype).max
    else:
        data = data.astype(np.float32)

    if data.ndim > 1:
        data = data.mean(axis=1)

    return data, sample_rate


def resample_to(data, sourceRate, targetRate):
    if sourceRate == targetRate:
        return data
    from math import gcd
    g = gcd(sourceRate, targetRate)
    return resample_poly(data, targetRate // g, sourceRate // g).astype(np.float32)


def find_onset(data, relativeThreshold=0.05):
    """First sample index whose magnitude clears relativeThreshold * peak - used to align the two
    recordings' starts so differing pre-roll silence doesn't offset every downstream comparison."""
    peak = np.max(np.abs(data))
    if peak <= 0.0:
        return 0
    threshold = peak * relativeThreshold
    indices = np.nonzero(np.abs(data) >= threshold)[0]
    return int(indices[0]) if len(indices) else 0


def windowed_rms(data, windowSize):
    numWindows = len(data) // windowSize
    trimmed = data[: numWindows * windowSize].reshape(numWindows, windowSize)
    return np.sqrt(np.mean(trimmed.astype(np.float64) ** 2, axis=1))


def windowed_echo_density(data, windowSize, relativeThreshold=0.2):
    """Count of resolvable local-maxima peaks per window, thresholded relative to that window's own
    peak - the same normalisation BloomFDNEngineTests.cpp uses, so density (not loudness) is what's
    being measured. Deliberately per-window relative, not a single global threshold, since a
    diffuse tail's absolute level keeps falling even while its structural density keeps rising."""
    numWindows = len(data) // windowSize
    densities = np.zeros(numWindows, dtype=np.int64)

    for w in range(numWindows):
        window = np.abs(data[w * windowSize : (w + 1) * windowSize])
        windowPeak = np.max(window)
        if windowPeak <= 0.0:
            continue
        threshold = windowPeak * relativeThreshold
        isLocalMax = (window[1:-1] >= window[:-2]) & (window[1:-1] >= window[2:]) & (window[1:-1] >= threshold)
        densities[w] = int(np.count_nonzero(isLocalMax))

    return densities


def envelope_correlation(rmsA, rmsB):
    n = min(len(rmsA), len(rmsB))
    a, b = rmsA[:n], rmsB[:n]
    if np.std(a) == 0 or np.std(b) == 0:
        return 0.0
    return float(np.corrcoef(a, b)[0, 1])


def log_spectral_distance(dataA, dataB, sampleRate):
    n = min(len(dataA), len(dataB))
    a, b = dataA[:n], dataB[:n]

    window = np.hanning(n)
    specA = np.abs(np.fft.rfft(a * window))
    specB = np.abs(np.fft.rfft(b * window))

    floor = 1.0e-6
    logA = 20.0 * np.log10(np.maximum(specA, floor))
    logB = 20.0 * np.log10(np.maximum(specB, floor))

    return float(np.mean(np.abs(logA - logB)))


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("rendered", help="Bloom's own rendered IR (from BloomRenderIR)")
    parser.add_argument("reference", help="Captured reference IR (reference-irs/*.wav)")
    parser.add_argument("--out", default=None, help="Path to save the comparison PNG (default: alongside the rendered file)")
    parser.add_argument("--no-plot", action="store_true", help="Skip saving the PNG, just print the metrics")
    parser.add_argument("--window-ms", type=float, default=50.0, help="Analysis window size in ms (default 50)")
    args = parser.parse_args()

    renderedData, renderedRate = load_mono(args.rendered)
    referenceData, referenceRate = load_mono(args.reference)

    targetRate = max(renderedRate, referenceRate)
    renderedData = resample_to(renderedData, renderedRate, targetRate)
    referenceData = resample_to(referenceData, referenceRate, targetRate)

    renderedData = renderedData[find_onset(renderedData):]
    referenceData = referenceData[find_onset(referenceData):]

    windowSize = max(1, int(targetRate * args.window_ms / 1000.0))

    rmsRendered = windowed_rms(renderedData, windowSize)
    rmsReference = windowed_rms(referenceData, windowSize)

    densityRendered = windowed_echo_density(renderedData, windowSize)
    densityReference = windowed_echo_density(referenceData, windowSize)

    correlation = envelope_correlation(rmsRendered, rmsReference)
    lsd = log_spectral_distance(renderedData, referenceData, targetRate)

    print(f"Sample rate (comparison):   {targetRate} Hz")
    print(f"Rendered length:            {len(renderedData) / targetRate:.2f}s")
    print(f"Reference length:           {len(referenceData) / targetRate:.2f}s")
    print(f"Envelope correlation:       {correlation:.4f}  (1.0 = identical shape, 0 = uncorrelated)")
    print(f"Log-spectral distance:      {lsd:.2f} dB      (lower = closer tonal match)")

    if args.no_plot:
        return

    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    fig, axes = plt.subplots(4, 1, figsize=(10, 13))

    timeRendered = np.arange(len(rmsRendered)) * args.window_ms / 1000.0
    timeReference = np.arange(len(rmsReference)) * args.window_ms / 1000.0
    axes[0].plot(timeRendered, rmsRendered, label="Rendered (Bloom)")
    axes[0].plot(timeReference, rmsReference, label="Reference (Midiverb)")
    axes[0].set_title("RMS energy envelope")
    axes[0].set_xlabel("Time (s)")
    axes[0].set_ylabel("RMS")
    axes[0].legend()

    axes[1].plot(timeRendered, densityRendered, label="Rendered (Bloom)")
    axes[1].plot(timeReference, densityReference, label="Reference (Midiverb)")
    axes[1].set_title("Echo density (peaks per window) - the actual buildup signature")
    axes[1].set_xlabel("Time (s)")
    axes[1].set_ylabel("Peak count")
    axes[1].legend()

    nperseg = min(1024, max(64, windowSize))
    fRendered, tRendered, sxxRendered = spectrogram(renderedData, fs=targetRate, nperseg=nperseg)
    axes[2].pcolormesh(tRendered, fRendered, 10 * np.log10(sxxRendered + 1.0e-12), shading="gouraud")
    axes[2].set_title("Rendered (Bloom) spectrogram")
    axes[2].set_xlabel("Time (s)")
    axes[2].set_ylabel("Frequency (Hz)")

    fReference, tReference, sxxReference = spectrogram(referenceData, fs=targetRate, nperseg=nperseg)
    axes[3].pcolormesh(tReference, fReference, 10 * np.log10(sxxReference + 1.0e-12), shading="gouraud")
    axes[3].set_title("Reference (Midiverb) spectrogram")
    axes[3].set_xlabel("Time (s)")
    axes[3].set_ylabel("Frequency (Hz)")

    fig.tight_layout()

    outPath = args.out or (args.rendered.rsplit(".", 1)[0] + "-comparison.png")
    fig.savefig(outPath, dpi=150)
    print(f"Saved comparison plot to {outPath}")


if __name__ == "__main__":
    sys.exit(main())
