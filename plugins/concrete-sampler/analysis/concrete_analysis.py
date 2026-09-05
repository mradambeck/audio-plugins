"""Analysis primitives for Concrete (see concrete-sampler-plugin-plan.md's Phase 0 / Ground rules).

Reuse ../../common/tools/compare_wavs.py for null tests, envelope correlation, and log-spectral
distance where it already covers what's needed. This module holds only genuinely new primitives
that file doesn't have:

  - FFT magnitude spectrum + peak/partial detection -- needed starting with Phase 0's own
    verification (a 1kHz sine must show exactly one partial, no harmonics).
  - A sample-accurate null-test/residual helper -- compare_wavs.py's comparisons are all
    approximate/statistical (envelope correlation, log-spectral distance), not a literal
    sample-by-sample difference, which Phase 4's capture-pass non-destructiveness test and
    Phase 5's double-smear regression test both need (e.g. "must null to silence").

THD, noise-floor, spectral-centroid, and image/alias-energy-above-a-frequency measurements are
deliberately NOT here yet -- they get added in Phase 2/3/4/7 when those phases first need them,
per this repo's ml-toolkit "build only what the current effect needs" convention (see AGENTS.md).
"""
import numpy as np
from scipy.io import wavfile
from scipy.signal import find_peaks


def load_wav(path):
    """Returns (sampleRate, samples) as float64 in [-1, 1], shape (numSamples,) for mono or
    (numSamples, numChannels) for multi-channel. Normalizes integer PCM; float WAVs pass through."""
    rate, data = wavfile.read(path)
    if np.issubdtype(data.dtype, np.integer):
        data = data.astype(np.float64) / np.iinfo(data.dtype).max
    else:
        data = data.astype(np.float64)
    return rate, data


def to_mono(data):
    return data if data.ndim == 1 else data.mean(axis=1)


def magnitude_spectrum_db(signal, sample_rate):
    """Windowed FFT magnitude spectrum in dB, normalized so a full-scale sine reads ~0dBFS
    regardless of window length. Returns (freqs_hz, magnitude_db)."""
    n = len(signal)
    window = np.hanning(n)
    spectrum = np.fft.rfft(signal * window)
    freqs = np.fft.rfftfreq(n, d=1.0 / sample_rate)
    coherent_gain = window.sum() / 2.0  # single-bin sinusoid normalization
    magnitude = np.abs(spectrum) / max(coherent_gain, 1e-12)
    magnitude_db = 20.0 * np.log10(np.maximum(magnitude, 1e-12))
    return freqs, magnitude_db


def find_partials(signal, sample_rate, prominence_db=20.0, min_freq_hz=20.0):
    """Local-maxima spectral peaks at least prominence_db above their immediate neighbors, sorted
    loudest-first. Returns a list of (freq_hz, magnitude_db)."""
    freqs, magnitude_db = magnitude_spectrum_db(signal, sample_rate)
    valid = freqs >= min_freq_hz
    freqs, magnitude_db = freqs[valid], magnitude_db[valid]
    peak_indices, _ = find_peaks(magnitude_db, prominence=prominence_db)
    peaks = [(float(freqs[i]), float(magnitude_db[i])) for i in peak_indices]
    peaks.sort(key=lambda p: -p[1])
    return peaks


def residual_db(signal_a, signal_b):
    """Sample-accurate null-test: RMS level of (a - b) relative to full scale, in dB. For proving
    non-destructiveness / bypass-equivalence ("must null to silence"), not the approximate
    envelope/spectral comparisons compare_wavs.py already covers. Truncates to the shorter length."""
    n = min(len(signal_a), len(signal_b))
    diff = signal_a[:n] - signal_b[:n]
    rms = float(np.sqrt(np.mean(diff ** 2)))
    return 20.0 * np.log10(max(rms, 1e-12))
