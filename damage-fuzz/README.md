# Damage

An FM-mangled fuzz/distortion plugin (AU / VST3 / Standalone) — a square-wave/pulse-width fuzz
stage that can be cross-modulated by its own internal oscillator, plus a noise gate keyed off the
input level.

See the [root README](../README.md) for shared build requirements, the exFAT/apostrophe build
gotchas, and running tests across all plugins at once.

## Building

```sh
cd damage-fuzz
cmake -B build -G Xcode
cmake --build build --config Release --target Damage_All
```

To build a single format only: `--target Damage_AU`, `Damage_VST3`, or `Damage_Standalone`.

## Installation

`COPY_PLUGIN_AFTER_BUILD` is enabled, so a successful build automatically copies the plugin into
the standard user plugin directories:

- **AU:** `~/Library/Audio/Plug-Ins/Components/Damage.component`
- **VST3:** `~/Library/Audio/Plug-Ins/VST3/Damage.vst3`

Restart your DAW (or run AU validation, below) after installing. The Standalone app is built to
`build/Damage_artefacts/Release/Standalone/Damage.app`.

## Launching the Standalone app

The fastest edit/listen loop during development — no DAW rescan needed:

```sh
cmake --build build --config Debug --target Damage_Standalone
open build/Damage_artefacts/Debug/Standalone/Damage.app
```

## Validating the AU (auval)

```sh
auval -v aufx Dmge WJag
```

`Dmge` is the plugin code, `WJag` the manufacturer code (both from `CMakeLists.txt`). A pass
prints `AU VALIDATION SUCCEEDED`.

## How it works

Drive pushes the input into a square/pulse-width fuzz stage (Pulse Width shapes the duty cycle);
Boost adds extra gain ahead of that stage. Gate mutes the signal below a threshold, keyed off the
input level, shown live on the Gate knob itself (Slow lengthens its release). When On is engaged,
the fuzz stage's pulse width is cross-modulated by an internal FM oscillator (FM Freq). Hi Pass /
Lo Pass shape the fuzz output before the Dry/Wet mix.

## Parameters

| Parameter | Range | Description |
|---|---|---|
| Drive | 1x – 60x | Gain into the fuzz stage |
| Gate | -80 – 0 dB | Threshold below which the input is muted; the knob also shows the live input level |
| Boost | on/off | Extra gain ahead of the fuzz stage |
| Slow | on/off | Lengthens the gate's release time |
| Hi Pass | 20 Hz – 2 kHz | High-pass filter on the fuzz output |
| Lo Pass | 200 Hz – 20 kHz | Low-pass filter on the fuzz output |
| Pulse Width | 0 – 100% | Duty cycle of the square-wave fuzz stage |
| FM Freq | 0.5 Hz – 5 kHz | Internal oscillator frequency, when On is engaged |
| On | on/off | Enables FM cross-modulation of the fuzz stage's pulse width |
| Dry | 0 – 100% | Dry signal level |
| Wet | 0 – 200% | Processed signal level (can exceed unity) |

## Project structure

```
damage-fuzz/
├── CMakeLists.txt
├── Source/
│   ├── PluginProcessor.h/.cpp      # DSP + parameter state
│   ├── PluginEditor.h/.cpp         # UI layout
│   └── DamageLookAndFeel.h/.cpp    # Thin subclass of the shared HardwarePanelLookAndFeel
└── installer/                      # This plugin's .pkg installer (see installers/README.md)
```

## License

[AGPLv3](../LICENSE), same as the rest of this repo.
