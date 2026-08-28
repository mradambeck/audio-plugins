# Flux

An analog-style phase shifter plugin (AU / VST3 / Standalone) with a switchable stage count (2–36)
and a Color section for shaping the phased signal before it's blended back with the dry signal.

See the [root README](../README.md) for shared build requirements, the exFAT/apostrophe build
gotchas, and running tests across all plugins at once.

## Building

```sh
cd flux-phaser
cmake -B build -G Xcode
cmake --build build --config Release --target Flux_All
```

To build a single format only: `--target Flux_AU`, `Flux_VST3`, or `Flux_Standalone`.

## Installation

`COPY_PLUGIN_AFTER_BUILD` is enabled, so a successful build automatically copies the plugin into
the standard user plugin directories:

- **AU:** `~/Library/Audio/Plug-Ins/Components/Flux.component`
- **VST3:** `~/Library/Audio/Plug-Ins/VST3/Flux.vst3`

Restart your DAW (or run AU validation, below) after installing. The Standalone app is built to
`build/Flux_artefacts/Release/Standalone/Flux.app`.

## Launching the Standalone app

The fastest edit/listen loop during development — no DAW rescan needed:

```sh
cmake --build build --config Debug --target Flux_Standalone
open build/Flux_artefacts/Debug/Standalone/Flux.app
```

## Validating the AU (auval)

```sh
auval -v aufx Flux WJag
```

`Flux` is the plugin code, `WJag` the manufacturer code (both from `CMakeLists.txt`). A pass
prints `AU VALIDATION SUCCEEDED`.

## Offline validation workflow

For verifying a DSP-internals change (e.g. a performance optimization) doesn't alter the sound,
on top of the usual `FluxTests` unit-test target:

1. **`FluxRenderIR`** (a console app, not a plugin format) - feeds a synthesized audio-in test
   signal (two-tone sine, sweep, noise, silence tail) through the real `FluxAudioProcessor` and
   writes the result to WAV:

   ```sh
   cmake --build build --config Release --target FluxRenderIR
   build/FluxRenderIR_artefacts/Release/FluxRenderIR --out rendered-audio/mine.wav --seconds 6
   ```

   Flags map 1:1 onto the plugin's own parameter IDs in their native units, plus `--preset "<name>"`
   for the 6 factory presets. Two extra capabilities exist specifically for exercising the Grit/
   Brightness filter-coefficient caches (see `PluginProcessor.h`'s `lastGritAmount`/
   `lastBrightnessAmount`): `--changeParamID/--changeParamValue/--changeAtSecond` applies one
   parameter change mid-render, and `--sampleRate2/--warmupSeconds` renders in two phases across a
   `prepareToPlay()` call at a different sample rate - see the tool's own header comment for both.
2. Render the same signal/parameter matrix before and after a change, and `cmp` each pair
   byte-for-byte - expect zero differences for anything claimed to not alter the sound.
   `../common/tools/compare_wavs.py` (shared across the catalog) is available for a fuzzier
   spectral/envelope comparison when an exact diff isn't the right tool.

## How it works

An LFO (Rate, or tempo-Synced to a note division; Depth; Shape) sweeps a chain of allpass filter
stages (switchable 2–36) to produce the phasing sweep; Offset shifts the sweep's center point and
Feedback resonance-boosts the notches for a more pronounced effect. Brightness and Grit shape the
phased signal's tone before Blend mixes it back with the dry signal.

## Parameters

| Parameter | Range | Description |
|---|---|---|
| Sync | on/off | Locks Rate to a tempo division instead of a free Hz value |
| Rate | 0.02 – 10 Hz | LFO speed (free-running, when Sync is off) |
| Division | note values | LFO speed as a tempo-synced note division (when Sync is on) |
| Depth | 0 – 100% | LFO sweep depth |
| Shape | 0 – 100% | LFO waveform shape |
| Stages | 2 / 4 / 6 / 8 / 12 / 24 / 36 | Number of allpass filter stages in the phaser chain |
| Offset | -100 – +100% | Shifts the sweep's center point |
| Feedback | 0 – 100% | Resonance feedback around the filter chain |
| Brightness | 0 – 100% | Tone shaping on the phased signal |
| Grit | 0 – 100% | Added texture/saturation on the phased signal |
| Blend | 0 – 100% | Dry/phased mix |

## Project structure

```
flux-phaser/
├── CMakeLists.txt
├── Source/
│   ├── PluginProcessor.h/.cpp    # DSP + parameter state
│   ├── PluginEditor.h/.cpp       # UI layout
│   ├── FluxLookAndFeel.h/.cpp    # Thin subclass of the shared HardwarePanelLookAndFeel
│   ├── Tests/                     # FluxTests: headless UnitTest console app
│   └── Tools/RenderIR.cpp         # FluxRenderIR: offline audio-in render console app
├── rendered-audio/                # FluxRenderIR output (gitignored, regenerable)
└── installer/                     # This plugin's .pkg installer (see installers/README.md)
```

## License

[AGPLv3](../LICENSE), same as the rest of this repo.
