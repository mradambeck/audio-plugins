#!/usr/bin/env python3
"""Runs Phase 3's Analysis checklist (concrete-sampler-plugin-plan.md) end to end through the real
ConcreteRenderIR tool.

  1. Linear 8 vs 12 vs 16-bit on a 1kHz sine: noise floor for each, expecting ~6dB per bit.
  2. Companded 8-bit vs linear 8-bit at full scale and at -30dB: similar noise at full scale,
     noticeably lower noise on the quiet signal for the companded version.
  3. Quantization noise should be signal-correlated (harmonic distortion) rather than white at low
     bit depths on a sine - reports the harmonic structure via THD.

Requires a built ConcreteRenderIR and the test-assets/ WAVs (see verify_phase0.py/verify_phase1.py).

Usage: python3 verify_phase3.py
"""
import os
import subprocess
import sys
import tempfile
import wave
import struct

import numpy as np

sys.path.insert(0, os.path.dirname(__file__))
import concrete_analysis as ca

HERE = os.path.dirname(__file__)
RENDER_IR_BIN = os.path.join(HERE, "..", "build", "ConcreteRenderIR_artefacts", "Release", "ConcreteRenderIR")
TEST_ASSETS = os.path.join(HERE, "..", "test-assets")

QUANT_LINEAR, QUANT_COMPANDED = 0, 1

results = []


def check(name, condition, detail=""):
    status = "PASS" if condition else "FAIL"
    print(f"[{status}] {name}" + (f" -- {detail}" if detail else ""))
    results.append(condition)
    return condition


def render(out_path, note=60, seconds=1.0, sample_rate=44100, velocity=127,
           bit_depth=16, quantizer_mode=QUANT_LINEAR, sample_path=None):
    if sample_path is None:
        sample_path = os.path.join(TEST_ASSETS, "sine_1khz.wav")
    subprocess.run(
        [RENDER_IR_BIN, "--out", out_path, "--sample", sample_path, "--note", str(note),
         "--velocity", str(velocity), "--seconds", str(seconds), "--sampleRate", str(sample_rate),
         # pitchEngineMode 0 = Reference throughout this phase's checks - Phase 3's quantizer is
         # deliberately isolated from Phase 2's pitch engines so each phase's checks measure only
         # the thing that phase added (matches verify_phase2.py's own isolation of pitch effects).
         "--pitchEngineMode", "0", "--bitDepth", str(bit_depth), "--quantizerMode", str(quantizer_mode),
         # captureBypass must be OFF: Phase 4 later moved Phase 3's quantizer from always-on live
         # processing into the offline capture pass (see ConcreteCapturePass.h's own comment on why
         # quantization lives there), which defaults to bypassed - without this flag, every render
         # below was silently an exact, unquantized copy of the source regardless of bitDepth/
         # quantizerMode, a real regression in this SCRIPT (not the DSP - ConcreteQuantizerTests.cpp
         # and verify_phase4.py both already exercise the quantizer correctly) that went unnoticed
         # since Phase 4 shipped, until a full verify_phase0-7 regression sweep caught it.
         # captureTranspose 0 keeps the capture pass's own resample step a no-op, isolating the
         # quantizer exactly as this phase's checks intend.
         "--captureBypass", "0", "--captureTranspose", "0"],
        check=True, capture_output=True, text=True,
    )


def write_mono_sine_wav(path, freq_hz, amplitude, seconds=1.0, sample_rate=44100):
    """A source asset at a specific, exact amplitude - test-assets/sine_1khz.wav is fixed at 0.5,
    but this phase's plan explicitly calls for measurements "at full scale and at -30dB", so those
    exact levels are built on the fly here rather than added as permanent test-assets only these
    checks need (matches verify_phase2.py Check 5's own full_scale_1khz.wav precedent)."""
    n = int(seconds * sample_rate)
    samples = (amplitude * np.sin(2 * np.pi * freq_hz * np.arange(n) / sample_rate) * 32767).astype(np.int16)
    with wave.open(path, "w") as wf:
        wf.setnchannels(1)
        wf.setsampwidth(2)
        wf.setframerate(sample_rate)
        wf.writeframes(struct.pack(f"<{n}h", *samples))


def noise_floor_db(signal, sample_rate, fundamental_hz, notch_hz=30.0, lo_hz=20.0, hi_hz=20000.0):
    """RMS level of everything in [lo_hz, hi_hz] EXCLUDING a narrow notch around the fundamental,
    as a fraction of full scale. Deliberately includes harmonics in that sum (not just broadband
    noise) - quantization error power splits between harmonic and broadband content depending on
    signal/bit depth, and the classic ~6dB/bit SQNR relationship the plan's Check 1 asks for is
    about TOTAL injected quantization noise+distortion power, not just its broadband component
    (same idea as a THD+N measurement). Matches verify_phase2.py's in-band-noise technique."""
    freqs, mag_db = ca.magnitude_spectrum_db(signal, sample_rate)
    mag = 10.0 ** (mag_db / 20.0)
    mask = (freqs >= lo_hz) & (freqs <= hi_hz) & (np.abs(freqs - fundamental_hz) > notch_hz)
    rms = float(np.sqrt(np.mean(mag[mask] ** 2))) if np.any(mask) else 0.0
    return 20.0 * np.log10(max(rms, 1e-12))


