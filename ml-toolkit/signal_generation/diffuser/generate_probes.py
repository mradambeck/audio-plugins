"""Generates the dry probe signals to play through a real diffuser (e.g. a Turbosynth-style
allpass-network diffuser) and re-record, for later ML fitting.

A diffuser is - to first order - LTI, same category as a reverb: signal_generation/reverb_ir/generate_probes.py's
ess_sweep.wav/ess_inverse_filter.wav pair is the primary capture method (better SNR on the tail
than a raw impulse) and its dirac_impulse.wav is directly useful here too, not just a deconvolution
fallback - a diffuser's whole job is smearing a transient into a short, dense burst, so a plain
impulse-in/impulse-out comparison is the most legible way to see that smearing/density behavior
directly, the way you'd audition it by ear in Turbosynth itself.

What's added here, on top of the shared reverb IR pair, is what a pure IR capture doesn't cover:

  - click_<level>dB.wav   - short, plain transient clicks (not the deconvolution-tuned dirac) at a
                            few levels, to check whether the diffuser is genuinely level-independent
                            (a true allpass network is) or has any level-dependent character (soft
                            clipping in an analog stage, a limiter, etc.) - the same reasoning as
                            signal_generation/distortion's level staircase, just narrower since a diffuser is
                            expected to be much closer to linear than a distortion stage.
  - noiseburst_<level>dB.wav - short (200ms) noise bursts at the same levels - a second, broadband
                            transient shape to cross-check the click-derived density/level findings
                            against, since a single transient shape could hide a spectrally-uneven
                            level dependency that broadband noise would expose.

Run once: play every file (plus signal_generation/reverb_ir/generate_probes.py's ess_sweep.wav/dirac_impulse.wav) through the
diffuser, record the wet output, and name it to pair with the dry probe, e.g.
  click_-6dB__Diffusion80.wav
mirroring signal_generation/distortion's and signal_generation/modulation's own paired dry/wet naming convention.
"""
from __future__ import annotations

import os

import numpy as np
import soundfile as sf

SAMPLE_RATE = 48000
BIT_DEPTH_SUBTYPE = "PCM_24"

CLICK_SILENCE_TAIL_S = 2.0    # long enough to capture a diffuser's full smear/decay
NOISEBURST_DURATION_S = 0.2
NOISEBURST_SILENCE_TAIL_S = 2.0

# Narrower than signal_generation/distortion's staircase - a diffuser is expected to be much closer to
# linear, this is a level-INDEPENDENCE check, not a curve-tracing probe.
LEVELS_DBFS = [-24, -12, -6, -1]

OUT_DIR = os.path.join(os.path.dirname(__file__), "probes")


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


def make_click(sr: int, silence_tail_s: float, peak: float = 1.0) -> np.ndarray:
    n = int(silence_tail_s * sr)
    signal = np.zeros(n)
    signal[0] = peak
    return signal


def make_noise_burst(sr: int, burst_s: float, silence_tail_s: float, seed: int = 0) -> np.ndarray:
    rng = np.random.default_rng(seed)
    burst = rng.standard_normal(int(burst_s * sr))
    n_fade = int(0.005 * sr)
    if n_fade * 2 < len(burst):
        ramp = np.linspace(0.0, 1.0, n_fade)
        burst[:n_fade] *= ramp
        burst[-n_fade:] *= ramp[::-1]
    tail = np.zeros(int(silence_tail_s * sr))
    return np.concatenate([burst, tail])


def generate_all() -> None:
    print(f"Generating diffuser probe set into {OUT_DIR} ({SAMPLE_RATE}Hz, {BIT_DEPTH_SUBTYPE})")
    print("Also use ../reverb_ir/probes/ess_sweep.wav + ess_inverse_filter.wav and dirac_impulse.wav")
    print(f"Level-independence check at: {LEVELS_DBFS} dBFS")

    for level in LEVELS_DBFS:
        click = make_click(SAMPLE_RATE, CLICK_SILENCE_TAIL_S)
        click = _normalize_to(click, level)
        _write(f"click_{level}dB.wav", click)

    for level in LEVELS_DBFS:
        burst = make_noise_burst(SAMPLE_RATE, NOISEBURST_DURATION_S, NOISEBURST_SILENCE_TAIL_S)
        burst = _normalize_to(burst, level)
        _write(f"noiseburst_{level}dB.wav", burst)

    print("Done. Play each file (plus the shared IR pair) through the diffuser, record the wet")
    print("output, and name it to pair with the dry probe + knob settings, e.g.")
    print("  click_-6dB__Diffusion80.wav")


if __name__ == "__main__":
    generate_all()
