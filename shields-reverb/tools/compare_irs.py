#!/usr/bin/env python3
"""Compares a rendered Shields impulse response against a reference Midiverb II capture.

Usage:
    python3 compare_irs.py <rendered.wav> <reference.wav> [--out report.png] [--window-ms 50]

Meant to be rerun after every parameter/topology change during tuning (build order step 3/4), not
a one-off script - it prints a small set of numbers to track across iterations and (unless
--no-plot is given) saves a PNG with the visual comparisons called for in the plan:

  1. RMS/energy decay envelope overlay - the overall loudness contour of both tails.
  2. Echo-density-over-time overlay - count of resolvable peaks per time window, which per the
     Bloom spec is the actual signature of the buildup, not the loudness contour above (a network
     can have a fairly flat or falling envelope while its echo density still visibly increases -
     see ShieldsFDNEngineTests.cpp for the same measurement done as a C++ regression check).
  3. Spectrogram comparison - tonal/bandwidth matching over TIME (rendered and reference side by
     side).
  4. Averaged spectrum overlay + spectral difference curve - tonal/bandwidth matching averaged over
     the WHOLE signal, frequency-resolved. This is what the single-number log-spectral-distance
     score below collapses into one figure; the difference curve is what actually answers "is it
     too bright, too dark, or off in some specific band" instead of just "off by how much overall."

Similarity is reported two ways rather than picking just one, since they capture different things:
  - Envelope correlation: normalised cross-correlation of the two RMS envelopes (0-1, higher is
    more similar) - a temporal/structural match ("does it swell and decay on the same timescale").
  - Log-spectral distance (dB): average |10*log10(psdA) - 10*log10(psdB)| across a Welch-averaged
    power spectrum (lower is more similar) - a broadband tonal match ("does it sound the same
    colour, on average, ignoring where"). See the spectral-difference plot (item 4 above) for the
    frequency-resolved version of this same comparison.
"""
import argparse
import sys

import numpy as np
from scipy.io import wavfile
from scipy.signal import resample_poly, spectrogram, welch


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
    peak - the same normalisation ShieldsFDNEngineTests.cpp uses, so density (not loudness) is what's
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


def averaged_psd(dataA, dataB, sampleRate, minHz=20.0):
    """Welch-averaged power spectra (many overlapping windows averaged together, not one FFT over
    the whole signal) - smoother and less dependent on exactly where the signal happens to be loud
    or quiet than a single long FFT would be. Returns LINEAR power (freqs, psdA, psdB) with
    everything below minHz dropped (DC/near-DC dominates a log-log dB comparison without being
    tonally meaningful for a reverb tail)."""
    n = min(len(dataA), len(dataB))
    a, b = dataA[:n], dataB[:n]

    nperseg = min(8192, n)
    freqs, psdA = welch(a, fs=sampleRate, nperseg=nperseg)
    _, psdB = welch(b, fs=sampleRate, nperseg=nperseg)

    keep = freqs >= minHz
    return freqs[keep], psdA[keep], psdB[keep]


def smooth_to_fractional_octave(freqs, psd, bandsPerOctave=3):
    """Averages linear power within fractional-octave bands (energy-summing, like a real RTA/1/3-
    octave analyzer) and returns (centerFreqs, smoothedDb). This is the standard way to compare
    tonal BALANCE while ignoring fine comb-filtering/resonance detail: two different delay-network
    topologies (this engine's 8+6 lines vs. whatever the real Midiverb uses internally) will always
    have resonant peaks at different exact frequencies, so a raw bin-by-bin comparison is dominated
    by peak-vs-null misalignment noise rather than genuine broadband colour - confirmed by how
    spiky (+-20-40dB bin to bin) the raw per-bin spectral-difference plot looked in practice, versus
    the much smoother trend this produces."""
    if len(freqs) == 0:
        return np.array([]), np.array([])

    minHz, maxHz = freqs[0], freqs[-1]
    if minHz <= 0.0:
        minHz = freqs[freqs > 0][0]

    numBands = max(1, int(np.log2(maxHz / minHz) * bandsPerOctave))
    centers = minHz * (2.0 ** (np.arange(numBands) / bandsPerOctave))

    floor = 1.0e-12
    smoothedDb = np.zeros(len(centers))
    bandWidthFactor = 2.0 ** (0.5 / bandsPerOctave)
    for i, center in enumerate(centers):
        inBand = (freqs >= center / bandWidthFactor) & (freqs < center * bandWidthFactor)
        bandPower = np.mean(psd[inBand]) if np.any(inBand) else floor
        smoothedDb[i] = 10.0 * np.log10(max(bandPower, floor))

    return centers, smoothedDb


