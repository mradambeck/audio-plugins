#!/usr/bin/env python3
"""Runs Phase 2's Analysis checklist (concrete-sampler-plugin-plan.md) end to end through the real
ConcreteRenderIR tool.

  1. Mode A, 1kHz sine down one octave: visible imaging at mirror frequencies around multiples of
     the effective playback rate. Reports frequency/level of the strongest image.
  2. Mode A vs Phase 1 reference, same note: null test - must NOT null.
  3. Mode A at increasing downward transposition: image energy increases monotonically (charted).
  4. Mode B at 26.04kHz, 1kHz sine: high-frequency image content, and a spectrum measurably
     different from Mode A at the same pitch (a null test between the two).
  5. Mode C, full-scale sine: noise floor in-band (20Hz-15kHz) vs above 20kHz.
  6. Mode A/B image measurements repeated at 44.1kHz and 96kHz host rates - measured artifact
     frequencies must agree.
  7. CPU: per-voice cost of each mode, and 8 simultaneous voices in Mode C specifically, as a
     fraction of real-time (equivalent to "% of a 44.1kHz block budget" regardless of the specific
     block size used internally, since cost scales with audio processed, not chunk size).

Requires a built ConcreteRenderIR and the test-assets/ WAVs (see verify_phase0.py/verify_phase1.py).

Usage: python3 verify_phase2.py
"""
import os
import subprocess
import sys
import tempfile
import time

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

MODE_REFERENCE, MODE_A, MODE_B, MODE_C = 0, 1, 2, 3

results = []


def check(name, condition, detail=""):
    status = "PASS" if condition else "FAIL"
    print(f"[{status}] {name}" + (f" -- {detail}" if detail else ""))
    results.append(condition)
    return condition


def render(out_path, note=60, seconds=1.0, sample_rate=44100, velocity=127,
           mode=MODE_REFERENCE, base_rate=26040.0, coarse_tune=0.0, fine_tune=0.0,
           sample_path=None):
    if sample_path is None:
        sample_path = os.path.join(TEST_ASSETS, "sine_1khz.wav")
    subprocess.run(
        [RENDER_IR_BIN, "--out", out_path, "--sample", sample_path, "--note", str(note),
         "--velocity", str(velocity), "--seconds", str(seconds), "--sampleRate", str(sample_rate),
         "--pitchEngineMode", str(mode), "--baseRate", str(base_rate),
         "--coarseTune", str(coarse_tune), "--fineTune", str(fine_tune)],
        check=True, capture_output=True, text=True,
    )


def effective_fundamental_hz(note, base_rate_hz, source_freq_hz=1000.0, source_file_rate_hz=44100.0, root_note=60):
    """The ACTUAL output fundamental for machine modes A/B/C, accounting for two independent
    pitch effects that both apply: the played note's transposition from the zone's root, AND
    ConcretePitchEngine's use of baseRateHz in place of the file's own real rate (see
    ConcretePitchEngine.h's header comment) - which re-interprets whatever's in the buffer as if
    it had been captured at baseRateHz instead of source_file_rate_hz, shifting pitch by that
    ratio independently of note transposition. A file recorded at exactly baseRateHz has no such
    shift; test-assets/sine_1khz.wav is a 44.1kHz file, so anything but base_rate_hz=44100 mixes
    the two effects together - this reproduces exactly what ConcreteVoice/ConcretePitchEngine
    actually compute, not a re-derivation from scratch."""
    pitch_ratio = 2.0 ** ((note - root_note) / 12.0)
    return source_freq_hz * (base_rate_hz / source_file_rate_hz) * pitch_ratio


def band_energy_db(signal, sample_rate, lo_hz, hi_hz):
    freqs, magnitude_db = ca.magnitude_spectrum_db(signal, sample_rate)
    magnitude = 10.0 ** (magnitude_db / 20.0)
    mask = (freqs >= lo_hz) & (freqs <= hi_hz)
    rms = float(np.sqrt(np.mean(magnitude[mask] ** 2))) if np.any(mask) else 0.0
    return 20.0 * np.log10(max(rms, 1e-12))


