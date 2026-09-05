#!/usr/bin/env python3
"""Generates Concrete's fixed set of test source WAVs into ../test-assets/ (gitignored -
regenerate by rerunning this script; see concrete-sampler-plugin-plan.md's Phase 0).

Six signals, each exercising a different thing later phases need:
  - 1kHz sine / 100Hz sine: single-partial reference tones (Phase 0/1's clean-playback baseline,
    Phase 2's image/alias measurements).
  - white noise burst: broadband content for noise-floor/companding measurements (Phase 3).
  - impulse: a single full-scale sample followed by silence, for filter frequency-response
    measurements (Phase 5).
  - bright transient (synthesized click/snare-like): fast attack + short exponential decay with
    real high-frequency content, for the capture-pass alias-energy-above-8kHz measurement
    (Phase 4) and the STANDALONE CHECK listening tests.
  - sine sweep: 20Hz-20kHz log sweep, for confirming the reference path tracks input with no
    discontinuities (Phase 1) and for frequency-response sweeps (Phase 5).

Usage: python3 make_test_assets.py
"""
import os

import numpy as np
from scipy.io import wavfile

SAMPLE_RATE = 44100
OUT_DIR = os.path.join(os.path.dirname(__file__), "..", "test-assets")


def write(name, signal):
    os.makedirs(OUT_DIR, exist_ok=True)
    path = os.path.join(OUT_DIR, name)
    wavfile.write(path, SAMPLE_RATE, signal.astype(np.float32))
    print(f"Wrote {path} ({len(signal) / SAMPLE_RATE:.2f}s)")


def sine(freq_hz, duration_s, amplitude=0.5):
    t = np.arange(int(duration_s * SAMPLE_RATE)) / SAMPLE_RATE
    return (amplitude * np.sin(2.0 * np.pi * freq_hz * t)).astype(np.float64)


def white_noise_burst(duration_s=1.0, amplitude=0.5, seed=12345):
    rng = np.random.default_rng(seed)
    return amplitude * rng.uniform(-1.0, 1.0, int(duration_s * SAMPLE_RATE))


def impulse(duration_s=1.0):
    signal = np.zeros(int(duration_s * SAMPLE_RATE))
    signal[0] = 1.0
    return signal


def bright_transient(duration_s=1.0, amplitude=0.9, decay_s=0.05, seed=54321):
    rng = np.random.default_rng(seed)
    n = int(duration_s * SAMPLE_RATE)
    t = np.arange(n) / SAMPLE_RATE
    noise = rng.uniform(-1.0, 1.0, n)
    envelope = np.exp(-t / decay_s)
    return amplitude * noise * envelope


def sine_sweep(duration_s=2.0, f0=20.0, f1=20000.0, amplitude=0.5):
    n = int(duration_s * SAMPLE_RATE)
    t = np.arange(n) / SAMPLE_RATE
    # Logarithmic (constant-Q) sweep: instantaneous phase for an exponential frequency ramp.
    k = (f1 / f0) ** (1.0 / duration_s)
    phase = 2.0 * np.pi * f0 * (np.power(k, t) - 1.0) / np.log(k)
    return amplitude * np.sin(phase)


def main():
    write("sine_1khz.wav", sine(1000.0, 2.0))
    write("sine_100hz.wav", sine(100.0, 2.0))
    write("white_noise_burst.wav", white_noise_burst())
    write("impulse.wav", impulse())
    write("bright_transient.wav", bright_transient())
    write("sine_sweep.wav", sine_sweep())


if __name__ == "__main__":
    main()
