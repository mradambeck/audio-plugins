"""Generates the dry probe signals to play through a real distortion/saturation hardware unit (or
plugin) and re-record, for later ML fitting.

Unlike signal_generation/modulation (time-varying, needs multi-LFO-cycle sustain) and effects/ambience
(LTI, capturable with one IR per setting), a distortion stage is - to first order - a memoryless,
LEVEL-dependent nonlinearity. The thing actually being fit is the transfer curve (and any
frequency-dependent asymmetry/pre-emphasis around it), so the probe set is built around a fine
input-level staircase rather than long sustains or multi-cycle timing:

  - tone_440Hz_<level>dB.wav     - a single tone frequency across a level staircase - the core
                                   transfer-curve probe. A memoryless nonlinearity's curve doesn't
                                   depend on frequency, so one frequency is enough to trace it; read
                                   adjacent levels against each other, and each one's own harmonic
                                   content at a single level.
  - twotone_<level>dB.wav        - close-spaced two-tone (440+480Hz) at 2 levels - the standard
                                   intermodulation-distortion probe (2f1-f2 etc.), more diagnostic
                                   of cross-term behavior than a wide multitone. Only need a couple
                                   levels to see whether IMD is present and how it scales.
  - sweep_<level>dB.wav          - one log sine sweep, at the loudest level - a coarse check for
                                   frequency-dependent nonlinearity (tone stacks, pre/de-emphasis)
                                   that the single-frequency tone probe can't separate from a flat
                                   gain change. Add more sweep levels later if the curve alone
                                   doesn't explain what you hear.
  - asym_halfwave_<level>dB.wav  - a DC-offset tone (sine riding on a positive offset, clipped back
                                   into range) at one level - reveals asymmetric (e.g. tube-style
                                   even-harmonic) clipping that a symmetric sine can hide, since a
                                   symmetric input can't distinguish a symmetric curve from an
                                   asymmetric one that happens to average out over a full cycle.

This is the minimal set (~10 files) - enough to fit a basic transfer curve and flag the most common
surprises (IMD, frequency dependence, asymmetry) without a full cross product of every probe type
against every level. Noise-burst probing was dropped entirely here since the tone staircase already
covers curve-tracing and twotone already covers cross-term behavior; add a noise probe back in later
only if something in a capture doesn't square with what the tone/twotone data explains.

Run once: play every file through the hardware at each drive/gain knob setting you want to
capture, and name the wet recording to match dry probe + knob settings, e.g.
  tone_440Hz_-12dB__Drive60.wav
mirroring signal_generation/modulation's own paired dry/wet naming convention.
"""
from __future__ import annotations

import os

import numpy as np
import soundfile as sf

SAMPLE_RATE = 48000
BIT_DEPTH_SUBTYPE = "PCM_24"

DURATION_TONE = 2.0        # short - no LFO cycles to span, just enough for a stable harmonic read
DURATION_SWEEP = 6.0

# Level staircase for the tone curve-tracing probe - the core thing distortion fitting needs that
# modulation/ambience don't. Coarser than a full fine sweep, but still enough points to trace a
# curve's shape (more low-end density since that's where a knee is most likely to sit).
LEVELS_DBFS = [-36, -24, -16, -10, -5, -1]

TONE_FREQ_HZ = 440.0
TWOTONE_LEVELS_DBFS = [-16, -3]     # just enough to see IMD presence + how it scales with drive
SWEEP_LEVEL_DBFS = -3.0             # single, loudest-case check
ASYM_LEVEL_DBFS = -6.0              # single mid-level check
TWOTONE_FREQS_HZ = (440.0, 480.0)   # close-spaced pair for intermodulation products
ASYM_OFFSET_FRACTION = 0.35         # DC offset as a fraction of full scale, before final level norm

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
    print(f"  wrote {path} ({len(signal) / SAMPLE_RATE:.1f}s, peak {20*np.log10(np.max(np.abs(signal))+1e-12):.1f}dBFS)")


def make_tone(freq: float, duration: float, sr: int) -> np.ndarray:
    t = np.arange(int(duration * sr)) / sr
    return np.sin(2 * np.pi * freq * t)


def make_twotone(freqs_hz: tuple[float, float], duration: float, sr: int) -> np.ndarray:
    t = np.arange(int(duration * sr)) / sr
    f1, f2 = freqs_hz
    return 0.5 * (np.sin(2 * np.pi * f1 * t) + np.sin(2 * np.pi * f2 * t))


def make_sine_sweep(duration: float, sr: int, f0: float = 20.0, f1: float = 20000.0) -> np.ndarray:
    t = np.arange(int(duration * sr)) / sr
    k = (f1 / f0) ** (1.0 / duration)
    phase = 2 * np.pi * f0 * (k ** t - 1.0) / np.log(k)
    return np.sin(phase)


def make_asym_halfwave(freq: float, duration: float, sr: int, offset_fraction: float) -> np.ndarray:
    """Sine riding on a positive DC offset, then hard-clipped back to [-1, 1] before the final
    level normalize - biases the waveform to spend more time near one rail, so an asymmetric
    transfer curve produces a measurably different result than on a symmetric probe.
    """
    t = np.arange(int(duration * sr)) / sr
    signal = np.sin(2 * np.pi * freq * t) + offset_fraction
    return np.clip(signal, -1.0, 1.0)


def generate_all() -> None:
    print(f"Generating minimal distortion probe set into {OUT_DIR} ({SAMPLE_RATE}Hz, {BIT_DEPTH_SUBTYPE})")
    print(f"Tone level staircase: {LEVELS_DBFS} dBFS")

    tone_base = _fade(make_tone(TONE_FREQ_HZ, DURATION_TONE, SAMPLE_RATE), SAMPLE_RATE)
    for level in LEVELS_DBFS:
        signal = _normalize_to(tone_base, level)
        _write(f"tone_{int(TONE_FREQ_HZ)}Hz_{level}dB.wav", signal)

    twotone_base = _fade(make_twotone(TWOTONE_FREQS_HZ, DURATION_TONE, SAMPLE_RATE), SAMPLE_RATE)
    for level in TWOTONE_LEVELS_DBFS:
        signal = _normalize_to(twotone_base, level)
        _write(f"twotone_{level}dB.wav", signal)

    sweep_base = _fade(make_sine_sweep(DURATION_SWEEP, SAMPLE_RATE), SAMPLE_RATE)
    signal = _normalize_to(sweep_base, SWEEP_LEVEL_DBFS)
    _write(f"sweep_{SWEEP_LEVEL_DBFS}dB.wav", signal)

    asym_base = _fade(
        make_asym_halfwave(220.0, DURATION_TONE, SAMPLE_RATE, ASYM_OFFSET_FRACTION), SAMPLE_RATE
    )
    signal = _normalize_to(asym_base, ASYM_LEVEL_DBFS)
    _write(f"asym_halfwave_220Hz_{ASYM_LEVEL_DBFS}dB.wav", signal)

    print("Done. Play each file through the hardware at every Drive/Gain setting you want to")
    print("capture, and name the wet recording to match the dry probe + knob settings, e.g.")
    print("  tone_440Hz_-5dB__Drive60.wav")


if __name__ == "__main__":
    generate_all()