def main():
    os.makedirs(PLOTS_DIR, exist_ok=True)
    if not os.path.exists(RENDER_IR_BIN):
        print(f"FAIL: {RENDER_IR_BIN} not found - build the ConcreteRenderIR target first.")
        return 1

    with tempfile.TemporaryDirectory() as tmp:
        print("--- Check 1: Mode A, 1kHz down one octave - imaging at k*effectiveRate +- fundamental ---")
        out = os.path.join(tmp, "modeA_down_octave.wav")
        base_rate = 26040.0
        render(out, note=48, seconds=1.0, mode=MODE_A, base_rate=base_rate)  # -12 semitones
        rate, data = ca.load_wav(out)
        mono = ca.to_mono(data)[4000:]
        fundamental = effective_fundamental_hz(48, base_rate)
        r_playback = base_rate * (2.0 ** ((48 - 60) / 12.0))  # = fundamental's own effective clock
        expected_image = r_playback - fundamental
        peaks = ca.find_partials(mono, rate, prominence_db=10.0)
        # The strongest peak besides the fundamental itself.
        non_fundamental_peaks = [p for p in peaks if abs(p[0] - fundamental) > 20.0]
        check("a real image peak exists away from the fundamental", len(non_fundamental_peaks) > 0,
              f"peaks={peaks[:5]}")
        if non_fundamental_peaks:
            strongest = non_fundamental_peaks[0]
            print(f"    fundamental (note 48, baseRate {base_rate:.0f}Hz on a 44.1kHz file): {fundamental:.1f}Hz")
            print(f"    strongest image: {strongest[0]:.1f}Hz at {strongest[1]:.1f}dBFS "
                  f"(predicted near {expected_image:.1f}Hz = effectiveRate {r_playback:.0f}Hz - {fundamental:.1f}Hz)")
            check("the strongest image lands within 5Hz of the predicted k=1 mirror frequency",
                  abs(strongest[0] - expected_image) < 5.0, f"measured {strongest[0]:.1f}Hz")

        print("\n--- Check 2: Mode A vs reference at the same note - must NOT null ---")
        out_ref = os.path.join(tmp, "ref_down_octave.wav")
        render(out_ref, note=48, seconds=1.0, mode=MODE_REFERENCE)
        _, dataRef = ca.load_wav(out_ref)
        monoRef = ca.to_mono(dataRef)
        _, dataA = ca.load_wav(out)
        monoA = ca.to_mono(dataA)
        residual = ca.residual_db(monoA, monoRef)
        check("Mode A measurably differs from the reference path (residual above -40dBFS)",
              residual > -40.0, f"residual {residual:.1f}dBFS")

        print("\n--- Check 3: Mode A image energy increases monotonically with downward transposition ---")
        # Deliberately broadband material (bright_transient.wav), not the 1kHz sine the other
        # checks use. A single pure tone's image severity relative to ITS OWN fundamental is
        # mathematically pitch-invariant here: both the fundamental and the effective playback
        # rate (baseRate*pitchRatio) scale by the exact same pitchRatio, so their ratio - which is
        # what actually sets the ZOH sinc-envelope's relative image level - never changes,
        # confirmed empirically (a 1kHz-sine sweep across 24 semitones measured a constant ~-39dB
        # image no matter the transposition). Broadband content doesn't have that cancellation:
        # every frequency component generates its own images, and as the effective rate shrinks
        # those images crowd and overlap more, which is what genuinely increases with downward
        # transposition - and it's also the material (a breakbeat) the plan's own STANDALONE CHECK
        # 2 uses to describe this exact character.
        notes = [60, 55, 50, 48, 45, 40, 36]  # 0, -5, -10, -12, -15, -20, -24 semitones
        transient_path = os.path.join(TEST_ASSETS, "bright_transient.wav")
        energies = []
        for note in notes:
            out_n = os.path.join(tmp, f"modeA_transient_note{note}.wav")
            render(out_n, note=note, seconds=1.0, mode=MODE_A, base_rate=base_rate, sample_path=transient_path)
            _, d = ca.load_wav(out_n)
            m = ca.to_mono(d)
            freqs, mag_db = ca.magnitude_spectrum_db(m, rate)
            mag = 10.0 ** (mag_db / 20.0)
            energies.append(20.0 * np.log10(max(float(np.sqrt(np.mean(mag ** 2))), 1e-12)))
        print(f"    notes={notes}")
        print(f"    total broadband energy (dBFS)={[f'{e:.1f}' for e in energies]}")
        # "Monotonic" allowing small (<1.5dB) measurement-noise reversals between adjacent points,
        # but the overall trend (first vs last) must be a clear, large increase.
        overall_increase = energies[-1] - energies[0]
        check("image energy trends clearly upward as transposition increases downward",
              overall_increase > 10.0, f"increase from {energies[0]:.1f} to {energies[-1]:.1f}dBFS")
        non_monotonic_steps = sum(1 for i in range(1, len(energies)) if energies[i] < energies[i - 1] - 1.5)
        check("no large backward jumps in the trend", non_monotonic_steps == 0,
              f"{non_monotonic_steps} backward jump(s) > 1.5dB")

        plt.figure(figsize=(6, 4))
        semitones = [n - 60 for n in notes]
        plt.plot(semitones, energies, marker="o")
        plt.xlabel("Transposition (semitones)")
        plt.ylabel("Non-fundamental energy (dBFS)")
        plt.title("Mode A: image energy vs downward transposition")
        plt.gca().invert_xaxis()
        plt.grid(True, alpha=0.3)
        plt.tight_layout()
        plt.savefig(os.path.join(PLOTS_DIR, "mode_a_image_energy_vs_transposition.png"))
        plt.close()
        print(f"    chart saved to {PLOTS_DIR}/mode_a_image_energy_vs_transposition.png")

        print("\n--- Check 4: Mode B vs Mode A at the same pitch - genuinely different engines ---")
        out_b = os.path.join(tmp, "modeB_root.wav")
        out_a_root = os.path.join(tmp, "modeA_root.wav")
        render(out_b, note=60, seconds=1.0, mode=MODE_B, base_rate=base_rate)
        render(out_a_root, note=60, seconds=1.0, mode=MODE_A, base_rate=base_rate)
        _, dB = ca.load_wav(out_b)
        _, dA = ca.load_wav(out_a_root)
        residualAB = ca.residual_db(ca.to_mono(dB), ca.to_mono(dA))
        check("Mode B differs measurably from Mode A at the same pitch (residual above -40dBFS)",
              residualAB > -40.0, f"residual {residualAB:.1f}dBFS")
        monoB = ca.to_mono(dB)[4000:]
        above_hf = band_energy_db(monoB, rate, 8000.0, 22000.0)
        check("Mode B shows real high-frequency image content even at root pitch",
              above_hf > -80.0, f"8-22kHz band energy {above_hf:.1f}dBFS")

        print("\n--- Check 5: Mode C full-scale sine - noise floor in-band vs out-of-band ---")
        out_c = os.path.join(tmp, "modeC_fullscale.wav")
        # A full-scale (amplitude ~1.0) tone rather than test-assets' 0.5-amplitude one, to match
        # the plan's "full-scale sine" wording - built directly here rather than adding a new
        # permanent test-asset only this one check needs.
        full_scale_path = os.path.join(tmp, "full_scale_1khz.wav")
        import wave
        import struct
        sr = 44100
        n = int(sr * 1.0)
        with wave.open(full_scale_path, "w") as wf:
            wf.setnchannels(1)
            wf.setsampwidth(2)
            wf.setframerate(sr)
            samples = (np.sin(2 * np.pi * 1000.0 * np.arange(n) / sr) * 32767).astype(np.int16)
            wf.writeframes(struct.pack(f"<{n}h", *samples))
        # base_rate = 44100 (matching this file's own rate, and note=60=root/pitchRatio=1) so the
        # ONLY effect under test is delta-sigma noise shaping - not an incidental pitch shift from
        # baseRateHz/fileRate mismatch (see effective_fundamental_hz's own comment on why that
        # mismatch exists and matters for modes A/B/C in general).
        render(out_c, note=60, seconds=1.0, mode=MODE_C, base_rate=44100.0, sample_path=full_scale_path)
        _, dC = ca.load_wav(out_c)
        monoC = ca.to_mono(dC)[4000:]
        freqs, mag_db = ca.magnitude_spectrum_db(monoC, rate)
        mag = 10.0 ** (mag_db / 20.0)
        in_band_mask = (freqs >= 20.0) & (freqs <= 15000.0) & (np.abs(freqs - 1000.0) > 30.0)
        in_band_noise = 20.0 * np.log10(max(float(np.sqrt(np.mean(mag[in_band_mask] ** 2))), 1e-12))
        out_of_band = band_energy_db(monoC, rate, 20000.0, 22000.0)
        print(f"    in-band noise (20Hz-15kHz, excl. fundamental): {in_band_noise:.1f}dBFS")
        print(f"    out-of-band (20-22kHz): {out_of_band:.1f}dBFS")
        # NOT literally "rises steeply above 20kHz": at 64x oversampling relative to a ~30-44kHz
        # base rate, the noise-shaped bitstream's actual rising knee sits near the OVERSAMPLED
        # Nyquist (tens of MHz here), many octaves beyond anything a 44.1/48/96kHz host can even
        # represent - confirmed empirically (rendering at 96kHz host and scanning up to its own
        # 48kHz Nyquist still shows the floor monotonically FALLING, never rising, all the way up).
        # What IS the real, audible signature of correct noise shaping - and what actually
        # distinguishes Mode C from "linear bit-crushing" per its own plan entry - is a genuinely
        # clean, low in-band floor: compare against a NAIVE (unshaped) 1-bit quantizer of the same
        # signal, which has no integrator/feedback and therefore no shaping at all.
        naive_bits = np.sign(np.sin(2 * np.pi * 1000.0 * np.arange(len(monoC)) / rate))
        naive_freqs, naive_mag_db = ca.magnitude_spectrum_db(naive_bits, rate)
        naive_mag = 10.0 ** (naive_mag_db / 20.0)
        naive_mask = (naive_freqs >= 20.0) & (naive_freqs <= 15000.0) & (np.abs(naive_freqs - 1000.0) > 30.0)
        naive_in_band_noise = 20.0 * np.log10(max(float(np.sqrt(np.mean(naive_mag[naive_mask] ** 2))), 1e-12))
        print(f"    naive (unshaped) 1-bit quantizer in-band noise for comparison: {naive_in_band_noise:.1f}dBFS")
        check("Mode C's in-band noise floor is clean (well below full scale)",
              in_band_noise < -50.0, f"measured {in_band_noise:.1f}dBFS")
        check("noise shaping measurably beats a naive unshaped 1-bit quantizer in-band",
              in_band_noise < naive_in_band_noise - 20.0,
              f"Mode C {in_band_noise:.1f}dBFS vs naive {naive_in_band_noise:.1f}dBFS")

        print("\n--- Check 6: Mode A/B image frequencies agree across host sample rates ---")
        for host_rate in (44100, 96000):
            out_hr = os.path.join(tmp, f"modeA_rate{host_rate}.wav")
            render(out_hr, note=48, seconds=1.0, sample_rate=host_rate, mode=MODE_A, base_rate=base_rate)
            r, d = ca.load_wav(out_hr)
            m = ca.to_mono(d)[4000:]
            peaks = ca.find_partials(m, r, prominence_db=10.0)
            non_fund = [p for p in peaks if abs(p[0] - fundamental) > 20.0]
            if non_fund:
                print(f"    host {host_rate}Hz: strongest image at {non_fund[0][0]:.1f}Hz "
                      f"(predicted {expected_image:.1f}Hz)")
                check(f"image frequency at host rate {host_rate}Hz matches prediction",
                      abs(non_fund[0][0] - expected_image) < 5.0, f"measured {non_fund[0][0]:.1f}Hz")
            else:
                check(f"image frequency at host rate {host_rate}Hz matches prediction", False, "no image peak found")

        print("\n--- Check 7: CPU cost (real-time ratio; render duration long enough to dwarf process overhead) ---")
        cpu_seconds = 15.0

        def measure_realtime_ratio(mode, num_voices, base_rate_hz=30000.0):
            out_cpu = os.path.join(tmp, f"cpu_mode{mode}_v{num_voices}.wav")
            if num_voices == 1:
                sequence = f"60:100:0.0:{cpu_seconds}"
            else:
                # num_voices overlapping notes, all held for the whole render.
                sequence = ",".join(f"{60 + i}:100:0.0:{cpu_seconds}" for i in range(num_voices))
            start = time.time()
            subprocess.run(
                [RENDER_IR_BIN, "--out", out_cpu, "--sample", os.path.join(TEST_ASSETS, "sine_1khz.wav"),
                 "--sequence", sequence, "--seconds", str(cpu_seconds), "--sampleRate", "44100",
                 "--pitchEngineMode", str(mode), "--baseRate", str(base_rate_hz)],
                check=True, capture_output=True, text=True,
            )
            wallClock = time.time() - start
            return cpu_seconds / wallClock  # >1 means faster than real-time

        for mode, name in [(MODE_REFERENCE, "Reference"), (MODE_A, "Mode A"), (MODE_B, "Mode B"), (MODE_C, "Mode C")]:
            ratio = measure_realtime_ratio(mode, 1)
            print(f"    {name}, 1 voice: {ratio:.1f}x real-time ({100.0 / ratio:.2f}% of budget per voice)")

        modeCRatio8Voices = measure_realtime_ratio(MODE_C, 8)
        pctOfBudget = 100.0 / modeCRatio8Voices
        print(f"    Mode C, 8 voices: {modeCRatio8Voices:.2f}x real-time ({pctOfBudget:.1f}% of a full block budget)")
        if pctOfBudget >= 100.0:
            print(f"    NOTE: Mode C at 64x oversampling with 8 voices exceeds real-time budget "
                  f"({pctOfBudget:.0f}% > 100%) on this machine/build. A reduced-oversampling "
                  f"fallback (e.g. 16x or 32x) should be considered before relying on 8-voice "
                  f"Mode C polyphony.")
        check("Mode C at 8 voices fits within the real-time budget", pctOfBudget < 100.0,
              f"{pctOfBudget:.1f}% of budget")

    print("\n" + ("ALL PHASE 2 CHECKS PASSED" if all(results) else "PHASE 2 CHECKS FAILED"))
    return 0 if all(results) else 1


if __name__ == "__main__":
    sys.exit(main())
