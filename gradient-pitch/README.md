# Gradient

A pitch-shifting delay plugin (AU / VST3 / Standalone) with two independent pitch/delay units (A
and B) that can run in Dual mode, Linked (B follows A by a fixed interval), or Cross-feedback (A
and B feed into each other).

See the [root README](../README.md) for shared build requirements, the exFAT/apostrophe build
gotchas, and running tests across all plugins at once.

## Building

```sh
cd gradient-pitch
cmake -B build -G Xcode
cmake --build build --config Release --target Gradient_All
```

To build a single format only: `--target Gradient_AU`, `Gradient_VST3`, or `Gradient_Standalone`.

## Installation

`COPY_PLUGIN_AFTER_BUILD` is enabled, so a successful build automatically copies the plugin into
the standard user plugin directories:

- **AU:** `~/Library/Audio/Plug-Ins/Components/Gradient.component`
- **VST3:** `~/Library/Audio/Plug-Ins/VST3/Gradient.vst3`

Restart your DAW (or run AU validation, below) after installing. The Standalone app is built to
`build/Gradient_artefacts/Release/Standalone/Gradient.app`.

## Launching the Standalone app

The fastest edit/listen loop during development — no DAW rescan needed:

```sh
cmake --build build --config Debug --target Gradient_Standalone
open build/Gradient_artefacts/Debug/Standalone/Gradient.app
```

## Validating the AU (auval)

```sh
auval -v aufx Grad WJag
```

`Grad` is the plugin code, `WJag` the manufacturer code (both from `CMakeLists.txt`). A pass
prints `AU VALIDATION SUCCEEDED`.

## Offline validation workflow

For verifying a DSP-internals change (e.g. a performance optimization) doesn't alter the sound,
on top of the usual `GradientTests` unit-test target:

1. **`GradientRenderIR`** (a console app, not a plugin format) - feeds a synthesized, deterministic
   test signal (two-tone sine, sweep, noise, silence tail - see `Source/Tools/RenderIR.cpp`) through
   the real `GradientAudioProcessor` and writes the result to WAV:

   ```sh
   cmake --build build --config Release --target GradientRenderIR
   build/GradientRenderIR_artefacts/Release/GradientRenderIR --out rendered-audio/mine.wav --seconds 5
   ```

   Flags map 1:1 onto the plugin's own parameter IDs in their native units (e.g.
   `--pitchSemitonesA 12 --spliceModeA smart --feedbackPercentA 80`) - see the tool's own header
   comment for the full list. Unlike a plain run, Drift's RNG is normally seeded from each engine
   instance's own memory address (varies per process under ASLR) - pass `--driftSeed <n>` to pin it
   for reproducible renders when Drift > 0.
2. Render the same parameter matrix before and after a change, and `cmp` each pair byte-for-byte -
   expect zero differences for anything claimed to not alter the sound. `../common/tools/compare_wavs.py`
   (shared across the catalog) is available for a fuzzier spectral/envelope comparison when an exact
   diff isn't the right tool (e.g. comparing against a different signal source entirely).

## How it works

Each of the two units (A/B) independently pitch-shifts and delays its input, using a splicing
pitch-shift engine (Splice Mode: Normal/Soft/Smart trades off latency, artifacts, and pitch
accuracy; Crossfade Length tunes the splice's grain overlap). Mode selects how A and B combine:
**Dual Mode** runs both as separate voices; **Link** locks B's pitch/delay to A plus a fixed
offset (Link Pitch/Link Delay); **Cross-feedback** routes each unit's output into the other's
feedback input for evolving, self-modulating textures. Drift adds slow pitch/time wander to either
unit; Stereo widens the combined output.

## Parameters

| Parameter (per unit, A and B) | Range | Description |
|---|---|---|
| Pitch | -24 – +24 st | Pitch shift amount |
| Fine | -50 – +50 ct | Fine pitch trim |
| Delay | 0 – 1000 ms | Delay time (free-running, when Delay Sync is off) |
| Delay Sync | on/off | Locks Delay to a tempo-synced note division instead |
| Feedback | 0 – 350% | Regeneration amount (self-oscillates at the high end) |
| Splice Mode | Normal / Soft / Smart | Pitch-shift splicing algorithm |
| Crossfade Length | 1 – 14 ms | Splice grain overlap length |
| Drift | 0 – 100% | Slow pitch/time wander |
| Mix | 0 – 100% | This unit's dry/processed balance |
| Output | -24 – +12 dB | This unit's output trim |

| Global parameter | Description |
|---|---|
| Dual Mode | Runs A and B as independent voices |
| Link / Link Pitch / Link Delay | Locks B to follow A by a fixed pitch/delay offset |
| Cross-feedback | Routes A and B's outputs into each other's feedback |
| Stereo | Output width |

## Project structure

```
gradient-pitch/
├── CMakeLists.txt
├── Source/
│   ├── PluginProcessor.h/.cpp          # Parameter state, routes to the two units
│   ├── GradientDelayBuffer.h/.cpp      # Standalone, unit-testable delay-line class
│   ├── GradientPitchShiftEngine.h/.cpp # Standalone, unit-testable pitch-shift engine
│   ├── PluginEditor.h/.cpp             # UI layout
│   ├── GradientLookAndFeel.h/.cpp      # Thin subclass of the shared HardwarePanelLookAndFeel
│   ├── Tests/                          # GradientTests: headless UnitTest console app (DSP core only)
│   └── Tools/RenderIR.cpp              # GradientRenderIR: offline render console app
├── rendered-audio/                     # GradientRenderIR output (gitignored, regenerable)
└── installer/                          # This plugin's .pkg installer (see installers/README.md)
```

## License

[AGPLv3](../LICENSE), same as the rest of this repo.
