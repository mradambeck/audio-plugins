# Caverns

A bucket-brigade-style stereo delay plugin (AU / VST3 / Standalone) with independent L/R delay
times (or a Linked mode), tape-style degrade/modulation for pitch wobble and lo-fi character, and
tempo sync.

See the [root README](../README.md) for shared build requirements, the exFAT/apostrophe build
gotchas, and running tests across all plugins at once.

## Building

```sh
cd caverns-delay
cmake -B build -G Xcode
cmake --build build --config Release --target Caverns_All
```

To build a single format only: `--target Caverns_AU`, `Caverns_VST3`, or `Caverns_Standalone`.

## Installation

`COPY_PLUGIN_AFTER_BUILD` is enabled, so a successful build automatically copies the plugin into
the standard user plugin directories:

- **AU:** `~/Library/Audio/Plug-Ins/Components/Caverns.component`
- **VST3:** `~/Library/Audio/Plug-Ins/VST3/Caverns.vst3`

Restart your DAW (or run AU validation, below) after installing. The Standalone app is built to
`build/Caverns_artefacts/Release/Standalone/Caverns.app`.

## Launching the Standalone app

The fastest edit/listen loop during development — no DAW rescan needed:

```sh
cmake --build build --config Debug --target Caverns_Standalone
open build/Caverns_artefacts/Debug/Standalone/Caverns.app
```

## Validating the AU (auval)

```sh
auval -v aufx Cavn WJag
```

`Cavn` is the plugin code, `WJag` the manufacturer code (both from `CMakeLists.txt`). A pass
prints `AU VALIDATION SUCCEEDED`.

## Offline validation workflow

For verifying a DSP-internals change (e.g. a performance optimization) doesn't alter the sound,
on top of the usual `CavernsTests` unit-test target:

1. **`CavernsRenderIR`** (a console app, not a plugin format) - feeds a synthesized audio-in test
   signal (two-tone sine, sweep, noise, silence tail) through the real `CavernsAudioProcessor` and
   writes the result to WAV:

   ```sh
   cmake --build build --config Release --target CavernsRenderIR
   build/CavernsRenderIR_artefacts/Release/CavernsRenderIR --out rendered-audio/mine.wav --seconds 6
   ```

   Flags map 1:1 onto the plugin's own parameter IDs in their native units, plus `--preset "<name>"`
   for the 8 factory presets. Two extra capabilities exist specifically for exercising the
   filter-coefficient cache (see `PluginProcessor.cpp`'s `lastLowCutHz`/`lastHighCutHz`/
   `lastDegradeDarkenerHz`): `--changeParamID/--changeParamValue/--changeAtSecond` applies one
   parameter change mid-render, and `--sampleRate2/--warmupSeconds` renders in two phases across a
   `prepareToPlay()` call at a different sample rate - see the tool's own header comment for both.
2. Render the same signal/parameter matrix before and after a change, and `cmp` each pair
   byte-for-byte - expect zero differences for anything claimed to not alter the sound.
   `../common/tools/compare_wavs.py` (shared across the catalog) is available for a fuzzier
   spectral/envelope comparison when an exact diff isn't the right tool.

## How it works

Independent L/R delay lines (free-running in ms, tempo-Synced to a note division, or Linked so R
follows L) feed into a feedback loop; Degrade and Mod Speed/Depth emulate bucket-brigade-delay
tape-style pitch wobble and lo-fi character in that feedback path. Low Cut/High Cut shape the
feedback signal's tone (darkening successive repeats, like real BBD hardware). Dry/Wet faders set
the final mix.

## Parameters

| Parameter | Range | Description |
|---|---|---|
| Sync | on/off | Locks L/R Time to tempo divisions instead of free ms values |
| Link | on/off | Forces R Time to exactly follow L Time |
| L Time / R Time | 1 – 2000 ms | Delay time per channel (free-running, when Sync is off) |
| L/R Division | note values | Delay time as a tempo-synced note division (when Sync is on) |
| Feedback | 0 – 65% | Delay regeneration amount |
| Degrade | 0 – 25% | Bucket-brigade-style lo-fi/wobble character in the feedback path |
| Mod Speed | 0.02 – 8 Hz | Modulation rate for the degrade/wobble effect |
| Mod Depth | 0 – 20 | Modulation depth for the degrade/wobble effect |
| Low Cut | 20 Hz – 2 kHz | High-pass filter on the feedback path |
| High Cut | 1 – 20 kHz | Low-pass filter on the feedback path |
| Dry | 0 – 100% | Dry signal level |
| Wet | 0 – 200% | Delayed signal level (can exceed unity) |

## Project structure

```
caverns-delay/
├── CMakeLists.txt
├── Source/
│   ├── PluginProcessor.h/.cpp      # DSP + parameter state
│   ├── PluginEditor.h/.cpp         # UI layout
│   ├── CavernsLookAndFeel.h/.cpp   # Thin subclass of the shared HardwarePanelLookAndFeel
│   ├── Tests/                      # CavernsTests: headless UnitTest console app
│   └── Tools/RenderIR.cpp          # CavernsRenderIR: offline audio-in render console app
├── rendered-audio/                 # CavernsRenderIR output (gitignored, regenerable)
└── installer/                      # This plugin's .pkg installer (see installers/README.md)
```

## License

[AGPLv3](../LICENSE), same as the rest of this repo.