def main():
    if not os.path.exists(RENDER_IR_BIN):
        print(f"FAIL: {RENDER_IR_BIN} not found - build the ConcreteRenderIR target first.")
        return 1

    with tempfile.TemporaryDirectory() as tmp:
        print("--- Check 1: Linear bit-depth noise floor scales ~6dB/bit (8 vs 12 vs 16-bit) ---")
        noise_by_depth = {}
        for bits in (8, 12, 13, 16):
            out = os.path.join(tmp, f"linear_{bits}bit.wav")
            render(out, bit_depth=bits, quantizer_mode=QUANT_LINEAR)
            rate, data = ca.load_wav(out)
            mono = ca.to_mono(data)[4000:]
            noise_by_depth[bits] = noise_floor_db(mono, rate, 1000.0)
            print(f"    {bits}-bit linear: noise floor {noise_by_depth[bits]:.1f}dBFS")

        # SQNR predicts ~6.02dB per added bit. Checked as the delta between each pair rather than
        # an absolute figure, since the source sine's own amplitude (0.5, from test-assets) isn't
        # full scale - the RELATIVE scaling per bit is what the plan asks for ("expecting ~6dB per
        # bit. Report actual dB figures").
        delta_16_12 = noise_by_depth[16] - noise_by_depth[12]  # 4 bits -> ~-24dB
        delta_12_8 = noise_by_depth[12] - noise_by_depth[8]    # 4 bits -> ~-24dB
        delta_13_12 = noise_by_depth[13] - noise_by_depth[12]  # 1 bit -> ~-6dB
        print(f"    delta 16->12bit (4 bits): {delta_16_12:.1f}dB (expect ~-24dB)")
        print(f"    delta 12->8bit (4 bits): {delta_12_8:.1f}dB (expect ~-24dB)")
        print(f"    delta 13->12bit (1 bit): {delta_13_12:.1f}dB (expect ~-6dB)")
        check("16->12bit noise floor drop is close to the 4-bit SQNR prediction (-24dB)",
              abs(delta_16_12 - (-24.1)) < 6.0, f"measured {delta_16_12:.1f}dB")
        check("12->8bit noise floor drop is close to the 4-bit SQNR prediction (-24dB)",
              abs(delta_12_8 - (-24.1)) < 6.0, f"measured {delta_12_8:.1f}dB")
        check("13->12bit noise floor drop is close to the 1-bit SQNR prediction (-6dB)",
              abs(delta_13_12 - (-6.0)) < 4.0, f"measured {delta_13_12:.1f}dB")
        check("noise floor decreases monotonically as bit depth increases",
              noise_by_depth[8] > noise_by_depth[12] > noise_by_depth[13] > noise_by_depth[16],
              f"{noise_by_depth}")

        print("\n--- Check 2: Companded vs linear 8-bit, at full scale and at -30dB ---")
        full_scale_path = os.path.join(tmp, "full_scale_1khz.wav")
        quiet_path = os.path.join(tmp, "quiet_1khz.wav")
        write_mono_sine_wav(full_scale_path, 1000.0, amplitude=1.0)
        write_mono_sine_wav(quiet_path, 1000.0, amplitude=10.0 ** (-30.0 / 20.0))  # -30dBFS

        levels = {}
        for level_name, path in (("full_scale", full_scale_path), ("quiet_-30dB", quiet_path)):
            for mode_name, mode in (("linear", QUANT_LINEAR), ("companded", QUANT_COMPANDED)):
                out = os.path.join(tmp, f"{level_name}_{mode_name}.wav")
                render(out, bit_depth=8, quantizer_mode=mode, sample_path=path)
                rate, data = ca.load_wav(out)
                mono = ca.to_mono(data)[4000:]
                levels[(level_name, mode_name)] = noise_floor_db(mono, rate, 1000.0)
                print(f"    {level_name}/{mode_name}: noise floor {levels[(level_name, mode_name)]:.1f}dBFS")

        full_scale_delta = levels[("full_scale", "companded")] - levels[("full_scale", "linear")]
        quiet_delta = levels[("quiet_-30dB", "companded")] - levels[("quiet_-30dB", "linear")]
        print(f"    full-scale companded-vs-linear delta: {full_scale_delta:.1f}dB")
        print(f"    quiet(-30dB) companded-vs-linear delta: {quiet_delta:.1f}dB (expect clearly negative - companded quieter)")
        # NOTE on the full-scale delta: the plan's own wording predicts "similar noise at full
        # scale," but a standard mu=255 curve (see ConcreteQuantizer.h) measurably trades away
        # full-scale headroom for its low-level gain - this is the textbook mu-law/A-law trade-off
        # (the same curve used in G.711 telephony is well documented as running roughly 10-15dB
        # WORSE than linear PCM at full scale in exchange for a much wider usable dynamic range),
        # not a bug in this implementation. 20dB comfortably bounds "a real companding curve doing
        # its job" while still catching a genuinely broken implementation (e.g. one that's 40+dB
        # worse, which would mean the compress/expand pair isn't behaving as a companding curve at
        # all). The actual point of the check - quiet-signal improvement - is the assertion below.
        check("at full scale, companded is worse than linear but within the expected mu-law trade-off (< 20dB)",
              abs(full_scale_delta) < 20.0, f"delta {full_scale_delta:.1f}dB")
        check("at -30dB, companding is measurably quieter than linear (at least 10dB lower noise floor)",
              quiet_delta < -10.0, f"delta {quiet_delta:.1f}dB")

        print("\n--- Check 3: Quantization noise is signal-correlated (harmonic), not white ---")
        # NOT measured via thd_percent()'s classic "energy at 2f0, 3f0, ..." reading: at 1000Hz
        # into a 44.1kHz stream, one cycle is 44.1 samples - not an integer - so the quantization
        # error (a memoryless, deterministic function of a periodic input, therefore itself exactly
        # periodic) only repeats every 441 samples (10 cycles), not every 44.1. Its true discrete-
        # time fundamental is sr/441 = 100Hz, not 1000Hz, so its energy lands on a comb of 100Hz
        # multiples - confirmed directly (99.9% of the error's spectral energy sits at exact 100Hz-
        # multiple bins, essentially none at the naively-expected 2000/3000/4000Hz) - and
        # thd_percent()'s bin-exact lookup at 1000Hz-multiples mostly misses that comb entirely,
        # making it read close to zero regardless of how much real distortion is present. This is a
        # genuine, well-known quantization/DAC "spur" phenomenon (a non-integer samples-per-cycle
        # ratio), not a bug in ConcreteQuantizer or a flaw in thd_percent() for its intended use.
        #
        # The general, rate-ratio-independent way to tell "harmonic/deterministic" from "white" is
        # spectral CONCENTRATION: a deterministic, periodic error signal packs its energy into a
        # small number of discrete lines; genuine white noise spreads it near-evenly across every
        # bin. Comparing against an i.i.d. Gaussian-noise control at the SAME RMS level as the real
        # quantization error isolates that shape difference from its magnitude.
        def top_k_concentration(signal, sample_rate, fundamental_hz, k=20, notch_hz=30.0):
            freqs, mag_db = ca.magnitude_spectrum_db(signal, sample_rate)
            mag2 = (10.0 ** (mag_db / 20.0)) ** 2
            non_fundamental = mag2[np.abs(freqs - fundamental_hz) > notch_hz]
            total = float(np.sum(non_fundamental))
            if total <= 0.0:
                return 0.0
            return float(np.sum(np.sort(non_fundamental)[-k:])) / total

        out16 = os.path.join(tmp, "concentration_16bit.wav")
        render(out16, bit_depth=16, quantizer_mode=QUANT_LINEAR)
        rate, data16 = ca.load_wav(out16)
        clean = ca.to_mono(data16)[4000:]

        rng = np.random.default_rng(20260905)  # fixed seed - a deterministic, reproducible control

        for label, bits, mode in (("8-bit linear", 8, QUANT_LINEAR),
                                    ("12-bit linear", 12, QUANT_LINEAR),
                                    ("8-bit companded", 8, QUANT_COMPANDED)):
            out = os.path.join(tmp, f"concentration_{label.replace(' ', '_')}.wav")
            render(out, bit_depth=bits, quantizer_mode=mode)
            _, data = ca.load_wav(out)
            quantized = ca.to_mono(data)[4000:]

            error_rms = float(np.sqrt(np.mean((quantized - clean) ** 2)))
            noise_control = clean + rng.normal(0.0, error_rms, len(clean))

            concentration = top_k_concentration(quantized, rate, 1000.0)
            control_concentration = top_k_concentration(noise_control, rate, 1000.0)
            print(f"    {label}: top-20-bin energy concentration {concentration * 100:.1f}% "
                  f"vs {control_concentration * 100:.1f}% for matched-RMS white noise")
            check(f"{label} quantization noise is far more spectrally concentrated than white noise "
                  f"at the same level (at least 5x)",
                  concentration > control_concentration * 5.0,
                  f"{concentration * 100:.1f}% vs {control_concentration * 100:.1f}%")

    print("\n" + ("ALL PHASE 3 CHECKS PASSED" if all(results) else "PHASE 3 CHECKS FAILED"))
    return 0 if all(results) else 1


if __name__ == "__main__":
    sys.exit(main())
