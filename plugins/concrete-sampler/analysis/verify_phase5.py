#!/usr/bin/env python3
"""Runs Phase 5's Analysis checklist (concrete-sampler-plugin-plan.md) end to end through the real
ConcreteRenderIR tool.

  1. Frequency-response slope of each ladder model (SSM/CEM Loss/CEM Compensated) at low
     resonance: close to 24dB/octave (4-pole).
  2. Resonance sweep on both CEM models: CEM Loss loses passband level as resonance rises; CEM
     Compensated holds it roughly steady.
  3. SSM driven hot: produces real harmonic distortion (not silence, not a flat pass-through).
  4. Self-oscillation: an impulse into the ladder at resonance=1 produces a SUSTAINED tone at (or
     near) the cutoff frequency, long after the impulse itself has died out.
  5. Per-voice independence: a 4-note chord through a high-resonance filter must match the sum of
     the four notes rendered and filtered INDEPENDENTLY - proof no filter state leaks between
     voices.

Thread safety and "turning the live filter knobs doesn't trigger a re-bake" are structural/code-
level guarantees (the filter parameters are simply never registered as APVTS listeners - see
PluginProcessor.h's own parameterChanged() comment), not spectral claims, so they're not re-tested
here.

Requires a built ConcreteRenderIR and the test-assets/ WAVs (see verify_phase0.py/verify_phase1.py).

Usage: python3 verify_phase5.py
"""
import os
import struct
import subprocess
import sys
import tempfile
import wave

import numpy as np

sys.path.insert(0, os.path.dirname(__file__))
import concrete_analysis as ca

HERE = os.path.dirname(__file__)
RENDER_IR_BIN = os.path.join(HERE, "..", "build", "ConcreteRenderIR_artefacts", "Release", "ConcreteRenderIR")
TEST_ASSETS = os.path.join(HERE, "..", "test-assets")

FILTER_BYPASS, FILTER_SSM, FILTER_CEM_LOSS, FILTER_CEM_COMP, FILTER_DIGITAL_VCA, FILTER_ONE_POLE = range(6)

results = []


def check(name, condition, detail=""):
    status = "PASS" if condition else "FAIL"
    print(f"[{status}] {name}" + (f" -- {detail}" if detail else ""))
    results.append(condition)
    return condition


def write_mono_sine_wav(path, freq_hz, amplitude=0.5, seconds=1.0, sample_rate=44100):
    n = int(seconds * sample_rate)
    samples = (amplitude * np.sin(2 * np.pi * freq_hz * np.arange(n) / sample_rate) * 32767).astype(np.int16)
    with wave.open(path, "w") as wf:
        wf.setnchannels(1)
        wf.setsampwidth(2)
        wf.setframerate(sample_rate)
        wf.writeframes(struct.pack(f"<{n}h", *samples))


def render(out_path, note=60, seconds=1.0, sample_rate=44100, velocity=127, sample_path=None, sequence=None,
           filter_model=FILTER_BYPASS, filter_cutoff=20000.0, filter_resonance=0.0,
           filter_env_amount=0.0, filter_key_track=0.0,
           capture_bypass=True, capture_transpose=0.0):
    if sample_path is None:
        sample_path = os.path.join(TEST_ASSETS, "sine_1khz.wav")
    args = [RENDER_IR_BIN, "--out", out_path, "--sample", sample_path,
            "--velocity", str(velocity), "--seconds", str(seconds), "--sampleRate", str(sample_rate),
            "--pitchEngineMode", "0",  # Reference throughout - isolates the filter from Phase 2's pitch engines
            # Capture pass bypassed by default, isolating Phase 5's filter from Phase 4's capture pass.
            "--captureBypass", "1" if capture_bypass else "0", "--captureTranspose", str(capture_transpose),
            "--filterModel", str(filter_model), "--filterCutoff", str(filter_cutoff),
            "--filterResonance", str(filter_resonance), "--filterEnvAmount", str(filter_env_amount),
            "--filterKeyTrack", str(filter_key_track)]
    if sequence is not None:
        args += ["--sequence", sequence]
    else:
        args += ["--note", str(note)]
    subprocess.run(args, check=True, capture_output=True, text=True)


