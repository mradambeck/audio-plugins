#!/usr/bin/env python3
"""Runs Phase 0's two verification checks (concrete-sampler-plugin-plan.md) end to end:

  1. Render silence via ConcreteRenderIR and confirm the harness produces a valid WAV of the
     expected length and sample rate.
  2. Generate a 1kHz sine test asset and confirm the analysis module (concrete_analysis.py)
     reports a single partial at 1kHz with no significant harmonics - this is a self-test of the
     analysis harness itself, independent of any DSP (there is none yet - see Phase 1).

Requires a built ConcreteRenderIR (../build/ConcreteRenderIR_artefacts/Release/ConcreteRenderIR -
build it first, console target only, never the AU/VST3/Standalone plugin targets) and this venv's
dependencies (numpy/scipy - see ../../common/tools/requirements.txt).

Usage: python3 verify_phase0.py
"""
import os
import subprocess
import sys
import tempfile

import numpy as np

sys.path.insert(0, os.path.dirname(__file__))
import concrete_analysis as ca
import make_test_assets as assets

HERE = os.path.dirname(__file__)
RENDER_IR_BIN = os.path.join(HERE, "..", "build", "ConcreteRenderIR_artefacts", "Release", "ConcreteRenderIR")


def check_silence_render():
    print("--- Check 1: ConcreteRenderIR produces a valid silent WAV ---")
    if not os.path.exists(RENDER_IR_BIN):
        print(f"FAIL: {RENDER_IR_BIN} not found - build the ConcreteRenderIR target first.")
        return False

    seconds = 2.0
    sample_rate = 44100
    with tempfile.TemporaryDirectory() as tmp:
        out_path = os.path.join(tmp, "silence.wav")
        subprocess.run(
            [RENDER_IR_BIN, "--out", out_path, "--seconds", str(seconds), "--sampleRate", str(sample_rate)],
            check=True, capture_output=True, text=True,
        )

        rate, data = ca.load_wav(out_path)
        expected_frames = int(seconds * sample_rate)
        ok = True

        if rate != sample_rate:
            print(f"FAIL: sample rate {rate} != expected {sample_rate}")
            ok = False
        if data.shape[0] != expected_frames:
            print(f"FAIL: frame count {data.shape[0]} != expected {expected_frames}")
            ok = False
        if data.ndim != 2 or data.shape[1] != 2:
            print(f"FAIL: expected stereo output, got shape {data.shape}")
            ok = False
        peak = float(np.max(np.abs(data)))
        if peak != 0.0:
            print(f"FAIL: expected exact silence, peak = {peak}")
            ok = False

        if ok:
            print(f"PASS: {expected_frames} frames @ {sample_rate}Hz, stereo, exact silence")
        return ok


def check_analysis_self_test():
    print("\n--- Check 2: analysis module correctly identifies a clean 1kHz sine ---")
    signal = assets.sine(1000.0, 2.0)
    freq = 1000.0
    peaks = ca.find_partials(signal, assets.SAMPLE_RATE)

    ok = True
    if len(peaks) != 1:
        print(f"FAIL: expected exactly 1 partial, found {len(peaks)}: {peaks}")
        ok = False
    else:
        measured_freq, measured_db = peaks[0]
        print(f"Measured partial: {measured_freq:.2f}Hz at {measured_db:.2f}dBFS")
        if abs(measured_freq - freq) > 2.0:
            print(f"FAIL: measured frequency {measured_freq:.2f}Hz too far from {freq}Hz")
            ok = False
        if measured_db > 1.0:
            print(f"FAIL: measured level {measured_db:.2f}dBFS should be at/below 0dBFS (amplitude 0.5)")
            ok = False

    if ok:
        print("PASS: single clean partial at 1kHz, no significant harmonics")
    return ok


def main():
    results = [check_silence_render(), check_analysis_self_test()]
    print("\n" + ("ALL PHASE 0 CHECKS PASSED" if all(results) else "PHASE 0 CHECKS FAILED"))
    return 0 if all(results) else 1


if __name__ == "__main__":
    sys.exit(main())