def log_spectral_distance(freqDbA, freqDbB):
    return float(np.mean(np.abs(freqDbA - freqDbB)))


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("rendered", help="Shields's own rendered IR (from ShieldsRenderIR)")
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

    # Match overall level before any tonal/spectral comparison - the two captures' absolute gain is
    # arbitrary (depends on e.g. what preamp level the reference was recorded at), and left
    # unmatched it would show up as a uniform offset across every frequency band, masking whether a
    # real shape difference exists underneath it. Envelope correlation doesn't need this (corrcoef
    # is already scale-invariant); the spectral comparisons and the plots below do.
    renderedRms = float(np.sqrt(np.mean(renderedData.astype(np.float64) ** 2)))
    referenceRms = float(np.sqrt(np.mean(referenceData.astype(np.float64) ** 2)))
    if renderedRms > 0.0:
        renderedData = renderedData * (referenceRms / renderedRms)

    windowSize = max(1, int(targetRate * args.window_ms / 1000.0))

    rmsRendered = windowed_rms(renderedData, windowSize)
    rmsReference = windowed_rms(referenceData, windowSize)

    densityRendered = windowed_echo_density(renderedData, windowSize)
    densityReference = windowed_echo_density(referenceData, windowSize)

    correlation = envelope_correlation(rmsRendered, rmsReference)
    rawFreqs, rawPsdRendered, rawPsdReference = averaged_psd(renderedData, referenceData, targetRate)
    spectrumFreqs, spectrumDbRendered = smooth_to_fractional_octave(rawFreqs, rawPsdRendered)
    _, spectrumDbReference = smooth_to_fractional_octave(rawFreqs, rawPsdReference)
    lsd = log_spectral_distance(spectrumDbRendered, spectrumDbReference)

    levelMatchDb = 20.0 * np.log10(referenceRms / renderedRms) if renderedRms > 0.0 else 0.0
    print(f"Sample rate (comparison):   {targetRate} Hz")
    print(f"Rendered length:            {len(renderedData) / targetRate:.2f}s")
    print(f"Reference length:           {len(referenceData) / targetRate:.2f}s")
    print(f"Level-matched rendered by:  {levelMatchDb:+.2f} dB (to reference's overall RMS, before any tonal comparison below)")
    print(f"Envelope correlation:       {correlation:.4f}  (1.0 = identical shape, 0 = uncorrelated)")
    print(f"Log-spectral distance:      {lsd:.2f} dB      (lower = closer tonal match)")

    # A quick "where" for the tonal mismatch without having to open the plot: positive = rendered
    # has MORE energy than reference in that band (too bright/present there), negative = less.
    spectrumDiff = spectrumDbRendered - spectrumDbReference
    bands = [("Low (20-500Hz)", 20.0, 500.0), ("Mid (500-4000Hz)", 500.0, 4000.0), ("High (4000Hz+)", 4000.0, spectrumFreqs[-1])]
    print("Spectral balance by band (rendered minus reference, dB):")
    for label, lowHz, highHz in bands:
        inBand = (spectrumFreqs >= lowHz) & (spectrumFreqs <= highHz)
        if np.any(inBand):
            print(f"  {label:<18} {np.mean(spectrumDiff[inBand]):+.2f} dB")

    if args.no_plot:
        return

    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    fig, axes = plt.subplots(6, 1, figsize=(10, 19))

    timeRendered = np.arange(len(rmsRendered)) * args.window_ms / 1000.0
    timeReference = np.arange(len(rmsReference)) * args.window_ms / 1000.0
    axes[0].plot(timeRendered, rmsRendered, label="Rendered (Shields)")
    axes[0].plot(timeReference, rmsReference, label="Reference (Midiverb)")
    axes[0].set_title("RMS energy envelope")
    axes[0].set_xlabel("Time (s)")
    axes[0].set_ylabel("RMS")
    axes[0].legend()

    axes[1].plot(timeRendered, densityRendered, label="Rendered (Shields)")
    axes[1].plot(timeReference, densityReference, label="Reference (Midiverb)")
    axes[1].set_title("Echo density (peaks per window) - the actual buildup signature")
    axes[1].set_xlabel("Time (s)")
    axes[1].set_ylabel("Peak count")
    axes[1].legend()

    nperseg = min(1024, max(64, windowSize))
    fRendered, tRendered, sxxRendered = spectrogram(renderedData, fs=targetRate, nperseg=nperseg)
    axes[2].pcolormesh(tRendered, fRendered, 10 * np.log10(sxxRendered + 1.0e-12), shading="gouraud")
    axes[2].set_title("Rendered (Shields) spectrogram")
    axes[2].set_xlabel("Time (s)")
    axes[2].set_ylabel("Frequency (Hz)")

    fReference, tReference, sxxReference = spectrogram(referenceData, fs=targetRate, nperseg=nperseg)
    axes[3].pcolormesh(tReference, fReference, 10 * np.log10(sxxReference + 1.0e-12), shading="gouraud")
    axes[3].set_title("Reference (Midiverb) spectrogram")
    axes[3].set_xlabel("Time (s)")
    axes[3].set_ylabel("Frequency (Hz)")

    # Averaged-over-the-whole-signal spectrum, frequency-resolved - the log-spectral-distance score
    # printed above is just the mean |gap| between these two curves, collapsed to one number.
    axes[4].semilogx(spectrumFreqs, spectrumDbRendered, label="Rendered (Shields)")
    axes[4].semilogx(spectrumFreqs, spectrumDbReference, label="Reference (Midiverb)")
    axes[4].set_title("Averaged spectrum (Welch PSD, 1/3-octave smoothed)")
    axes[4].set_xlabel("Frequency (Hz)")
    axes[4].set_ylabel("Power (dB)")
    axes[4].legend()

    # The actual answer to "where is the tonal mismatch": positive = rendered has more energy than
    # the reference in that band (too bright/present there), negative = less (too dark/thin there).
    axes[5].semilogx(spectrumFreqs, spectrumDiff, color="tab:red")
    axes[5].axhline(0.0, color="black", linewidth=0.8)
    axes[5].set_title("Spectral difference (Rendered minus Reference)")
    axes[5].set_xlabel("Frequency (Hz)")
    axes[5].set_ylabel("dB")

    fig.tight_layout()

    outPath = args.out or (args.rendered.rsplit(".", 1)[0] + "-comparison.png")
    fig.savefig(outPath, dpi=150)
    print(f"Saved comparison plot to {outPath}")


if __name__ == "__main__":
    sys.exit(main())
