#!/usr/bin/env python3
"""Generates the synthetic impulse responses bundled with the ConvBase dev harness.

These are NOT a product IR set. They exist so the shared convolution engine in
plugins/common/convolution/ has something to be tested and auditioned against in CI and in a DAW,
without committing captured audio to a public repo. Every one of them is chosen to exercise a
specific code path:

  dirac-48k        a single unit impulse at sample 0, then silence. Convolving with it is an
                   identity operation, which is what makes "reported latency is zero" and "the
                   impulse comes out at sample 0" measurable rather than assertable. Its 0.25 s
                   length is not arbitrary: afconvert writes FLAC in 4608-frame packets and
                   silently drops a file shorter than one, producing a valid-looking 42-byte
                   header with no audio in it.
  room-stereo-48k  stereo, already at the usual session rate - the no-resampling path.
  hall-stereo-44k  stereo at 44.1 kHz - forces IRLibrary to resample in a 48 kHz session.
  plate-mono-96k   mono at 96 kHz - forces the opposite ratio, and the mono-IR branch of
                   ConvolutionEngine::loadIR().

Deterministic: the RNG seed is fixed, so re-running this reproduces the committed files byte for
byte and a diff means someone changed the generator, not the weather.

Usage: python3 tools/make_test_irs.py      (writes ../irs/*.flac, needs numpy + afconvert)
"""

import pathlib
import subprocess
import tempfile
import wave

import numpy as np

SEED = 20260915
OUT_DIR = pathlib.Path(__file__).resolve().parent.parent / "irs"


def decaying_noise(rng, sample_rate, seconds, decay_seconds, channels, lowpass_hz):
    """A crude but IR-shaped signal: band-limited noise under an exponential decay."""
    n = int(sample_rate * seconds)
    noise = rng.standard_normal((channels, n))

    # One-pole lowpass. Rolling the top off makes this behave more like a real room tail and,
    # incidentally, makes it compress far better than white noise would.
    alpha = np.exp(-2.0 * np.pi * lowpass_hz / sample_rate)
    filtered = np.empty_like(noise)
    state = np.zeros(channels)
    for i in range(n):
        state = (1.0 - alpha) * noise[:, i] + alpha * state
        filtered[:, i] = state

    envelope = np.exp(-np.arange(n) / (decay_seconds * sample_rate))
    shaped = filtered * envelope

    # A short fade-in stands in for the direct-sound gap a real capture has, and keeps the very
    # first sample from being a step.
    attack = int(0.002 * sample_rate)
    shaped[:, :attack] *= np.linspace(0.0, 1.0, attack)

    peak = np.max(np.abs(shaped))
    return shaped / peak * 0.9 if peak > 0 else shaped


def dirac(sample_rate, seconds):
    n = int(sample_rate * seconds)
    out = np.zeros((1, n))
    out[0, 0] = 1.0
    return out


def write_flac(samples, sample_rate, destination):
    """16-bit WAV via the stdlib, then afconvert to FLAC - no third-party audio deps needed."""
    channels, n = samples.shape
    quantised = np.clip(samples, -1.0, 1.0)
    interleaved = (quantised.T.reshape(-1) * 32767.0).astype("<i2")

    with tempfile.TemporaryDirectory() as tmp:
        wav_path = pathlib.Path(tmp) / "ir.wav"
        with wave.open(str(wav_path), "wb") as wav:
            wav.setnchannels(channels)
            wav.setsampwidth(2)
            wav.setframerate(sample_rate)
            wav.writeframes(interleaved.tobytes())

        destination.parent.mkdir(parents=True, exist_ok=True)
        subprocess.run(
            ["afconvert", "-f", "flac", "-d", "flac", str(wav_path), str(destination)],
            check=True,
        )

    print(f"{destination.name}: {channels}ch {sample_rate} Hz {n} samples "
          f"-> {destination.stat().st_size / 1024:.0f} KB")


def main():
    rng = np.random.default_rng(SEED)

    write_flac(dirac(48000, 0.25), 48000, OUT_DIR / "dirac-48k.flac")
    write_flac(decaying_noise(rng, 48000, 0.6, 0.12, 2, 3500.0), 48000, OUT_DIR / "room-stereo-48k.flac")
    write_flac(decaying_noise(rng, 44100, 1.2, 0.30, 2, 2500.0), 44100, OUT_DIR / "hall-stereo-44k.flac")
    write_flac(decaying_noise(rng, 96000, 0.8, 0.18, 1, 5000.0), 96000, OUT_DIR / "plate-mono-96k.flac")


if __name__ == "__main__":
    main()
