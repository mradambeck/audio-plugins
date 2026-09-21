"""Generates the dry probe signals to play through a real modulation hardware unit (chorus/
flanger/phaser/etc.) and re-record, for later ML fitting.

Unlike effects/ambience (an LTI reverb, capturable with a single impulse response per knob
setting), a modulation effect is time-varying - an IR only samples one instant of its LFO cycle.
These probes are chosen to expose the LFO rate/depth/shape and any level-dependent nonlinearity
instead:

  - sweep_<level>dB.wav      - log sine sweep (20Hz-20kHz), several levels
  - tone_<freq>Hz.wav        - long sustained tones spanning several LFO cycles, several pitches
  - noise_white.wav / noise_pink.wav - long broadband bursts
  - multitone.wav            - stacked sine tones (cross-modulation probe)
  - clicktrain_<rate>Hz.wav  - impulse trains at a few rates (comb-filtering probe)

Run once per hardware knob (Rate/Depth, etc.) setting you want to capture: play every file in
this directory through the hardware at that setting, record the wet output, and keep the dry
probe and wet capture paired by filename (mirrors effects/ambience's paired dry/wet convention
in core/io.py's Capture.kind == "paired").
"""
from __future__ import annotations

import os

import numpy as np
import soundfile as sf

SAMPLE_RATE = 48000
DURATION_LONG = 12.0   # seconds; several LFO cycles even at a slow ~0.1Hz rate
DURATION_SWEEP = 10.0
BIT_DEPTH_SUBTYPE = "PCM_24"
HEADROOM_DBFS = -1.0   # peak headroom so nothing clips through the hardware's own input stage

OUT_DIR = os.path.join(os.path.dirname(__file__), "probes")

TONE_FREQS_HZ = [110, 220, 440, 880, 1760]
SWEEP_LEVELS_DBFS = [-6, -18, -30]
CLICKTRAIN_RATES_HZ = [1, 4, 10]


def _fade(signal: np.ndarray, sr: int, fade_s: float = 0.02) -> np.ndarray:
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
    print(f"  wrote {path} ({len(signal) / SAMPLE_RATE:.1f}s)")


def make_sine_sweep(duration: float, sr: int, f0: float = 20.0, f1: float = 20000.0) -> np.ndarray:
    """Exponential (log) sweep, standard chirp for LTI-ish probing of nonlinear stages."""
    t = np.arange(int(duration * sr)) / sr
    k = (f1 / f0) ** (1.0 / duration)
    phase = 2 * np.pi * f0 * (k ** t - 1.0) / np.log(k)
    return np.sin(phase)


def make_tone(freq: float, duration: float, sr: int) -> np.ndarray:
    t = np.arange(int(duration * sr)) / sr
    return np.sin(2 * np.pi * freq * t)


def make_multitone(freqs_hz: list[float], duration: float, sr: int) -> np.ndarray:
    t = np.arange(int(duration * sr)) / sr
    signal = sum(np.sin(2 * np.pi * f * t) for f in freqs_hz)
    return signal / len(freqs_hz)


def make_white_noise(duration: float, sr: int, seed: int = 0) -> np.ndarray:
    rng = np.random.default_rng(seed)
    return rng.standard_normal(int(duration * sr))


def make_pink_noise(duration: float, sr: int, seed: int = 0) -> np.ndarray:
    """Voss-McCartney approximation - good enough as a broadband probe, not a calibrated pink ref."""
    rng = np.random.default_rng(seed)
    n = int(duration * sr)
    n_rows = 16
    array = rng.standard_normal((n_rows, n))
    for row in range(1, n_rows):
        stride = 2 ** row
        array[row, ::stride] = array[row, ::stride]
        array[row] = np.repeat(array[row, ::stride], stride)[:n]
    pink = array.sum(axis=0)
    return pink


def make_click_train(rate_hz: float, duration: float, sr: int) -> np.ndarray:
    signal = np.zeros(int(duration * sr))
    step = int(sr / rate_hz)
    signal[::step] = 1.0
    return signal


def generate_all() -> None:
    print(f"Generating modulation probe set into {OUT_DIR} ({SAMPLE_RATE}Hz, {BIT_DEPTH_SUBTYPE})")

    for level in SWEEP_LEVELS_DBFS:
        sweep = make_sine_sweep(DURATION_SWEEP, SAMPLE_RATE)
        sweep = _fade(sweep, SAMPLE_RATE)
        sweep = _normalize_to(sweep, level)
        _write(f"sweep_{level}dB.wav", sweep)

    for freq in TONE_FREQS_HZ:
        tone = make_tone(freq, DURATION_LONG, SAMPLE_RATE)
        tone = _fade(tone, SAMPLE_RATE)
        tone = _normalize_to(tone, HEADROOM_DBFS)
        _write(f"tone_{freq}Hz.wav", tone)

    multitone = make_multitone(TONE_FREQS_HZ, DURATION_LONG, SAMPLE_RATE)
    multitone = _fade(multitone, SAMPLE_RATE)
    multitone = _normalize_to(multitone, HEADROOM_DBFS)
    _write("multitone.wav", multitone)

    white = _fade(make_white_noise(DURATION_LONG, SAMPLE_RATE), SAMPLE_RATE)
    white = _normalize_to(white, HEADROOM_DBFS)
    _write("noise_white.wav", white)

    pink = _fade(make_pink_noise(DURATION_LONG, SAMPLE_RATE), SAMPLE_RATE)
    pink = _normalize_to(pink, HEADROOM_DBFS)
    _write("noise_pink.wav", pink)

    for rate in CLICKTRAIN_RATES_HZ:
        clicks = make_click_train(rate, DURATION_LONG, SAMPLE_RATE)
        _write(f"clicktrain_{rate}Hz.wav", clicks)

    print("Done. Play each file through the hardware at every Rate/Depth setting you want to")
    print("capture, and name the wet recording to match the dry probe + knob settings, e.g.")
    print("  tone_440Hz__Rate2.0_Depth60.wav")


if __name__ == "__main__":
    generate_all()
