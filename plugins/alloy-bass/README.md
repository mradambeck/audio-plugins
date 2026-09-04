# Alloy

A stacked mono synth (AU / VST3 / Standalone) combining an analog-style oscillator with unison
and a sub-oscillator, an FM voice (carrier + modulator), and a built-in arpeggiator — voiced for
bass but usable for any monophonic patch.

See the [root README](../README.md) for shared build requirements, the exFAT/apostrophe build
gotchas, and running tests across all plugins at once.

## Building

```sh
cd alloy-bass
cmake -B build -G Xcode
cmake --build build --config Release --target Alloy_All
```

To build a single format only: `--target Alloy_AU`, `Alloy_VST3`, or `Alloy_Standalone`.

## Installation

`COPY_PLUGIN_AFTER_BUILD` is enabled, so a successful build automatically copies the plugin into
the standard user plugin directories:

- **AU:** `~/Library/Audio/Plug-Ins/Components/Alloy.component`
- **VST3:** `~/Library/Audio/Plug-Ins/VST3/Alloy.vst3`

Restart your DAW (or run AU validation, below) after installing. The Standalone app is built to
`build/Alloy_artefacts/Release/Standalone/Alloy.app`.

## Launching the Standalone app

The fastest edit/listen loop during development — no DAW rescan needed:

```sh
cmake --build build --config Debug --target Alloy_Standalone
open build/Alloy_artefacts/Debug/Standalone/Alloy.app
```

## Validating the AU (auval)

```sh
auval -v aumu Aloy WJag
```

`Aloy` is the plugin code, `WJag` the manufacturer code (both from `CMakeLists.txt`). Note the AU
type is `aumu` (music device/synth), not `aufx` — Alloy is `IS_SYNTH TRUE`. A pass prints
`AU VALIDATION SUCCEEDED`.

## Offline validation workflow

For verifying a DSP-internals change (e.g. a performance optimization) doesn't alter the sound,
on top of the usual `AlloyTests` unit-test target:

1. **`AlloyRenderIR`** (a console app, not a plugin format) - feeds a scripted MIDI note sequence
   through the real `AlloyAudioProcessor` and writes the result to WAV (Alloy is a synth, not an
   effect, so there's no audio input to process - output comes entirely from MIDI):

   ```sh
   cmake --build build --config Release --target AlloyRenderIR
   build/AlloyRenderIR_artefacts/Release/AlloyRenderIR --out rendered-audio/mine.wav --seconds 6
   ```

   Flags map 1:1 onto the plugin's own parameter IDs in their native units, plus `--preset "<name>"`
   to apply one of the 8 factory presets first (see the tool's own header comment for the full
   list). `ageNoiseRandom` (the Age knob's drift/warble source) is normally seeded from system
   entropy - pass `--ageSeed <n>` to pin it for reproducible renders when Age > 0.
2. Render the same MIDI sequence/parameter matrix before and after a change, and `cmp` each pair
   byte-for-byte - expect zero differences for anything claimed to not alter the sound.
   `../common/tools/compare_wavs.py` (shared across the catalog) is available for a fuzzier
   spectral/envelope comparison when an exact diff isn't the right tool - e.g. the arpeggiator's
   Random pattern uses the process-global `juce::Random::getSystemRandom()` with no per-instance
   seed available, so it isn't byte-reproducible across renders by design.

## How it works

Alloy layers three sound sources into one mono voice: an **Analog** oscillator (selectable
waveform, octave, unison voices/detune, resonant filter with its own ADSR and velocity-to-filter
amount, plus an amp ADSR) with an optional **Sub** oscillator underneath it; and an **FM** voice
(carrier + modulator oscillators, each with its own octave/ADSR, modulator Brightness controlling
FM index). The Analog and FM pages are shown side by side; a third **Sub/Arp/Mix** page (behind
the header button) holds the sub-oscillator's own controls, a tempo-syncable arpeggiator
(pattern, octave range, rate/division, hold), and the final level balance between the three
sources. **Panic** force-stops all voices if a note gets stuck.

## Parameters

Grouped by section — see the running app for exact per-knob ranges (hover any knob's value
readout), since there are ~40 individual parameters across the three pages.

| Section | Controls |
|---|---|
| Analog | Waveform, Octave, Unison (voice count + detune), Cutoff, Resonance, Env Amount, Vel>Filter, filter/amp ADSR, Glide, Volume |
| FM Carrier | Waveform, Octave, Volume, Vel>Carrier, ADSR |
| FM Modulator | Waveform, Octave, Volume, Vel>Bright, Brightness (FM index), ADSR |
| Sub / Arp / Mix (second page) | Sub oscillator waveform/octave/level; arpeggiator on/off, pattern, octave range, sync/rate, hold; Analog/Sub/FM level balance |

## Project structure

```
alloy-bass/
├── CMakeLists.txt
├── Source/
│   ├── PluginProcessor.h/.cpp    # DSP + parameter state
│   ├── PluginEditor.h/.cpp       # UI layout (2-page: Analog+FM, Sub/Arp/Mix)
│   ├── AlloyLookAndFeel.h/.cpp   # Thin subclass of the shared HardwarePanelLookAndFeel
│   ├── Tests/                    # AlloyTests: headless UnitTest console app
│   └── Tools/RenderIR.cpp        # AlloyRenderIR: offline MIDI-driven render console app
├── rendered-audio/               # AlloyRenderIR output (gitignored, regenerable)
└── installer/                    # This plugin's .pkg installer (see installers/README.md)
```

## License

[AGPLv3](../LICENSE), same as the rest of this repo.
