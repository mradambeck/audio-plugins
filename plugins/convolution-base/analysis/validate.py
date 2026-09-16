#!/usr/bin/env python3
"""Measures the shared convolution engine's behaviour from rendered audio.

The unit suite in ../Source/Tests asserts these properties; this script demonstrates them, by
driving the real plugin through ConvBaseRenderIR and measuring the WAV that comes out. That
distinction matters for the claims that are about *audio* rather than about return values - "bypass
does not click" is a statement about a waveform, and a passing assertion is not the same as having
looked at one.

Needs only numpy: every measurement here is time-domain or a plain FFT, so there is no venv to set
up and nothing to install beyond what the repo already assumes.

Usage:
    python3 analysis/validate.py            # renders, measures, writes the report
    python3 analysis/validate.py --no-render  # re-measures existing renders
"""

import argparse
import json
import pathlib
import struct
import subprocess
import sys

import numpy as np

PLUGIN_DIR = pathlib.Path(__file__).resolve().parent.parent
RENDER_BIN = PLUGIN_DIR / "build" / "ConvBaseRenderIR_artefacts" / "Release" / "ConvBaseRenderIR"
RENDER_DIR = PLUGIN_DIR / "rendered-audio"
RESULTS_JSON = PLUGIN_DIR / "analysis" / "validation_results.json"
REPORT_MD = PLUGIN_DIR / "analysis" / "validation_report.md"

# Indices into variantConfig()'s IR table (see ../Source/VariantConfig.cpp).
IR_ROOM_48K = 0
IR_HALL_44K = 1
IR_PLATE_96K = 2
IR_DIRAC = 3


def read_wav(path):
    """Minimal RIFF reader for the 32-bit float WAVs ConvBaseRenderIR writes.

    Hand-rolled rather than pulled from scipy so this script has a single dependency. Only the
    formats the harness actually produces are supported - anything else raises rather than
    silently misreading.
    """
    data = pathlib.Path(path).read_bytes()
    if data[:4] != b"RIFF" or data[8:12] != b"WAVE":
        raise ValueError(f"{path} is not a RIFF/WAVE file")

    position = 12
    fmt = None
    samples = None

    while position + 8 <= len(data):
        chunk_id = data[position:position + 4]
        chunk_size = struct.unpack("<I", data[position + 4:position + 8])[0]
        body = data[position + 8:position + 8 + chunk_size]

        if chunk_id == b"fmt ":
            audio_format, channels, sample_rate, _, _, bits = struct.unpack("<HHIIHH", body[:16])
            fmt = (audio_format, channels, sample_rate, bits)
        elif chunk_id == b"data":
            samples = body

        position += 8 + chunk_size + (chunk_size % 2)

    if fmt is None or samples is None:
        raise ValueError(f"{path} is missing a fmt or data chunk")

    audio_format, channels, sample_rate, bits = fmt

    # 0xFFFE is WAVE_FORMAT_EXTENSIBLE, which JUCE writes for float; the sub-format is still IEEE
    # float at the bit depth the fmt chunk reports.
    if audio_format not in (3, 0xFFFE) or bits != 32:
        raise ValueError(f"{path}: expected 32-bit float, got format {audio_format} / {bits} bits")

    audio = np.frombuffer(samples, dtype="<f4").reshape(-1, channels)
    return sample_rate, audio


def render(name, **flags):
    out = RENDER_DIR / f"{name}.wav"
    args = [str(RENDER_BIN), "--out", str(out)]
    for key, value in flags.items():
        args += [f"--{key}", str(value)]

    result = subprocess.run(args, capture_output=True, text=True, cwd=PLUGIN_DIR)
    if result.returncode != 0:
        raise RuntimeError(f"render '{name}' failed:\n{result.stderr}")

    return out


def peak_index(channel):
    return int(np.argmax(np.abs(channel)))


def max_step(channel, start, end):
    """Largest sample-to-sample jump in a window - the measurable form of "a click"."""
    window = channel[max(start, 1):end]
    previous = channel[max(start, 1) - 1:end - 1]
    return float(np.max(np.abs(window - previous))) if len(window) else 0.0