def level_at_freq_db(mono, rate, freq_hz, notch_hz=15.0):
    """Peak magnitude in dB right at freq_hz (nearest bin), for a steady-state sine tone."""
    freqs, mag_db = ca.magnitude_spectrum_db(mono, rate)
    mask = np.abs(freqs - freq_hz) <= notch_hz
    return float(np.max(mag_db[mask])) if np.any(mask) else -120.0


def main():
    if not os.path.exists(RENDER_IR_BIN):
        print(f"FAIL: {RENDER_IR_BIN} not found - build the ConcreteRenderIR target first.")
        return 1

    with tempfile.TemporaryDirectory() as tmp:
        print("--- Check 1: Frequency-response slope of each ladder model (~24dB/octave expected) ---")
        cutoff = 1000.0
        for model_name, model in (("SSM", FILTER_SSM), ("CEM Loss", FILTER_CEM_LOSS), ("CEM Compensated", FILTER_CEM_COMP)):
            levels = {}
            for freq in (2000.0, 4000.0):
                tone_path = os.path.join(tmp, f"tone_{freq:.0f}.wav")
                write_mono_sine_wav(tone_path, freq, amplitude=0.5)
                out_path = os.path.join(tmp, f"slope_{model_name}_{freq:.0f}.wav")
                render(out_path, sample_path=tone_path, filter_model=model, filter_cutoff=cutoff, filter_resonance=0.0)
                rate, data = ca.load_wav(out_path)
                mono = ca.to_mono(data)[4000:]
                levels[freq] = level_at_freq_db(mono, rate, freq)
            slope = levels[4000.0] - levels[2000.0]
            print(f"    {model_name}: {levels[2000.0]:.1f}dB @2kHz, {levels[4000.0]:.1f}dB @4kHz, slope={slope:.1f}dB/oct")
            check(f"{model_name} rolls off close to 24dB/octave (within 8dB)", abs(slope - (-24.0)) < 8.0,
                  f"measured {slope:.1f}dB/oct")

        print("\n--- Check 2: CEM resonance sweep - Loss loses passband level, Compensated holds it ---")
        tone_path = os.path.join(tmp, "tone_200.wav")
        write_mono_sine_wav(tone_path, 200.0, amplitude=0.5)

        def measure_passband(model, resonance):
            out_path = os.path.join(tmp, f"cem_{model}_{resonance}.wav")
            render(out_path, sample_path=tone_path, filter_model=model, filter_cutoff=2000.0, filter_resonance=resonance)
            rate, data = ca.load_wav(out_path)
            return level_at_freq_db(ca.to_mono(data)[4000:], rate, 200.0)

        lossLow = measure_passband(FILTER_CEM_LOSS, 0.0)
        lossHigh = measure_passband(FILTER_CEM_LOSS, 0.9)
        compLow = measure_passband(FILTER_CEM_COMP, 0.0)
        compHigh = measure_passband(FILTER_CEM_COMP, 0.9)
        print(f"    CEM Loss: {lossLow:.1f}dB -> {lossHigh:.1f}dB (delta {lossHigh - lossLow:+.1f}dB)")
        print(f"    CEM Compensated: {compLow:.1f}dB -> {compHigh:.1f}dB (delta {compHigh - compLow:+.1f}dB)")
        check("CEM Compensated's passband level moves less across the resonance sweep than CEM Loss's",
              abs(compHigh - compLow) < abs(lossHigh - lossLow),
              f"Loss moved {abs(lossHigh - lossLow):.1f}dB, Compensated moved {abs(compHigh - compLow):.1f}dB")

        print("\n--- Check 3: SSM driven hot produces real harmonic distortion ---")
        out_clean = os.path.join(tmp, "ssm_clean.wav")
        out_driven = os.path.join(tmp, "ssm_driven.wav")
        render(out_clean, sample_path=tone_path, filter_model=FILTER_BYPASS)
        render(out_driven, sample_path=tone_path, filter_model=FILTER_SSM, filter_cutoff=5000.0, filter_resonance=0.8)
        _, dClean = ca.load_wav(out_clean)
        _, dDriven = ca.load_wav(out_driven)
        rate = 44100
        thdClean = ca.thd_percent(ca.to_mono(dClean)[4000:], rate, 200.0)
        thdDriven = ca.thd_percent(ca.to_mono(dDriven)[4000:], rate, 200.0)
        print(f"    THD: bypass={thdClean:.3f}%, SSM driven (resonance 0.8)={thdDriven:.2f}%")
        check("SSM at high resonance produces measurably more harmonic distortion than bypass "
              "(at least a 10x increase)",
              thdDriven > thdClean * 10.0, f"{thdClean:.3f}% -> {thdDriven:.2f}%")

        print("\n--- Check 4: Self-oscillation - an impulse rings on at (near) the cutoff frequency ---")
        impulse_path = os.path.join(TEST_ASSETS, "impulse.wav")
        out_osc = os.path.join(tmp, "self_oscillation.wav")
        render(out_osc, sample_path=impulse_path, filter_model=FILTER_SSM, filter_cutoff=1200.0, filter_resonance=1.0)
        rate, data = ca.load_wav(out_osc)
        mono = ca.to_mono(data)
        # Well after the impulse itself (and its immediate filter transient) - only genuine
        # self-oscillation should still be present this far in, with no other input arriving.
        tail = mono[20000:]
        peaks = ca.find_partials(tail, rate, prominence_db=6.0)
        print(f"    tail RMS: {20*np.log10(max(float(np.sqrt(np.mean(tail**2))), 1e-9)):.1f}dBFS, top peaks: {peaks[:3]}")
        check("a real, non-silent tail exists long after the impulse (self-oscillation, not a decayed echo)",
              float(np.sqrt(np.mean(tail ** 2))) > 0.001, f"RMS {float(np.sqrt(np.mean(tail ** 2))):.4f}")
        if peaks:
            check("the sustained tail's strongest partial sits close to the filter's own cutoff (1200Hz)",
                  abs(peaks[0][0] - 1200.0) < 150.0, f"measured {peaks[0][0]:.1f}Hz")

        print("\n--- Check 5: Per-voice independence - a 4-note chord matches the sum of 4 independent notes ---")
        chord_notes = [60, 64, 67, 71]
        individual = None
        for note in chord_notes:
            out_n = os.path.join(tmp, f"chord_note{note}.wav")
            render(out_n, note=note, seconds=1.0, filter_model=FILTER_SSM, filter_cutoff=800.0,
                   filter_resonance=0.85, filter_env_amount=3.0)
            rate, d = ca.load_wav(out_n)
            mono = ca.to_mono(d)
            individual = mono if individual is None else individual + mono

        out_chord = os.path.join(tmp, "chord_together.wav")
        sequence = ",".join(f"{n}:127:0.0:1.0" for n in chord_notes)
        render(out_chord, sequence=sequence, seconds=1.0, filter_model=FILTER_SSM, filter_cutoff=800.0,
               filter_resonance=0.85, filter_env_amount=3.0)
        _, dChord = ca.load_wav(out_chord)
        monoChord = ca.to_mono(dChord)

        residual = ca.residual_db(monoChord, individual)
        print(f"    residual (chord vs. sum of 4 independent renders): {residual:.1f}dBFS")
        check("a 4-note chord through the filter matches the sum of 4 independently-filtered notes "
              "(no shared/leaking filter state between voices)",
              residual < -40.0, f"residual {residual:.1f}dBFS")

    print("\n" + ("ALL PHASE 5 CHECKS PASSED" if all(results) else "PHASE 5 CHECKS FAILED"))
    return 0 if all(results) else 1


if __name__ == "__main__":
    sys.exit(main())
