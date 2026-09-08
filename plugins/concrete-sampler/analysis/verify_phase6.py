#!/usr/bin/env python3
"""Runs Phase 6's Analysis checklist (concrete-sampler-plugin-plan.md) end to end through the real
ConcreteRenderIR tool.

  1. Voice count limit: 6 overlapping notes with a 4-voice limit - exactly the last 4 triggered
     should be audible, the first 2 stolen silently (deterministic oldest-first stealing, matching
     ConcreteVoiceAllocatorTests.cpp's own unit-level coverage of the policy itself).
  2. Velocity response: render velocity 1/32/64/96/127 and confirm output level increases
     monotonically and tracks velocity/127 (Phase 1's flat linear velocityGain).
  3. Contoured amp envelope mode: measurably quieter later in a long held note (the K250-style
     continuously-decaying shape - see ConcreteContourEnvelope.h), unlike ADSR's flat sustain over
     the same span.

Choke groups and "voice count changes per machine" are structural/end-to-end-MIDI-dispatch
concerns rather than spectral claims (they need two zones sharing a chokeGroup, which v1's UI/
RenderIR can't construct with a single --sample file - see ConcreteProcessorTests.cpp's own
"Choke groups" test, which builds a two-zone set directly in C++), so they're not re-tested here.

Requires a built ConcreteRenderIR and the test-assets/ WAVs (see verify_phase0.py/verify_phase1.py).

Usage: python3 verify_phase6.py
"""
import os
import subprocess
import sys
import tempfile

import numpy as np

sys.path.insert(0, os.path.dirname(__file__))
import concrete_analysis as ca

HERE = os.path.dirname(__file__)
RENDER_IR_BIN = os.path.join(HERE, "..", "build", "ConcreteRenderIR_artefacts", "Release", "ConcreteRenderIR")
TEST_ASSETS = os.path.join(HERE, "..", "test-assets")

AMP_ENV_ADSR, AMP_ENV_CONTOURED = 0, 1

results = []


def check(name, condition, detail=""):
    status = "PASS" if condition else "FAIL"
    print(f"[{status}] {name}" + (f" -- {detail}" if detail else ""))
    results.append(condition)
    return condition


def render(out_path, note=60, seconds=1.0, sample_rate=44100, velocity=100, sample_path=None, sequence=None,
           voice_count=8, amp_envelope_mode=AMP_ENV_ADSR):
    if sample_path is None:
        sample_path = os.path.join(TEST_ASSETS, "sine_1khz.wav")
    args = [RENDER_IR_BIN, "--out", out_path, "--sample", sample_path,
            "--velocity", str(velocity), "--seconds", str(seconds), "--sampleRate", str(sample_rate),
            "--pitchEngineMode", "0",  # Reference throughout - isolates Phase 6 from Phase 2's engines
            "--captureBypass", "1",  # isolates Phase 6 from the capture pass entirely
            "--voiceCount", str(voice_count), "--ampEnvelopeMode", str(amp_envelope_mode)]
    if sequence is not None:
        args += ["--sequence", sequence]
    else:
        args += ["--note", str(note)]
    subprocess.run(args, check=True, capture_output=True, text=True)


def level_at_freq_db(mono, rate, freq_hz, notch_hz=8.0):
    """Peak magnitude in dB right at freq_hz (nearest bin), for a steady-state sine tone."""
    freqs, mag_db = ca.magnitude_spectrum_db(mono, rate)
    mask = np.abs(freqs - freq_hz) <= notch_hz
    return float(np.max(mag_db[mask])) if np.any(mask) else -120.0


def rms_window(mono, start, length):
    seg = mono[start:start + length]
    return float(np.sqrt(np.mean(seg ** 2))) if len(seg) else 0.0


