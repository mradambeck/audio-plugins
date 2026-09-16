#!/usr/bin/env python3
"""Runs Phase 4's Analysis checklist (concrete-sampler-plugin-plan.md) end to end through the real
ConcreteRenderIR tool.

  1. Capture +5 with compensation, 1kHz sine: output still ~1kHz (and measurably NOT ~1kHz with
     compensation off, confirming the compensation math is actually doing something).
  2. A near-Nyquist tone, capture engaged vs bypassed: bypassed leaves it untouched; engaged makes
     it alias to a specific, precisely-predictable OTHER frequency.
  3. Sweep capture transpose 0 -> +12 semitones: the measured output frequency tracks the
     predicted ALIAS frequency (not a naive clean pitch-up) with increasing severity/deviation.
  4. Input drive nominal vs +12dB: THD increases; and separately, high drive measurably rolls off
     high-frequency content on broadband material (the MPC60-style rolloff).
  5. Non-destructiveness: with captureBypass on, the output must null against a render that never
     touched any capture-pass parameter at all, regardless of what bitDepth/transpose/drive are
     simultaneously set to - bypass must be a TRUE bypass, not something that leaks through.

NOTE on Checks 2/3's methodology: the plan's own wording ("alias energy above 8kHz... expect
monotonic increase") assumes a simple energy-above-threshold metric tracks aliasing severity. It
doesn't, empirically - for THIS resample construction (decimate a buffer via interpolation, then
play it back at the original declared rate to realize the pitch-up), aliasing FOLDS content
within the existing 0-Nyquist range rather than adding NEW energy specifically above a fixed
threshold (confirmed with white noise, a multi-tone signal, and broadband transient material -
"energy above 8kHz" stayed flat or even fell as transpose increased, regardless of interpolation
quality tried). The precise, closed-form alternative used here - a single near-Nyquist test tone,
whose aliased landing frequency is exactly predictable as `hostRate - ratio * f0` once
`ratio * f0` exceeds the reduced Nyquist - verifies the same underlying phenomenon (the resample
step has no anti-aliasing filter, matching the SP-1200's own deliberately-filterless precedent)
far more rigorously than a broadband energy sum could.

Thread safety (the plan's 6th Analysis bullet - "hammer re-bakes... under a thread sanitizer
build") is verified separately in C++ (see ConcreteProcessorTests.cpp's stress test, run under
both a normal build and a real -fsanitize=thread build), not here - it isn't a spectral claim
Python/ConcreteRenderIR can exercise.

Requires a built ConcreteRenderIR and the test-assets/ WAVs (see verify_phase0.py/verify_phase1.py).

Usage: python3 verify_phase4.py
"""
import os
import struct
import subprocess
import sys
import tempfile
import wave

import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

sys.path.insert(0, os.path.dirname(__file__))
import concrete_analysis as ca

HERE = os.path.dirname(__file__)
RENDER_IR_BIN = os.path.join(HERE, "..", "build", "ConcreteRenderIR_artefacts", "Release", "ConcreteRenderIR")
TEST_ASSETS = os.path.join(HERE, "..", "test-assets")
PLOTS_DIR = os.path.join(HERE, "validation_plots")

results = []


def check(name, condition, detail=""):
    status = "PASS" if condition else "FAIL"
    print(f"[{status}] {name}" + (f" -- {detail}" if detail else ""))
    results.append(condition)
    return condition


def render(out_path, note=60, seconds=1.0, sample_rate=44100, velocity=127, sample_path=None,
           capture_bypass=True, capture_transpose=5.0, capture_drive=0.0,
           capture_auto_compensate=True, capture_iterations=1, bit_depth=16, quantizer_mode=0):
    if sample_path is None:
        sample_path = os.path.join(TEST_ASSETS, "sine_1khz.wav")
    subprocess.run(
        [RENDER_IR_BIN, "--out", out_path, "--sample", sample_path, "--note", str(note),
         "--velocity", str(velocity), "--seconds", str(seconds), "--sampleRate", str(sample_rate),
         "--pitchEngineMode", "0",  # Reference throughout - isolates the capture pass from Phase 2's pitch engines
         "--captureBypass", "1" if capture_bypass else "0",
         "--captureTranspose", str(capture_transpose),
         "--captureDrive", str(capture_drive),
         "--captureAutoCompensate", "1" if capture_auto_compensate else "0",
         "--captureIterations", str(capture_iterations),
         "--bitDepth", str(bit_depth), "--quantizerMode", str(quantizer_mode)],
        check=True, capture_output=True, text=True,
    )