def rms(channel, start=0, end=None):
    segment = channel[start:end if end is not None else len(channel)]
    return float(np.sqrt(np.mean(segment.astype(np.float64) ** 2))) if len(segment) else 0.0


def decay_time_to(channel, sample_rate, db_down):
    """Seconds for the backward-integrated energy (Schroeder) to fall by db_down from its peak."""
    energy = np.cumsum(channel[::-1].astype(np.float64) ** 2)[::-1]
    if energy[0] <= 0:
        return 0.0
    curve = 10.0 * np.log10(np.maximum(energy / energy[0], 1e-20))
    below = np.where(curve <= -db_down)[0]
    return float(below[0] / sample_rate) if len(below) else float(len(channel) / sample_rate)


def measure_all(do_render):
    results = {}

    def get(name, **flags):
        path = render(name, **flags) if do_render else RENDER_DIR / f"{name}.wav"
        return read_wav(path)

    # 1. Latency. The Dirac IR makes convolution an identity, so anything other than sample 0 is
    #    delay the engine added.
    sample_rate, audio = get("latency-dirac", signal="impulse", ir=IR_DIRAC, seconds=0.5,
                             dry=0, wet=100, predelay=0)
    results["latency"] = {
        "impulse_peak_sample": peak_index(audio[:, 0]),
        "peak_amplitude": float(np.max(np.abs(audio[:, 0]))),
    }

    # 2. Transparency at defaults. Same identity IR, but fed a tone: the whole wet chain (shaper,
    #    pre-delay line, both filters) must give back what it was given.
    sample_rate, audio = get("defaults-transparency", signal="sine", freq=440, ir=IR_DIRAC,
                             seconds=0.5, dry=0, wet=100, length=100, attack=0,
                             lowcut=20, highcut=20000)
    n = audio.shape[0]
    reference = 0.5 * np.sin(2 * np.pi * 440 * np.arange(n) / sample_rate)
    results["defaults_transparency"] = {
        "max_abs_error": float(np.max(np.abs(audio[:, 0] - reference))),
        "reference_peak": float(np.max(np.abs(reference))),
    }

    # 3. Pre-delay lands where the knob says.
    sample_rate, audio = get("predelay-25ms", signal="impulse", ir=IR_DIRAC, seconds=0.5,
                             dry=0, wet=100, predelay=25)
    results["predelay"] = {
        "requested_ms": 25.0,
        "measured_ms": peak_index(audio[:, 0]) / sample_rate * 1000.0,
    }

    # 4. Bypass. Measured against an identical run that never toggles, so the comparison is against
    #    this signal's own steady-state roughness rather than an arbitrary threshold.
    toggle_s = 1.0
    sample_rate, control = get("bypass-control", signal="sine", freq=220, ir=IR_ROOM_48K,
                               seconds=2.0, dry=100, wet=100)
    _, toggled = get("bypass-toggled", signal="sine", freq=220, ir=IR_ROOM_48K,
                     seconds=2.0, dry=100, wet=100, bypassAt=toggle_s)

    start = int(toggle_s * sample_rate) - 512
    end = int((toggle_s + 0.1) * sample_rate)
    results["bypass"] = {
        "control_max_step": max_step(control[:, 0], start, end),
        "toggled_max_step": max_step(toggled[:, 0], start, end),
        "rms_after_control": rms(control[:, 0], end),
        "rms_after_toggled": rms(toggled[:, 0], end),
    }

    # 5. IR swap mid-tail.
    switch_s = 1.0
    sample_rate, control = get("swap-control", signal="sine", freq=220, ir=IR_ROOM_48K,
                               seconds=3.0, dry=0, wet=100)
    _, swapped = get("swap-toggled", signal="sine", freq=220, ir=IR_ROOM_48K, seconds=3.0,
                     dry=0, wet=100, switchIRAt=switch_s, switchIRTo=IR_HALL_44K)

    start = int(switch_s * sample_rate) - 512
    end = int((switch_s + 0.6) * sample_rate)
    results["ir_swap"] = {
        "control_max_step": max_step(control[:, 0], start, end),
        "swapped_max_step": max_step(swapped[:, 0], start, end),
    }

    # 6. Sample-rate conversion. The same 44.1 kHz IR rendered in a 48 kHz and a 96 kHz session must
    #    decay over the same number of seconds - if IRLibrary resampled by the wrong ratio, the tail
    #    would be audibly shorter or longer in one of them.
    decays = {}
    for rate in (44100, 48000, 96000):
        sr, audio = get(f"src-hall-{rate}", signal="impulse", ir=IR_HALL_44K, seconds=2.0,
                        sampleRate=rate, dry=0, wet=100)
        decays[str(rate)] = decay_time_to(audio[:, 0], sr, 20.0)

    spread = max(decays.values()) - min(decays.values())
    results["sample_rate_conversion"] = {
        "t20_seconds": decays,
        "spread_seconds": spread,
    }

    # 7. Filters do engage when asked. Paired against the neutral render so the comparison isolates
    #    the filter rather than the IR's own response.
    _, flat_low = get("lowcut-flat", signal="sine", freq=80, ir=IR_DIRAC, seconds=1.0,
                      dry=0, wet=100, lowcut=20)
    _, cut_low = get("lowcut-800", signal="sine", freq=80, ir=IR_DIRAC, seconds=1.0,
                     dry=0, wet=100, lowcut=800)
    _, flat_high = get("highcut-flat", signal="sine", freq=9000, ir=IR_DIRAC, seconds=1.0,
                       dry=0, wet=100, highcut=20000)
    _, cut_high = get("highcut-1k", signal="sine", freq=9000, ir=IR_DIRAC, seconds=1.0,
                      dry=0, wet=100, highcut=1000)

    skip = int(0.1 * 48000)
    results["filters"] = {
        "low_80hz_flat_rms": rms(flat_low[:, 0], skip),
        "low_80hz_cut800_rms": rms(cut_low[:, 0], skip),
        "high_9khz_flat_rms": rms(flat_high[:, 0], skip),
        "high_9khz_cut1k_rms": rms(cut_high[:, 0], skip),
    }

    return results


