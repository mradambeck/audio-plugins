# Corrosion

A tanh-based overdrive/distortion plugin (AU / VST3 / Standalone) with an asymmetric bias stage
for even-harmonic warmth and an optional rectifier stage for a more aggressive, octave-up-flavored
character.

See the [root README](../README.md) for shared build requirements, the exFAT/apostrophe build
gotchas, and running tests across all plugins at once.

## Building

```sh
cd corrosion-drive
cmake -B build -G Xcode
cmake --build build --config Release --target Corrosion_All
```

To build a single format only: `--target Corrosion_AU`, `Corrosion_VST3`, or `Corrosion_Standalone`.

## Installation

`COPY_PLUGIN_AFTER_BUILD` is enabled, so a successful build automatically copies the plugin into
the standard user plugin directories:

- **AU:** `~/Library/Audio/Plug-Ins/Components/Corrosion.component`
- **VST3:** `~/Library/Audio/Plug-Ins/VST3/Corrosion.vst3`

Restart your DAW (or run AU validation, below) after installing. The Standalone app is built to
`build/Corrosion_artefacts/Release/Standalone/Corrosion.app`.

## Launching the Standalone app

The fastest edit/listen loop during development — no DAW rescan needed:

```sh
cmake --build build --config Debug --target Corrosion_Standalone
open build/Corrosion_artefacts/Debug/Standalone/Corrosion.app
```

## Validating the AU (auval)

```sh
auval -v aufx Corr WJag
```

`Corr` is the plugin code, `WJag` the manufacturer code (both from `CMakeLists.txt`). A pass
prints `AU VALIDATION SUCCEEDED`.

## How it works

Input runs through: an asymmetric-bias tanh waveshaper (Drive controls the gain into the shaper;
Bias skews the transfer curve for 2nd-harmonic warmth; Character blends between a soft and a
harder-kneed tanh curve), then a Color low-pass with a complementary presence/dip EQ, then an
optional half/full-wave rectifier stage blended in afterward, then a Dry/Comp/Wet output mix
(Comp compresses only the dry path's parallel copy, not the driven signal).

## Parameters

| Parameter | Range | Description |
|---|---|---|
| Drive | 1x – 75x | Gain applied before waveshaping |
| Bias | -1 – +1 | Asymmetric offset for even-harmonic (tube-like) warmth |
| Character | 0 – 100% | Blends from a soft tanh curve toward a harder-kneed variant |
| Color | 200 Hz – 20 kHz | Low-pass cutoff, with a complementary presence/dip EQ tied to the same knob |
| Half/Full | 0 – 100% | Blends the rectifier stage from half-wave toward full-wave rectification |
| Blend | 0 – 100% | How much of the rectified signal mixes back in (0 = rectifier inaudible) |
| Dry | -100 – 0 dB | Dry signal level |
| Comp | 0 – 100% | Compression applied to the dry path only |
| Wet | -100 – +12 dB | Processed signal level |

## Project structure

```
corrosion-drive/
├── CMakeLists.txt
├── Source/
│   ├── PluginProcessor.h/.cpp        # DSP + parameter state
│   ├── PluginEditor.h/.cpp           # UI layout
│   └── CorrosionLookAndFeel.h/.cpp   # Thin subclass of the shared HardwarePanelLookAndFeel
└── installer/                        # This plugin's .pkg installer (see installers/README.md)
```

## License

[AGPLv3](../LICENSE), same as the rest of this repo.
