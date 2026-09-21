"""Generates a probe to play through a real reverb (hardware or plugin) to capture its impulse
response, for later ML fitting (the same role effects/ambience's 65 hand-captured .wav files
already fill for the AMS RMX16).

This lives under signal_generation/, alongside the modulation/distortion/diffuser probe
generators, not under effects/ - these are all test-signal generators for a hardware capture
session, not effect implementations themselves.

Unlike signal_generation/modulation's probe set, a reverb is (to first order) LTI, so a single
well-chosen probe recovers its full IR. Two are generated:

  - dirac_impulse.wav       - a single full-scale sample, for units that can take a true impulse
                              cleanly (fine on most digital gear, but easy to clip/distort analog
                              hardware, and its huge crest factor buries quiet late reverb tail in
                              noise on a real recording chain).
  - ess_sweep.wav + ess_inverse_filter.wav
                              - the standard exponential sine sweep (ESS) method: play the sweep
                              through the unit, record the wet output, then convolve it with the
                              paired inverse filter to recover the IR. Much better SNR/dynamic
                              range on the decay tail than a raw impulse, and separates harmonic
                              distortion into non-causal pre-response - the reason this is the
                              standard method for real (not simulated) IR capture. Prefer this pair
                              over the dirac unless the target is a plugin with no capture noise.

Recovering the IR from the ESS pair: convolve the wet recording with ess_inverse_filter.wav (e.g.
scipy.signal.fftconvolve(wet, inverse, mode="full")) and trim to the sweep's own duration from the
point where the direct-path peak lands.
"""
from __future__ import annotations

import os

import numpy as np
import soundfile as sf

SAMPLE_RATE = 48000
BIT_DEPTH_SUBTYPE = "PCM_24"

SWEEP_DURATION_S = 10.0
SWEEP_F0_HZ = 20.0
SWEEP_F1_HZ = 20000.0
SWEEP_SILENCE_TAIL_S = 3.0   # appended after the sweep so a long real decay isn't truncated
SWEEP_LEVEL_DBFS = -6.0

OUT_DIR = os.path.join(os.path.dirname(__file__), "probes")


def _fade(signal: np.ndarray, sr: int, fade_s: float = 0.01) -> np.ndarray:
    n = int(fade_s * sr)
    if n * 2 >= len(signal):
        return signal
    window = np.ones_like(signal)
    ramp = np.linspace(0.0, 1.0, n)
    window[:n] = ramp
    window[-n:] = ramp[::-1]
    return signal * window


def _normalize_to(signal: np.ndarray, peak_dbfs: float) -> np.ndarray:
    peak = np.max(np.abs(signal))
    if peak == 0:
        return signal
    target = 10.0 ** (peak_dbfs / 20.0)
    return signal * (target / peak)


def _write(name: str, signal: np.ndarray) -> None:
    os.makedirs(OUT_DIR, exist_ok=True)
    path = os.path.join(OUT_DIR, name)
    sf.write(path, signal.astype(np.float32), SAMPLE_RATE, subtype=BIT_DEPTH_SUBTYPE)
    print(f"  wrote {path} ({len(signal) / SAMPLE_RATE:.2f}s)")


def make_dirac_impulse(sr: int, silence_tail_s: float = 3.0) -> np.ndarray:
    n = int(silence_tail_s * sr)
    signal = np.zeros(n)
    signal[0] = 1.0
    return signal


def make_ess_pair(
    duration: float, sr: int, f0: float, f1: float
) -> tuple[np.ndarray, np.ndarray]:
    """Returns (sweep, inverse_filter) per Farina's exponential sine sweep method."""
    t = np.arange(int(duration * sr)) / sr
    k = (f1 / f0) ** (1.0 / duration)
    phase = 2 * np.pi * f0 * (k ** t - 1.0) / np.log(k)
    sweep = np.sin(phase)

    # Inverse filter: time-reversed sweep with an amplitude envelope that compensates for the
    # sweep's own falling energy density (-6dB/octave in a log sweep), so deconvolution recovers
    # a flat, unbiased spectrum instead of one tilted by the sweep's own shape.
    envelope = k ** (-t / duration)
    inverse = sweep[::-1] * envelope[::-1]
    # Normalize so convolving sweep with inverse gives a unit-height delta.
    inverse = inverse / np.max(np.abs(np.convolve(sweep, inverse)))

    return sweep, inverse


def generate_all() -> None:
    print(f"Generating reverb IR probes into {OUT_DIR} ({SAMPLE_RATE}Hz, {BIT_DEPTH_SUBTYPE})")

    impulse = make_dirac_impulse(SAMPLE_RATE, SWEEP_SILENCE_TAIL_S)
    _write("dirac_impulse.wav", impulse)

    sweep, inverse = make_ess_pair(SWEEP_DURATION_S, SAMPLE_RATE, SWEEP_F0_HZ, SWEEP_F1_HZ)
    sweep = _fade(sweep, SAMPLE_RATE)
    sweep = _normalize_to(sweep, SWEEP_LEVEL_DBFS)
    tail = np.zeros(int(SWEEP_SILENCE_TAIL_S * SAMPLE_RATE))
    sweep_with_tail = np.concatenate([sweep, tail])
    _write("ess_sweep.wav", sweep_with_tail)
    _write("ess_inverse_filter.wav", inverse)

    print("Done. Preferred method: play ess_sweep.wav through the reverb, record the wet output,")
    print("then convolve it with ess_inverse_filter.wav to recover the IR (see module docstring).")
    print("dirac_impulse.wav is the simpler fallback for units/plugins where a true impulse won't")
    print("clip or distort.")


if __name__ == "__main__":
    generate_all()
