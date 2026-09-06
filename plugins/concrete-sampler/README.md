# Concrete

A vintage sampler emulation instrument (AU / VST3 / Standalone) modeling the playback
architectures of twelve classic 1980s hardware samplers - the pitch-shifting mechanism,
quantization scheme, and analog filter behavior of each machine, not just a bitcrusher preset.

**Status: early build-in-progress, not a usable instrument yet.** Phases 0-2 of the 9-phase build
plan are done (scaffold, clean sample playback, and the three pitch-engine modes - variable-clock
zero-order hold, fixed-rate drop-sample decimation, and delta-sigma). No quantization/companding,
capture-pass resampling, filter models, real polyphony/voice stealing, machine presets, or
hardware-panel UI exist yet - see
[`concrete-sampler-plugin-plan.md`](concrete-sampler-plugin-plan.md) for the full plan and current
progress. The editor is a plain utility panel (load a file, see the waveform, play via MIDI or an
on-screen keyboard), not the finished UI.

See the [root README](../README.md) for shared build requirements, the exFAT/apostrophe build
gotchas, and running tests across all plugins at once.

## Building

```sh
cd concrete-sampler
cmake -B build -G Xcode
cmake --build build --config Release --target Concrete_All
```

To build a single format only: `--target Concrete_AU`, `Concrete_VST3`, or `Concrete_Standalone`.

## Installation

`COPY_PLUGIN_AFTER_BUILD` is enabled, so a successful build automatically copies the plugin into
the standard user plugin directories:

- **AU:** `~/Library/Audio/Plug-Ins/Components/Concrete.component`
- **VST3:** `~/Library/Audio/Plug-Ins/VST3/Concrete.vst3`

Restart your DAW (or run AU validation, below) after installing. The Standalone app is built to
`build/Concrete_artefacts/Release/Standalone/Concrete.app`.

## Launching the Standalone app

The fastest edit/listen loop during development - no DAW rescan needed:

```sh
cmake --build build --config Debug --target Concrete_Standalone
open build/Concrete_artefacts/Debug/Standalone/Concrete.app
```

Load a sample via the Load button or by dragging a WAV/AIFF file onto the waveform area, then play
it from your MIDI controller or the on-screen keyboard at the bottom of the window. The Pitch
Engine dropdown and the Base Rate/Coarse Tune/Fine Tune sliders above the waveform switch between
Phase 2's playback engines - see the Parameters table below.

**The Standalone app remembers whatever you left it at, not the compiled-in defaults.** Every
plugin in this catalog persists its full state (via the same `getStateInformation()` a DAW session
save would use, including the loaded sample's embedded audio - see Architecture #2 in the plan
doc) to `~/Library/Application Support/Concrete.settings` between launches - this is standard JUCE
standalone-host behavior, not specific to Concrete. For a known-good baseline when testing, click
the **Reset** button next to the Root Note control - it sets:

| Control | Value |
|---|---|
| Pitch Engine | Reference (unless deliberately testing Mode A/B/C) |
| Base Rate | 44,100 Hz |
| Coarse Tune | 0 |
| Fine Tune | 0 |
| Root Note | C3 (60) |

Reset doesn't touch the loaded sample. To force a genuinely clean slate (also discards whichever
sample was embedded - reload it afterward): `rm ~/Library/Application\ Support/Concrete.settings`.

## Validating the AU (auval)

```sh
auval -v aumu Cnct WJag
```

`Cnct` is the plugin code, `WJag` the manufacturer code (both from `CMakeLists.txt`). The AU type
is `aumu` (music device/synth), not `aufx` - Concrete is `IS_SYNTH TRUE`. A pass prints
`AU VALIDATION SUCCEEDED`.

## Tests and offline rendering

Two `juce::UnitTestRunner`-based console apps (not CTest-integrated - see the root README):

```sh
cmake --build build --config Release --target ConcreteTests
./build/ConcreteTests_artefacts/Release/ConcreteTests

cmake --build build --config Release --target ConcreteProcessorTests
./build/ConcreteProcessorTests_artefacts/Release/ConcreteProcessorTests
```

`ConcreteTests` exercises the framework-free DSP/data seams directly (`ConcreteSampleSet`,
`ConcreteBusRouter`, `ConcreteVoiceAllocator`, `ConcreteVoice`, `ConcretePitchEngine`,
`ConcreteSampleIO`) against hand-built buffers and real temp WAV files. `ConcreteProcessorTests`
drives the real `ConcreteAudioProcessor` end-to-end (APVTS, MIDI dispatch, `processBlock`,
save/reload).

**`ConcreteRenderIR`** is a third console app (not a plugin format) that loads a real sample and
renders MIDI-triggered playback through the actual processor to a WAV file - this is what the
`analysis/` scripts below use for spectral verification, and it's also the fastest way to audition
a change by ear without opening a DAW:

