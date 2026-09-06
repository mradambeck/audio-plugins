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
  - THD and energy-above-a-frequency -- needed starting with Phase 1's own reference-path
    verification ("THD < 0.1%", "no energy above 2kHz beyond the noise floor"), a phase earlier
    than originally guessed when this module was first written.

Spectral-centroid and image/alias-energy-vs-transpose measurements are still deliberately NOT
here -- they get added in Phase 2/7 when those phases first need them, per this repo's ml-toolkit
"build only what the current effect needs" convention (see AGENTS.md).
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


def thd_percent(signal, sample_rate, fundamental_hz, num_harmonics=10):
    """Total Harmonic Distortion, as a percentage: RMS of the 2nd..Nth harmonics' magnitude over
    the fundamental's magnitude. Reads magnitude directly at the fundamental's and each harmonic's
    exact expected bin (nearest-bin lookup, not a peak search) so a low-THD measurement isn't
    thrown off by find_partials() picking up an unrelated nearby peak instead."""
    freqs, magnitude_db = magnitude_spectrum_db(signal, sample_rate)
    bin_width = freqs[1] - freqs[0]

    def magnitude_at(freq_hz):
        index = int(round(freq_hz / bin_width))
        if index <= 0 or index >= len(magnitude_db):
            return 0.0
        return 10.0 ** (magnitude_db[index] / 20.0)

    fundamental_magnitude = magnitude_at(fundamental_hz)
    if fundamental_magnitude <= 0.0:
        return float("inf")

    harmonic_energy = sum(magnitude_at(fundamental_hz * n) ** 2 for n in range(2, num_harmonics + 1))
    return 100.0 * np.sqrt(harmonic_energy) / fundamental_magnitude


def energy_above_freq_db(signal, sample_rate, freq_hz):
    """RMS level, in dBFS, of spectral energy at frequencies >= freq_hz - e.g. "is there anything
    above 2kHz that shouldn't be there" for a 1kHz reference tone."""
    freqs, magnitude_db = magnitude_spectrum_db(signal, sample_rate)
    magnitude = 10.0 ** (magnitude_db / 20.0)
    mask = freqs >= freq_hz
    rms = float(np.sqrt(np.mean(magnitude[mask] ** 2))) if np.any(mask) else 0.0
    return 20.0 * np.log10(max(rms, 1e-12))


def residual_db(signal_a, signal_b):
    """Sample-accurate null-test: RMS level of (a - b) relative to full scale, in dB. For proving
    non-destructiveness / bypass-equivalence ("must null to silence"), not the approximate
    envelope/spectral comparisons compare_wavs.py already covers. Truncates to the shorter length."""
    n = min(len(signal_a), len(signal_b))
    diff = signal_a[:n] - signal_b[:n]
    rms = float(np.sqrt(np.mean(diff ** 2)))
    return 20.0 * np.log10(max(rms, 1e-12))