def write_mono_sine_wav(path, freq_hz, amplitude=0.8, seconds=1.0, sample_rate=44100):
    """A source asset at an exact, known frequency - test-assets/ has nothing this high (its own
    sine assets are 100Hz/1kHz), and Checks 2/3 specifically need a tone close to the file's own
    Nyquist to alias predictably (see the module docstring)."""
    n = int(seconds * sample_rate)
    samples = (amplitude * np.sin(2 * np.pi * freq_hz * np.arange(n) / sample_rate) * 32767).astype(np.int16)
    with wave.open(path, "w") as wf:
        wf.setnchannels(1)
        wf.setsampwidth(2)
        wf.setframerate(sample_rate)
        wf.writeframes(struct.pack(f"<{n}h", *samples))


def main():
    os.makedirs(PLOTS_DIR, exist_ok=True)
    if not os.path.exists(RENDER_IR_BIN):
        print(f"FAIL: {RENDER_IR_BIN} not found - build the ConcreteRenderIR target first.")
        return 1

    with tempfile.TemporaryDirectory() as tmp:
        print("--- Check 1: Capture +5 with compensation keeps a 1kHz sine at ~1kHz ---")
        out_compensated = os.path.join(tmp, "compensated.wav")
        out_uncompensated = os.path.join(tmp, "uncompensated.wav")
        render(out_compensated, capture_bypass=False, capture_transpose=5.0, capture_auto_compensate=True)
        render(out_uncompensated, capture_bypass=False, capture_transpose=5.0, capture_auto_compensate=False)

        rate, dataComp = ca.load_wav(out_compensated)
        peaksComp = ca.find_partials(ca.to_mono(dataComp)[4000:], rate, prominence_db=20.0)
        rate, dataUncomp = ca.load_wav(out_uncompensated)
        peaksUncomp = ca.find_partials(ca.to_mono(dataUncomp)[4000:], rate, prominence_db=20.0)

        print(f"    compensated fundamental: {peaksComp[0][0]:.1f}Hz")
        print(f"    uncompensated fundamental: {peaksUncomp[0][0]:.1f}Hz (predicted ~{1000.0 * 2.0**(5/12):.1f}Hz)")
        check("compensated output stays within 3Hz of 1000Hz", abs(peaksComp[0][0] - 1000.0) < 3.0,
              f"measured {peaksComp[0][0]:.1f}Hz")
        check("uncompensated output is measurably pitched up (>200Hz above 1000Hz)",
              peaksUncomp[0][0] - 1000.0 > 200.0, f"measured {peaksUncomp[0][0]:.1f}Hz")

        print("\n--- Check 2: A near-Nyquist tone, capture engaged vs bypassed ---")
        # 18kHz - close enough to the file's own 22.05kHz Nyquist that a +12 semitone (2x) capture
        # transpose pushes ratio*f0 (36kHz) past the host's Nyquist, guaranteeing a fold - see the
        # module docstring for why a tone (not broadband material) is what makes this precise.
        f0 = 18000.0
        tone_path = os.path.join(tmp, "near_nyquist_tone.wav")
        write_mono_sine_wav(tone_path, f0)

        out_bypassed = os.path.join(tmp, "tone_bypassed.wav")
        out_engaged = os.path.join(tmp, "tone_engaged.wav")
        render(out_bypassed, sample_path=tone_path, capture_bypass=True)
        render(out_engaged, sample_path=tone_path, capture_bypass=False, capture_transpose=12.0,
               capture_auto_compensate=False)  # uncompensated - inspect what's actually stored, unsmoothed by a return trip

        rate, dataBypassed = ca.load_wav(out_bypassed)
        rate, dataEngaged = ca.load_wav(out_engaged)
        peakBypassed = ca.find_partials(ca.to_mono(dataBypassed), rate, prominence_db=10.0)[0][0]
        peakEngaged = ca.find_partials(ca.to_mono(dataEngaged), rate, prominence_db=10.0)[0][0]
        predicted_alias = 44100.0 - 2.0 * f0  # ratio=2 exactly at +12 semitones
        print(f"    bypassed: strongest peak at {peakBypassed:.1f}Hz (source was {f0:.0f}Hz)")
        print(f"    engaged (+12st): strongest peak at {peakEngaged:.1f}Hz (predicted alias {predicted_alias:.0f}Hz)")
        check("bypassed leaves the tone untouched", abs(peakBypassed - f0) < 5.0, f"measured {peakBypassed:.1f}Hz")
        check("engaging the capture pass moves the tone to the precisely-predicted alias frequency, "
              "not a clean pitch-up",
              abs(peakEngaged - predicted_alias) < 5.0, f"measured {peakEngaged:.1f}Hz")

        print("\n--- Check 3: Sweep capture transpose 0 -> +12 semitones - the aliased fold deviates "
              "further from a clean pitch-up as transpose increases ---")
        transposes = [0.0, 3.0, 6.0, 9.0, 12.0]
        measured = []
        deviationFromClean = []
        for transpose in transposes:
            out_t = os.path.join(tmp, f"tone_transpose{transpose:.0f}.wav")
            render(out_t, sample_path=tone_path, capture_bypass=False, capture_transpose=transpose,
                   capture_auto_compensate=False)
            _, d = ca.load_wav(out_t)
            peak = ca.find_partials(ca.to_mono(d), rate, prominence_db=10.0)[0][0]
            ratio = 2.0 ** (transpose / 12.0)
            clean_prediction = f0 * ratio  # what a genuinely alias-free pitch-up would show
            measured.append(peak)
            deviationFromClean.append(abs(peak - clean_prediction))
        print(f"    transposes={transposes}")
        print(f"    measured peak (Hz)={[f'{m:.0f}' for m in measured]}")
        print(f"    deviation from a clean (alias-free) pitch-up (Hz)={[f'{d:.0f}' for d in deviationFromClean]}")
        check("no aliasing yet at transpose=0 (deviation near zero)", deviationFromClean[0] < 5.0,
              f"measured {deviationFromClean[0]:.0f}Hz")
        check("deviation from clean pitch-up grows to hundreds of Hz by +12 semitones - real, "
              "substantial aliasing, not a rounding artifact",
              deviationFromClean[-1] > 500.0, f"measured {deviationFromClean[-1]:.0f}Hz")
        non_monotonic_steps = sum(1 for i in range(1, len(deviationFromClean))
                                   if deviationFromClean[i] < deviationFromClean[i - 1] - 5.0)
        check("deviation from clean pitch-up increases monotonically with transpose",
              non_monotonic_steps == 0, f"{non_monotonic_steps} backward step(s)")

        plt.figure(figsize=(6, 4))
        plt.plot(transposes, deviationFromClean, marker="o")
        plt.xlabel("Capture transpose (semitones)")
        plt.ylabel("Deviation from a clean pitch-up (Hz)")
        plt.title("Capture pass: aliasing severity vs. transpose (near-Nyquist test tone)")
        plt.grid(True, alpha=0.3)
        plt.tight_layout()
        plt.savefig(os.path.join(PLOTS_DIR, "capture_alias_deviation_vs_transpose.png"))
        plt.close()
        print(f"    chart saved to {PLOTS_DIR}/capture_alias_deviation_vs_transpose.png")

        print("\n--- Check 4: Input drive nominal vs +12/+24dB - THD rises, HF content falls ---")
        out_drive0 = os.path.join(tmp, "drive0.wav")
        out_drive12 = os.path.join(tmp, "drive12.wav")
        out_drive24 = os.path.join(tmp, "drive24.wav")
        # transpose=0 isolates drive's own effect from the resample step's - only drive/quantize
        # are under test here.
        render(out_drive0, capture_bypass=False, capture_transpose=0.0, capture_drive=0.0)
        render(out_drive12, capture_bypass=False, capture_transpose=0.0, capture_drive=12.0)
        render(out_drive24, capture_bypass=False, capture_transpose=0.0, capture_drive=24.0)

        _, d0 = ca.load_wav(out_drive0)
        _, d12 = ca.load_wav(out_drive12)
        _, d24 = ca.load_wav(out_drive24)
        thd0 = ca.thd_percent(ca.to_mono(d0)[4000:], rate, 1000.0)
        thd12 = ca.thd_percent(ca.to_mono(d12)[4000:], rate, 1000.0)
        thd24 = ca.thd_percent(ca.to_mono(d24)[4000:], rate, 1000.0)
        print(f"    THD: 0dB={thd0:.2f}%, +12dB={thd12:.2f}%, +24dB={thd24:.2f}% (context only, see note below)")
        check("THD rises measurably from 0dB to +12dB drive (the plan's own nominal-vs-+12dB comparison)",
              thd12 > thd0 * 2.0, f"{thd0:.2f}% -> {thd12:.2f}%")
        # +24dB's THD is NOT asserted to keep rising past +12dB: at that point the rolloff filter's
        # cutoff (see ConcreteCapturePass::applyDrive) has fallen close to the 1kHz test tone's own
        # fundamental, so the filter starts attenuating the fundamental itself along with the
        # harmonics, non-monotonically changing their RATIO (what thd_percent measures) - a real,
        # understood interaction between the two coupled effects on this specific test tone, not a
        # bug. The plan's own claim only compares nominal vs +12dB, which does hold.

        # HF rolloff needs broadband material - a driven sine's own harmonics ARE high-frequency
        # content, which would confound a THD-heavy signal's own high-frequency energy with the
        # rolloff filter's effect.
        transient_path = os.path.join(TEST_ASSETS, "bright_transient.wav")
        out_hf0 = os.path.join(tmp, "hf_drive0.wav")
        out_hf24 = os.path.join(tmp, "hf_drive24.wav")
        render(out_hf0, sample_path=transient_path, capture_bypass=False, capture_transpose=0.0, capture_drive=0.0)
        render(out_hf24, sample_path=transient_path, capture_bypass=False, capture_transpose=0.0, capture_drive=24.0)
        _, dHf0 = ca.load_wav(out_hf0)
        _, dHf24 = ca.load_wav(out_hf24)
        hf0 = ca.energy_above_freq_db(ca.to_mono(dHf0), rate, 5000.0)
        hf24 = ca.energy_above_freq_db(ca.to_mono(dHf24), rate, 5000.0)
        print(f"    broadband energy above 5kHz: 0dB drive={hf0:.1f}dBFS, +24dB drive={hf24:.1f}dBFS")
        check("high drive measurably rolls off high-frequency content (at least 3dB lower above 5kHz)",
              hf24 < hf0 - 3.0, f"{hf0:.1f}dBFS -> {hf24:.1f}dBFS")

        print("\n--- Check 5: Non-destructiveness - bypass is a TRUE bypass regardless of other dial positions ---")
        out_clean = os.path.join(tmp, "clean_never_touched.wav")
        out_bypassed_dirty_dials = os.path.join(tmp, "bypassed_dirty_dials.wav")
        render(out_clean)  # every capture-pass parameter left at its own default
        # Bypassed, but with every OTHER capture-pass parameter deliberately set to something
        # non-default - if bypass leaked through anywhere, this would differ from out_clean.
        render(out_bypassed_dirty_dials, capture_bypass=True, capture_transpose=12.0, capture_drive=24.0,
               capture_iterations=4, bit_depth=8, quantizer_mode=1)

        _, dClean = ca.load_wav(out_clean)
        _, dDirty = ca.load_wav(out_bypassed_dirty_dials)
        residual = ca.residual_db(ca.to_mono(dClean), ca.to_mono(dDirty))
        print(f"    residual (clean vs. bypassed-with-dirty-dials): {residual:.1f}dBFS")
        check("bypass nulls to silence regardless of other capture-pass parameter values",
              residual < -100.0, f"residual {residual:.1f}dBFS")

    print("\n" + ("ALL PHASE 4 CHECKS PASSED" if all(results) else "PHASE 4 CHECKS FAILED"))
    return 0 if all(results) else 1


if __name__ == "__main__":
    sys.exit(main())