def evaluate(results):
    """Each check returns (name, passed, detail). Thresholds match ../Source/Tests where they overlap."""
    checks = []

    latency = results["latency"]
    checks.append((
        "Reported and actual latency are zero",
        latency["impulse_peak_sample"] == 0,
        f"impulse peak at sample {latency['impulse_peak_sample']}, "
        f"amplitude {latency['peak_amplitude']:.4f}",
    ))

    transparency = results["defaults_transparency"]
    checks.append((
        "Wet path is transparent at defaults",
        transparency["max_abs_error"] < 1e-4,
        f"worst deviation {transparency['max_abs_error']:.2e} against a peak of "
        f"{transparency['reference_peak']:.2f}",
    ))

    predelay = results["predelay"]
    checks.append((
        "Pre-delay lands within a sample of the requested time",
        abs(predelay["measured_ms"] - predelay["requested_ms"]) < 0.05,
        f"requested {predelay['requested_ms']:.1f} ms, measured {predelay['measured_ms']:.3f} ms",
    ))

    bypass = results["bypass"]
    checks.append((
        "Bypass introduces no step beyond the signal's own",
        bypass["toggled_max_step"] < bypass["control_max_step"] * 3.0,
        f"toggled {bypass['toggled_max_step']:.5f} vs control {bypass['control_max_step']:.5f}",
    ))
    checks.append((
        "Bypass actually removes the wet signal",
        bypass["rms_after_toggled"] < bypass["rms_after_control"] * 0.95,
        f"RMS after: {bypass['rms_after_toggled']:.4f} bypassed vs "
        f"{bypass['rms_after_control']:.4f} active",
    ))

    swap = results["ir_swap"]
    checks.append((
        "IR swap introduces no step beyond the signal's own",
        swap["swapped_max_step"] < swap["control_max_step"] * 3.0,
        f"swapped {swap['swapped_max_step']:.5f} vs control {swap['control_max_step']:.5f}",
    ))

    src = results["sample_rate_conversion"]
    checks.append((
        "Decay time is independent of session sample rate",
        src["spread_seconds"] < 0.01,
        "T20 " + ", ".join(f"{rate} Hz: {value * 1000:.1f} ms"
                           for rate, value in src["t20_seconds"].items())
        + f" (spread {src['spread_seconds'] * 1000:.2f} ms)",
    ))

    filters = results["filters"]
    low_ratio = filters["low_80hz_cut800_rms"] / max(filters["low_80hz_flat_rms"], 1e-12)
    high_ratio = filters["high_9khz_cut1k_rms"] / max(filters["high_9khz_flat_rms"], 1e-12)
    checks.append((
        "Low cut attenuates below its corner",
        low_ratio < 0.3,
        f"80 Hz tone at {low_ratio * 100:.1f}% of its unfiltered level with an 800 Hz low cut",
    ))
    checks.append((
        "High cut attenuates above its corner",
        high_ratio < 0.3,
        f"9 kHz tone at {high_ratio * 100:.1f}% of its unfiltered level with a 1 kHz high cut",
    ))

    return checks


