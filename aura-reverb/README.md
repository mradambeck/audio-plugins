# Aura

*Ambience Reverb.* (Highlight colour: `#DCAC52`.)

An algorithmic reverb (AU / VST3 / Standalone) emulating the AMS RMX16 "Ambience" program - the
first module built from `ml-toolkit/` (a reusable pipeline, at the repo root, that turns captured
hardware audio into fitted real-time DSP parameters via a differentiable PyTorch fitting harness;
see [`../AGENTS.md`](../AGENTS.md)'s ML toolkit section and `ml-toolkit/effects/ambience/findings.md`).
Built and calibrated from 65 real hardware captures (`ml-toolkit/effects/ambience/captures/`) -
the Python fit established the model shape and an initial parameter set, but several of the
shipped values are directly hand-calibrated against the real captures instead (see "How it works"
below for why, and `AuraDecayGainData.h`/`AuraOnsetTiltData.h`/`AuraSubBassGainData.h` for the
specific tables). Full validation against all 65 captures: ~0.94 envelope correlation, ~2.2dB
log-spectral distance, low/mid/high band balance within ~1dB (Low=0 subset;
`analysis/validation_report.md` has the current numbers and per-setting breakdown).

See the [root README](../README.md) for shared build requirements, the exFAT/apostrophe build
gotchas, and running tests across all plugins at once.

## Building

```sh
cd aura-reverb
cmake -B build -G Xcode
cmake --build build --config Release --target Aura_All
```

To build a single format only: `--target Aura_AU`, `Aura_VST3`, or `Aura_Standalone`.

## Installation

`COPY_PLUGIN_AFTER_BUILD` is enabled, so a successful build automatically copies the plugin into
the standard user plugin directories:

- **AU:** `~/Library/Audio/Plug-Ins/Components/Aura - Reverb.component`
- **VST3:** `~/Library/Audio/Plug-Ins/VST3/Aura - Reverb.vst3`

Restart your DAW (or run AU validation, below) after installing. The Standalone app is built to
`build/Aura_artefacts/Release/Standalone/Aura - Reverb.app`.

A double-clickable `.pkg` installer also exists (`installer/`, or the combined
`../installers/WildJagPlugins-Installer.pkg`) - see `../installers/README.md`.

## Launching the Standalone app

```sh
cmake --build build --config Debug --target Aura_Standalone
open build/Aura_artefacts/Debug/Standalone/*.app
```

(The product name has spaces in it - `Aura - Reverb.app` - so if you're typing the path out by
hand rather than globbing it, it needs to stay quoted as one argument:
`open "build/Aura_artefacts/Debug/Standalone/Aura - Reverb.app"`.)

## Validating the AU (auval)

```sh
auval -v aufx Aura WJag
```

## Offline analysis & validation workflow

This plugin's DSP is derived and tuned against real hardware captures rather than by ear alone,
split across two places:

1. **`ml-toolkit/effects/ambience/`** (repo root, not plugin-local - shared toolkit infrastructure)
   - the Python side: `captures/` (the 65 ground-truth WAVs), `analyze.py`/`findings.md` (Phase 1-2
   feature extraction and hand-review), `model.py`/`fit_ambience.py` (the differentiable FDN fit),
   `cross_validate.py`, `export_params.py` (produces `AuraReferenceData.h`). Has its own venv - see
   [`../AGENTS.md`](../AGENTS.md)'s ML toolkit section.
2. **This repo's own `analysis/`** (Phase D, mirrors intruder-gated-reverb's pattern) - renders the
   real plugin at all 65 reference settings and scores it on more than amplitude: envelope
   correlation, log-spectral distance/EQ balance, crest factor, spectral flatness (all via
   `../common/tools/compare_wavs.py`). Its own venv:
   `python3 -m venv analysis/venv && source analysis/venv/bin/activate && pip install -r ../common/tools/requirements.txt`.

   ```sh
   cmake --build build --config Release --target AuraRenderIR
   python3 analysis/validate.py
   ```

   Writes `analysis/validation_report.md` and per-setting plots under `analysis/validation_plots/`.
   Rerun after any DSP change - that's the point of having it as a script, not a one-off check.
   `AuraRenderIR`'s flags map 1:1 onto the plugin's own parameter IDs in their native units, plus
   `--preset "<name>"` for the 7 factory presets (`PluginProcessor.cpp`'s `getFactoryPresets()`).
3. **`AuraCalibrateProbe`** (a third console app, calibration-only, not part of the shipped
   plugin) - drives `AuraFDNEngine` directly at raw `decayGain`/`dampingWeight`/`subBassGain`,
   bypassing `AuraParameterMap`'s Time/High curves entirely, so the hand-calibrated tables below
   have something to binary-search against:

   ```sh
   cmake --build build --config Release --target AuraCalibrateProbe
   build/AuraCalibrateProbe_artefacts/Release/AuraCalibrateProbe --out /tmp/probe.wav --decayGain 0.9 --dampingWeight 0.9 --subBassGain 1.0
   ```

## How it works

The core is `AuraFDNEngine`: an 8-line Hadamard-mixed FDN, deliberately simpler than
`IntruderFDNEngine`/`ShieldsFDNEngine` (no input diffuser, no envelope/gate stage, no delay-line
modulation) - `findings.md` found Ambience needs none of those, unlike Intruder's Non-Lin 2 on the
same hardware unit.

**Several DSP-ready values are hand-calibrated against the real captures, not the Python fit's own
output**, found empirically over the course of Phase D validation:

- **`AuraDecayGainData.h`** - a single shared feedback gain (not independent low/high band gains -
  those looked reasonable in isolation but produced a ~2x low/high decay-time disparity once
  rendered, where the real hardware's own bands are within ~1.3% of each other) plus per-line
  damping weight, binary-searched per Decay setting to reproduce the real measured decay time and
  tonal balance directly, superseding the automated fit's own values for these two parameters.
- **`AuraOnsetTiltData.h`** - Color's onset-tone effect, applied at an *input-stage* `TiltFilter`,
  not the in-loop feedback shelf: an early version's in-loop-only tilt produced measurably ZERO
  onset-tone difference between Color settings, since the shelf only touches signal after a
  feedback round trip while the dry input is injected unshelved on its first pass.
- **A fixed 70Hz input high-pass** (`inputHighPassL`/`R`, outside the feedback loop) models a
  measured sub-bass level excess in the real captures - fixes LEVEL but, being outside the loop,
  structurally cannot fix DECAY RATE.
- **`AuraSubBassGainData.h`** - a second in-loop shelf (`subBassShelf`, own 120Hz pivot,
  independent of the main feedback shelf's 1kHz one) that DOES address decay rate: an in-loop,
  per-round-trip attenuation below 120Hz, calibrated per Decay setting to match each real
  capture's own low-vs-mid decay-rate relationship (not simply "flatten it" - the real hardware's
  own relationship is small and sign-inconsistent across most settings, with a genuine large
  outlier at the shortest setting). Two earlier attempts at this same problem (uniformly doubling
  the line count, then an asymmetric mix of original + new extra-damped long lines) were tried and
  rejected - the first hurt short-Decay crest factor, the second's longer delay lines imposed a
  hard floor on achievable decay time no gain/damping tuning could remove. This in-loop-gain
  approach doesn't add any new delay path, so it can't hit that same floor, and was verified not to
  touch anything above 500Hz or the reverb's crest factor/density character at all.
- **Low Cut** (0-300Hz) is NOT derived from the hardware at all - the real unit's own "Low" knob
  was measured to have no onset-tone effect and no clear decay effect, so it was repurposed
  outright into a plain input-stage utility high-pass, applied before pre-delay and the whole
  effect chain.
- **Converter** (8/16/24 bit, default 24 - a genuine bypass, see below - underlying param ID still
  `bitDepth`) is a discrete 3-position output quantization selector - a small hand-painted slide
  switch (see "The UI" below) modelled directly on a cropped reference photo of the Roland RE-501
  Chorus Echo's own "LEVEL" switch, the hardware this whole panel language is based on. Originally
  a continuous 8-24 knob defaulting to 16, chosen because it models the real unit's measured ~90dB
  dynamic-range ceiling (found by fitting a decay-time model to the real captures' own tails, which
  bottom out at almost exactly -90dB relative to peak, matching what a real 16-bit-class converter
  delivers in practice - not exactly the theoretical 16-bit limit). Rebuilt as a 3-choice switch
  (Adam, 2026-09-03), briefly defaulting to 8 bit for a heavier, deliberately more colored startup
  sound, then to 24 bit (bypassed) once every parameter's default was set to match the "Default"
  factory preset exactly (Adam, 2026-09-03 - see "Parameters" below and getFactoryPresets()'s own
  comment in `PluginProcessor.cpp`). 24 bit is a genuine bypass (`AuraFDNEngine::bitDepthActive`),
  same contract as the original knob - quantization is only actually audible at 8/16 bit, applied
  undithered at the output stage only (wet path, after the tank sum). Its dominant audible effect
  at 8-bit is not broadband grit but the decay tail truncating to hard digital silence well before
  it would naturally finish decaying, once the signal drops below half the quantization step
  (`std::round` then snaps permanently to exact zero).

The UI is the full hardware-panel treatment (see the `juce-hardware-panel-ui` skill; accent colour
`#DCAC52`), built from the approved mockup at `mockups/aura-mockup-v1.html`. Three sections - Tone
(Low Cut + Converter's slide switch side by side, top row; Color hero knob, own row below - the
original 2-row shape, Color staying hero-sized since it's solo rather than sharing a row), Timing
(Pre-Delay + Decay hero knob, stacked), Mix (Wet/Dry as two independent, full-height vertical
faders - not knobs, and not a single crossfading Blend control). `AuraLookAndFeel` is a thin
subclass of the shared `wildjag::HardwarePanelLookAndFeel` supplying Aura's accent pair and the two
embedded fonts (shared Oxanium/Oswald from `common/Assets/`, no plugin-specific typeface). The
Converter switch itself (`AuraEditorContent::ConverterSwitch`) is a small hand-painted component
local to Aura, not part of the shared LookAndFeel - no other plugin in the catalog uses this
control shape yet.

## Parameters

Every default below matches the "Default" factory preset exactly (Adam, 2026-09-03) - a freshly
instantiated plugin sounds identical to selecting "Default" from the preset menu, see "How it
works" below.

| Parameter | Range | Default | Description |
|---|---|---|---|
| Decay | 0.1 - 8.0 s | 1.31 s | Tail length, close to literal seconds (unlike Intruder's compressed Decay label on the same hardware unit) |
| Low Cut | 0 - 300 Hz | 10 Hz | Input-stage utility high-pass, before pre-delay/the effect - NOT derived from the hardware, see "How it works" |
| Color | -8 - 0 dB | 0 dB | Broadband bass/treble tilt (the hardware's own control) that also shortens decay as it goes negative |
| Pre-Delay | 0 - 200 ms | 32 ms | Delay before the reverb signal starts |
| Converter | 8 / 16 / 24 bit | 24 bit | Output quantization depth, a small 3-position slide switch; 24 is a genuine bypass, see "How it works" |
| Dry | 0 - 100% | 0% | Dry signal gain |
| Wet | 0 - 200% | 100% | Wet (processed) signal gain - independent of Dry, can exceed unity |
| Bypass | on/off | off | |

## Project structure

```
aura-reverb/
├── CMakeLists.txt
├── Source/
│   ├── PluginProcessor.h/.cpp        # Parameter state, dry/wet mix, buffer plumbing
│   ├── AuraFDNEngine.h/.cpp          # The reverb core (see "How it works" above)
│   ├── AuraParameterMap.h/.cpp       # Maps UI values (Decay, Color) to DSP coefficients via the
│   │                                 # measured/calibrated reference curves - kept separate from
│   │                                 # the DSP core so the mapping can be refit without touching it
│   ├── AuraReferenceData.h           # The Python fit's own output - only its damping High-offset
│   │                                 # curve is still used; decayGain/dampingWeight/onset-tilt are
│   │                                 # all superseded by the hand-calibrated tables below
│   ├── AuraDecayGainData.h           # Hand-calibrated decayGain/dampingWeight per Decay setting
│   ├── AuraOnsetTiltData.h           # Hand-calibrated Color onset-tilt curve
│   ├── AuraSubBassGainData.h         # Hand-calibrated sub-bass decay-rate brake per Decay setting
│   ├── AuraLookAndFeel.h/.cpp        # Thin subclass of the shared HardwarePanelLookAndFeel
│   ├── PluginEditor.h/.cpp           # Hardware-panel UI (see mockups/aura-mockup-v1.html)
│   ├── Tests/                        # AuraTests: headless UnitTest console app (DSP + mapping)
│   └── Tools/
│       ├── RenderIR.cpp              # AuraRenderIR: offline render console app
│       └── CalibrateProbe.cpp        # AuraCalibrateProbe: raw-coefficient calibration probe
├── mockups/aura-mockup-v1.html       # Approved HTML/CSS mockup the real UI was built from
├── analysis/                         # Phase D validation pipeline (see above) - its own venv,
│                                      # gitignored
└── installer/                        # This plugin's .pkg installer (see ../installers/README.md)
```

The ground-truth captures and the Python fitting pipeline live in `ml-toolkit/effects/ambience/`
at the repo root, not here - see that folder and its own `findings.md` for the full analysis.

Offline IR comparison/scoring script: `../common/tools/compare_wavs.py` (shared across the catalog,
not plugin-local).

## License

[AGPLv3](../LICENSE), same as the rest of this repo.