def main():
    if not os.path.exists(RENDER_IR_BIN):
        print(f"FAIL: {RENDER_IR_BIN} not found - build the ConcreteRenderIR target first.")
        return 1

    with tempfile.TemporaryDirectory() as tmp:
        print("--- Check 1: Voice Count limit - 6 overlapping notes, 4-voice limit, deterministic stealing ---")
        # Each note is a semitone apart (>= 59Hz spacing at 1kHz root - well clear of the 8Hz notch
        # used below), all triggered at once and held throughout with no note-off, matching the
        # plan's literal "6 overlapping notes" wording.
        notes = [60, 61, 62, 63, 64, 65]
        freqs = {n: 1000.0 * 2.0 ** ((n - 60) / 12.0) for n in notes}
        sequence = ",".join(f"{n}:100:0.0:0" for n in notes)
        out_path = os.path.join(tmp, "voice_limit.wav")
        render(out_path, sequence=sequence, seconds=1.0, voice_count=4)
        rate, data = ca.load_wav(out_path)
        mono = ca.to_mono(data)[4000:]  # past the attack transient

        levels = {n: level_at_freq_db(mono, rate, freqs[n]) for n in notes}
        for n in notes:
            print(f"    note {n} ({freqs[n]:.1f}Hz): {levels[n]:.1f}dB")

        # Deterministic oldest-first stealing (see ConcreteVoiceAllocator.h): the first two notes
        # triggered (60, 61) are the ones stolen from once the 4-voice limit is exceeded; the last
        # four (62-65) are the ones actually sounding.
        for n in (60, 61):
            check(f"note {n} (stolen by the voice-count limit) is near-silent", levels[n] < -50.0,
                  f"{levels[n]:.1f}dB")
        for n in (62, 63, 64, 65):
            check(f"note {n} (within the 4-voice limit) is clearly audible", levels[n] > -30.0,
                  f"{levels[n]:.1f}dB")

        print("\n--- Check 2: Velocity response - output level increases monotonically and roughly "
              "linearly with velocity ---")
        rms_by_velocity = {}
        for vel in (1, 32, 64, 96, 127):
            out_v = os.path.join(tmp, f"vel_{vel}.wav")
            render(out_v, note=60, seconds=0.3, velocity=vel)
            rate, data = ca.load_wav(out_v)
            mono = ca.to_mono(data)[2000:]
            rms_by_velocity[vel] = float(np.sqrt(np.mean(mono ** 2)))
            print(f"    velocity {vel}: RMS {rms_by_velocity[vel]:.4f}")

        velocities = sorted(rms_by_velocity)
        for prev_v, v in zip(velocities, velocities[1:]):
            check(f"velocity {v} louder than velocity {prev_v}", rms_by_velocity[v] > rms_by_velocity[prev_v],
                  f"{rms_by_velocity[prev_v]:.4f} -> {rms_by_velocity[v]:.4f}")

        ratio_at_max = rms_by_velocity[127]
        for vel in (32, 64, 96):
            expected_ratio = vel / 127.0
            actual_ratio = rms_by_velocity[vel] / ratio_at_max
            check(f"velocity {vel}'s level relative to 127 tracks {expected_ratio:.2f} (linear gain)",
                  abs(actual_ratio - expected_ratio) < 0.05, f"measured ratio {actual_ratio:.3f}")

        print("\n--- Check 3: Contoured amp envelope decays over a held note; ADSR stays flat ---")
        # Played an octave down (note 48) so the 2s source spans ~4s of output - long enough to see
        # the contour's decay2 stage clearly separate from its own attack/decay1.
        out_contoured = os.path.join(tmp, "contour.wav")
        render(out_contoured, note=48, seconds=4.0, amp_envelope_mode=AMP_ENV_CONTOURED)
        rate, data = ca.load_wav(out_contoured)
        mono = ca.to_mono(data)
        contouredEarly = rms_window(mono, 4000, 4000)
        contouredLate = rms_window(mono, int(3.5 * rate), 4000)
        print(f"    contoured: early RMS {contouredEarly:.4f}, late (3.5s) RMS {contouredLate:.4f}")
        check("contoured envelope is measurably quieter later while the note is still held",
              contouredLate < contouredEarly * 0.5, f"early={contouredEarly:.4f}, late={contouredLate:.4f}")

        out_adsr = os.path.join(tmp, "adsr.wav")
        render(out_adsr, note=48, seconds=4.0, amp_envelope_mode=AMP_ENV_ADSR)
        rate, data = ca.load_wav(out_adsr)
        mono = ca.to_mono(data)
        adsrEarly = rms_window(mono, 4000, 4000)
        adsrLate = rms_window(mono, int(3.5 * rate), 4000)
        print(f"    ADSR: early RMS {adsrEarly:.4f}, late (3.5s) RMS {adsrLate:.4f}")
        check("ADSR's flat sustain stays essentially constant over the same held span",
              abs(adsrLate - adsrEarly) < adsrEarly * 0.1, f"early={adsrEarly:.4f}, late={adsrLate:.4f}")

    print("\n" + ("ALL PHASE 6 CHECKS PASSED" if all(results) else "PHASE 6 CHECKS FAILED"))
    return 0 if all(results) else 1


if __name__ == "__main__":
    sys.exit(main())