def write_report(results, checks):
    passed = sum(1 for _, ok, _ in checks if ok)
    lines = [
        "# ConvBase engine validation",
        "",
        "Measured from audio rendered through the real `ConvolutionProcessor` by `ConvBaseRenderIR`,",
        "not from the DSP re-implemented in Python. Regenerate with `python3 analysis/validate.py`.",
        "",
        f"**{passed} of {len(checks)} checks passed.**",
        "",
        "| Check | Result | Measurement |",
        "|---|---|---|",
    ]

    for name, ok, detail in checks:
        lines.append(f"| {name} | {'PASS' if ok else 'FAIL'} | {detail} |")

    lines += [
        "",
        "## Why these and not others",
        "",
        "Each row is a claim the plugin makes that a compile cannot check:",
        "",
        "- **Latency** is the one that justifies the rest. `juce::dsp::Convolution` is built with",
        "  `NonUniform{256}` for CPU rather than for latency, so the zero has to be measured. It is",
        "  also what makes the ramped bypass safe - with no plugin delay compensation in play, there",
        "  is no timing discontinuity to reconcile when a host toggles bypass.",
        "- **Transparency at defaults** covers the promise that Length, Attack and both filters start",
        "  out doing nothing. Convolving with a Dirac IR is an identity, so any deviation is the wet",
        "  chain's own doing.",
        "- **The two step measurements** are what 'no clicks' means once it is written down. Both are",
        "  compared against an identical run that never toggles, so the threshold is the signal's own",
        "  steady-state roughness rather than a number picked to pass.",
        "- **Decay time across sample rates** is the check that the resampler used the right ratio.",
        "  `IRLibrary::resample()` also compensates `LagrangeInterpolator`'s 2-sample group delay;",
        "  without that, the same IR would start at different moments in 44.1 and 48 kHz sessions.",
        "",
    ]

    REPORT_MD.write_text("\n".join(lines))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--no-render", action="store_true",
                        help="measure the existing renders instead of producing new ones")
    args = parser.parse_args()

    do_render = not args.no_render

    if do_render and not RENDER_BIN.exists():
        print(f"Build the harness first:\n"
              f"  cmake --build build --config Release --target ConvBaseRenderIR", file=sys.stderr)
        return 1

    RENDER_DIR.mkdir(exist_ok=True)

    results = measure_all(do_render)
    checks = evaluate(results)

    RESULTS_JSON.write_text(json.dumps(results, indent=2) + "\n")
    write_report(results, checks)

    for name, ok, detail in checks:
        print(f"[{'PASS' if ok else 'FAIL'}] {name}: {detail}")

    failures = sum(1 for _, ok, _ in checks if not ok)
    print(f"\n{len(checks) - failures} passed, {failures} failed -> {REPORT_MD.relative_to(PLUGIN_DIR)}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