```sh
cmake --build build --config Release --target ConcreteRenderIR
build/ConcreteRenderIR_artefacts/Release/ConcreteRenderIR \
  --out rendered-audio/mine.wav --sample /path/to/sample.wav --note 60 --seconds 3 \
  --pitchEngineMode 1 --baseRate 44100 --coarseTune 0 --fineTune 0
```

`--pitchEngineMode` is `0` (Reference), `1` (Mode A), `2` (Mode B), or `3` (Mode C). `--note`/
`--velocity` trigger a single held note (no note-off); `--sequence
"note:velocity:onSeconds:durationSeconds,..."` scripts a multi-note sequence instead. Any other
`--<paramID>` flag maps directly onto the plugin's own APVTS parameter IDs in native units.

`analysis/` holds the Python spectral-verification suite (`concrete_analysis.py`'s FFT/THD/null-
test primitives, and one `verify_phaseN.py` script per completed build phase) that
`ConcreteRenderIR` output gets checked against - see that phase's section in
`concrete-sampler-plugin-plan.md` for what each script verifies. Set up its venv once:

```sh
cd analysis
python3 -m venv venv && source venv/bin/activate
pip install -r ../../common/tools/requirements.txt
python3 verify_phase2.py   # or verify_phase0.py / verify_phase1.py
```

## Parameters

Only the Phase 2 pitch-engine controls exist so far - no quantization, capture-pass, filter, or
machine-preset parameters yet.

| Parameter | Range | Default | Description |
|---|---|---|---|
| Pitch Engine | Reference / Mode A / Mode B / Mode C | Reference | Which playback engine renders the note - see `ConcretePitchEngine.h`. Reference is Phase 1's clean, high-quality interpolation (no real machine preset will ever select it once Phase 7 lands). Mode A holds the last sample value at a variable clock that scales with transposition (zero-order hold, no anti-imaging). Mode B keeps a fixed output tick rate and pitches by dropping/repeating samples within it. Mode C is a 64x-oversampled 1-bit delta-sigma modulator with noise shaping. |
| Base Rate | 4,000 - 100,000 Hz | 44,100 Hz | The assumed machine capture/effective sampling rate for Modes A/B/C - substitutes for the loaded file's own real sample rate in the pitch calculation (a machine has no way to know what rate a foreign file was really recorded at). Defaults to 44.1kHz so a typical file reproduces its original pitch/tempo at root note out of the box; lower it to emulate a narrower-bandwidth machine on purpose (matches the SP-1200's 26.04kHz, for example), or raise/lower it to intentionally mismatch a specific loaded file's own rate. Has no effect in Reference mode. |
| Coarse Tune | -24 to +24 semitones | 0 | Added to the note's own transposition from the zone's root note. |
| Fine Tune | -50 to +50 cents | 0 | Added on top of Coarse Tune. |

## Project structure

```
concrete-sampler/
├── CMakeLists.txt
├── concrete-sampler-plugin-plan.md   # The full 9-phase build plan and current progress
├── Source/
│   ├── PluginProcessor.h/.cpp        # Parameter state, MIDI dispatch, sample loading, voice pool
│   ├── PluginEditor.h/.cpp           # Utility UI: file load/drag-drop, waveform, on-screen keyboard
│   ├── ConcreteLookAndFeel.h/.cpp    # Thin subclass of the shared HardwarePanelLookAndFeel
│   ├── ConcreteSampleZone.h          # One playable region: buffer, key/velocity range, tune/level/pan
│   ├── ConcreteSampleSet.h           # The zone list + note/velocity lookup (multi-zone-ready)
│   ├── ConcreteSampleIO.h/.cpp       # File loading, FLAC session-embedding, relocate handling
│   ├── ConcreteVoice.h/.cpp          # One note's playback: pitch engine + ADSR envelope
│   ├── ConcretePitchEngine.h/.cpp    # Reference/Mode A/Mode B/Mode C playback engines
│   ├── ConcreteVoiceAllocator.h      # Voice-to-note allocation/oldest-voice-stealing for the pool
│   ├── ConcreteBusRouter.h           # Per-zone output-destination indirection (main bus only so far)
│   ├── Tests/                        # ConcreteTests (DSP/data seams) + ConcreteProcessorTests
│   └── Tools/RenderIR.cpp            # ConcreteRenderIR: offline MIDI-driven render console app
├── analysis/                          # Python spectral verification (see "Tests and offline rendering")
├── rendered-audio/                    # ConcreteRenderIR output (gitignored, regenerable)
└── test-assets/                       # Generated test WAVs (gitignored - see analysis/make_test_assets.py)
```

## License

[AGPLv3](../LICENSE), same as the rest of this repo.
