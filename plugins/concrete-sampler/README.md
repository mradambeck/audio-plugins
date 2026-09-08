# Concrete

A vintage sampler emulation instrument (AU / VST3 / Standalone) modeling the playback
architectures of twelve classic 1980s hardware samplers - the pitch-shifting mechanism,
quantization scheme, and analog filter behavior of each machine, not just a bitcrusher preset.

**Status: early build-in-progress, not a usable instrument yet.** Phases 0-7 of the 9-phase build
plan are done (scaffold, clean sample playback, the three pitch-engine modes - variable-clock
zero-order hold, fixed-rate drop-sample decimation, and delta-sigma - bit-depth reduction plus
E-mu-style companding, the capture pass: an offline resample/drive/quantize technique modeling
"pitch the source up before capturing it, then pitch it back down on playback," five playback-side
filter models, Phase 6's voice architecture - a per-preset voice-count limit with deterministic
oldest-voice stealing, choke groups, velocity-sensitive amp gain, and a K250-style "contoured" amp
envelope alternative to the default flat-sustain ADSR - and Phase 7's twelve hand-authored machine
presets, selectable via a Machine parameter and via the host's own native program list), plus a
standalone One-Shot playback option not in the original plan. The hardware-panel UI doesn't exist
yet - see [`concrete-sampler-plugin-plan.md`](concrete-sampler-plugin-plan.md) for the full plan
and current progress. The editor is a plain utility panel (load a file, see the waveform, play via
MIDI or an on-screen keyboard), not the finished UI.

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
it from your MIDI controller or the on-screen keyboard at the bottom of the window. The Machine
dropdown right under the load/reset controls is Phase 7's primary control: picking one of the
twelve applies its whole parameter set (pitch engine, base rate, bit depth, companding, filter
model, voice count, amp envelope) in one go, WITHOUT touching the loaded sample - everything it
sets is still a normal secondary control underneath, so a machine is a starting point, not a locked
mode. It starts on "(Custom)," a deliberate non-machine placeholder (see
`ConcreteAudioProcessor::machineParamID`'s own comment) so a fresh instance keeps every control's
own independent default rather than being silently colored by whichever machine is listed first;
your host's own native program/preset list offers the same twelve machines too, kept in sync with
this dropdown automatically. The One-Shot toggle next to Root Note switches between the two ways a
sampler can handle playback: off (the default) plays only while the key/trigger is held, stopping
(with the amp envelope's release tail) on note-off; on plays the whole zone through to its own end
regardless of how long the key was held - see `ConcreteSampleZone::oneShot`. The Pitch Engine
dropdown and the Base Rate/Coarse Tune/Fine Tune sliders below the Machine control switch between
Phase 2's playback engines; the Bit Depth slider and Quantizer Mode dropdown below them switch
between Phase 3's bit-depth reduction and companding; the Capture Transpose/Drive sliders and Pitch
Compensate/Capture Bypass toggles below those control Phase 4's capture pass; the Filter
Model/Cutoff/Resonance/Env Amount/Key Track controls below those are Phase 5's; the Voice Count
slider and Amp Envelope dropdown at the bottom are Phase 6's - see the Parameters table below.
Capture-pass and quantizer changes trigger a background re-bake of the loaded sample (not instant -
there's no bake-progress indicator yet, that's Phase 8); the live filter controls, Voice Count, Amp
Envelope, and Machine itself (like Phase 2's pitch controls) take effect on the very next sample
instead, with no re-bake at all - though selecting a machine that also changes Bit Depth/Quantizer
Mode/Capture Transpose still triggers the SAME re-bake those controls always would on their own.
One-Shot and Root Note are zone state, like the loaded sample itself - not APVTS parameters, so
they aren't automatable and take effect immediately, with no re-bake either.

**Bit Depth/Quantizer Mode have no audible effect until Capture Bypass is off.** Every machine
preset ships with the capture pass bypassed by design (see the Parameters table's own Capture
Bypass row) - Bit Depth/Quantizer Mode are baked in as PART of that same pass, so a machine's own
12-bit/8-bit/13-bit storage character (its defining trait in the machine table) only becomes
audible once you turn Capture Bypass off yourself. Picking a machine still sets the right Bit
Depth/Quantizer Mode/Capture Transpose values in advance, ready for whenever you do.

**The Standalone app remembers whatever you left it at, not the compiled-in defaults.** Every
plugin in this catalog persists its full state (via the same `getStateInformation()` a DAW session
save would use, including the loaded sample's embedded audio - see Architecture #2 in the plan
doc) to `~/Library/Application Support/Concrete.settings` between launches - this is standard JUCE
standalone-host behavior, not specific to Concrete. For a known-good baseline when testing, click
the **Reset** button next to the Root Note control - it sets:

| Control | Value |
|---|---|
| Machine | (Custom) |
| Pitch Engine | Reference (unless deliberately testing Mode A/B/C) |
| Base Rate | 44,100 Hz |
| Coarse Tune | 0 |
| Fine Tune | 0 |
| Bit Depth | 16-bit |
| Quantizer Mode | Linear |
| Capture Transpose | 5 st |
| Capture Drive | 0 dB |
| Capture Pitch Compensate | On |
| Capture Bypass | On |
| Capture Iterations | 1 |
| Filter Model | Bypass |
| Filter Cutoff | 20,000 Hz |
| Filter Resonance | 0 |
| Filter Env Amount | 0 oct |
| Filter Key Track | 0 |
| Voice Count | 8 |
| Amp Envelope | ADSR |
| One-Shot | Off |
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
python3 verify_phase7.py   # or verify_phase0.py / verify_phase1.py / verify_phase2.py / verify_phase3.py / verify_phase4.py / verify_phase5.py / verify_phase6.py
```

`verify_phase7.py` also writes `analysis/validation_report.md` and `analysis/validation_results.json`
(the twelve-machine spectral summary, pairwise null tests, and targeted checks against the plan's
machine table).

## Parameters

The Phase 2 pitch-engine controls, Phase 3's quantizer, Phase 4's capture pass, Phase 5's filter
controls, Phase 6's voice-architecture controls, and Phase 7's Machine selector exist so far.

| Parameter | Range | Default | Description |
|---|---|---|---|
| Machine | (Custom) / the twelve machines below | (Custom) | Applies a whole preset's worth of the OTHER parameters below in one go (pitch engine, base rate, bit depth, quantizer mode, capture transpose, filter model, voice count, amp envelope) - see `ConcreteMachines.h`. Never touches the loaded sample or Capture Bypass's own on/off state beyond what the machine itself sets (always on - see the plan's table notes). "(Custom)" is a deliberate no-op placeholder, not a thirteenth machine, so a fresh instance isn't silently colored before you've touched anything, and so every real machine stays reachable from its own combo box (JUCE doesn't fire a combo's onChange when you reselect the item already showing). The host's own native program/preset list offers the same twelve, kept in sync automatically. |
| Pitch Engine | Reference / Mode A / Mode B / Mode C | Reference | Which playback engine renders the note - see `ConcretePitchEngine.h`. Reference is Phase 1's clean, high-quality interpolation (no real machine preset ever selects it). Mode A holds the last sample value at a variable clock that scales with transposition (zero-order hold, no anti-imaging). Mode B keeps a fixed output tick rate and pitches by dropping/repeating samples within it. Mode C is a 64x-oversampled 1-bit delta-sigma modulator with noise shaping. |
| Base Rate | 4,000 - 100,000 Hz | 44,100 Hz | The assumed machine capture/effective sampling rate for Modes A/B/C - controls ONLY artifact character (how coarse the zero-order hold/decimation is), never root-pitch playback speed, which always tracks the loaded file's own real rate regardless of this control (see `ConcretePitchEngine.h`'s own comment on `readModeA()` - an earlier version conflated the two, a real bug where switching machines changed a loaded sample's pitch/tempo, not just its character). Defaults to 44.1kHz, matching a typical loaded file, so the default is transparent (no artifact) until Base Rate is deliberately lowered to emulate a narrower-bandwidth machine on purpose (matches the SP-1200's 26.04kHz, for example) or a machine preset sets its own default. Has no effect in Reference mode. |
| Coarse Tune | -24 to +24 semitones | 0 | Added to the note's own transposition from the zone's root note. |
| Fine Tune | -50 to +50 cents | 0 | Added on top of Coarse Tune. |
| Bit Depth | 1-16 bit | 16-bit | The storage word width the capture pass quantizes each zone's working buffer to - see `ConcreteQuantizer.h`. Baked offline into the working buffer (Phase 4), not applied live - changing it triggers a background re-bake. Defaults to 16-bit, which is transparent (noise floor around -96dBFS); the machine presets set the real depths (8, 12, 13-bit) from the machine table, though those have no audible effect until Capture Bypass is off (see that row below - bit-depth reduction is baked in as part of the SAME capture pass every machine ships with off). |
| Quantizer Mode | Linear / Companded | Linear | Linear is direct mid-tread rounding at Bit Depth - a constant quantization step regardless of signal level. Companded compresses (mu-law-shaped) before quantizing and expands after, the E-mu Emulator II/Emax storage scheme - quantization error shrinks at low signal levels at the cost of some full-scale headroom. No dither anywhere in either mode, matching the real machines. |
| Capture Transpose | 0-24 semitones | 5 | The 33->45rpm-style pitch-up amount applied (per iteration) before "capturing" - see `ConcreteCapturePass.h`. Has no effect while Capture Bypass is on. 5 semitones is the ratio most people actually want once they turn bypass off. |
| Capture Drive | 0-24 dB | 0 dB | Saturation drive into the capture stage, plus an accompanying high-frequency rolloff that increases with drive (modeling "sampling hot rolled off the top end on playback," e.g. the MPC60). 0dB is a no-op - no added saturation or filtering. |
| Capture Pitch Compensate | On/Off | On | Subtracts the baked-in Capture Transpose back out at playback, so a note lands at its expected pitch while still carrying the capture pass's artifacts. Off plays the pitched-up/sped-up capture directly. A live toggle - does not trigger a re-bake. |
| Capture Bypass | On/Off | On | Disables the whole capture pass (resample + drive + quantize) at once, making the working buffer an exact copy of the source - "every preset ships with the capture pass off," per the plan; it's a technique the user applies, not a machine's stock behavior. |
| Capture Iterations | 1-4 | 1 | Repeats the whole resample -> drive -> quantize chain this many times, for compounding degradation. |
| Filter Model | Bypass / SSM / CEM Loss / CEM Compensated / Digital+VCA / One-Pole | Bypass | The playback-side filter (see `ConcreteFilterModels.h`) - live, per-voice, applied fresh every sample (never baked). SSM/CEM Loss/CEM Compensated share a 4-pole resonant ladder core: SSM adds its own internal saturation and can self-oscillate; CEM Loss loses passband level as resonance rises (the Fairlight/Linn 9000 CEM3320); CEM Compensated holds passband level steady instead (the Mirage's CEM3328). Digital+VCA is a clean, non-ladder state-variable lowpass plus a fixed VCA-character saturation stage (the K250 - deliberately not an analog filter model). One-Pole (the SK-1) ignores Filter Resonance entirely - a single pole can't peak or self-oscillate. Bypass ignores every filter parameter. |
| Filter Cutoff | 20-20,000 Hz | 20,000 Hz | Defaults fully open (transparent) - same "no coloration until asked for" convention as every other control. |
| Filter Resonance | 0-1 | 0 | 1 is at (or just past) self-oscillation for the ladder models; has no effect on One-Pole. |
| Filter Env Amount | -8 to +8 octaves | 0 | Depth of a fixed-shape filter envelope's modulation of cutoff (bipolar - negative sweeps down). The envelope's own attack/decay/sustain/release shape isn't user-adjustable, only its depth - same fixed-shape convention as the amp envelope (Amp Envelope below only picks between two whole shapes, not their individual stage timings). |
| Filter Key Track | 0-1 | 0 | How much the cutoff scales with the played note's distance from C3 - 0 is no tracking, 1 is full 1:1 tracking, like pitch. |
| Voice Count | 1-18 | 8 | The runtime polyphony cap (see `ConcreteVoiceAllocator.h`) - a note-on beyond this many already-sounding voices steals the oldest-triggered one instead of adding a 9th, matching the plan's "running out of voices was an audible, characteristic part of playing these machines." A live control, not baked - takes effect on the very next note-on. |
| Amp Envelope | ADSR / Contoured | ADSR | The per-voice amplitude envelope shape (see `ConcreteContourEnvelope.h`). ADSR is a flat-sustain shape (2ms attack, no decay, full sustain, 50ms release) - every earlier phase's behavior. Contoured is the Kurzweil K250's dual-VCA-style shape instead: it keeps decaying in two stages the whole time a note is held, rather than holding a flat level, modeling "amplitude contouring, not filtering." |

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
│   ├── ConcreteVoice.h/.cpp          # One note's playback: pitch engine + filter + amp envelope
│   ├── ConcretePitchEngine.h/.cpp    # Reference/Mode A/Mode B/Mode C playback engines
│   ├── ConcreteQuantizer.h           # Linear bit-depth reduction + mu-law compand/expand curve
│   ├── ConcreteCapturePass.h/.cpp    # Offline resample -> drive -> quantize working-buffer bake
│   ├── ConcreteFilterModels.h        # SSM/CEM Loss/CEM Compensated/Digital+VCA/One-Pole/Bypass
│   ├── ConcreteContourEnvelope.h     # K250-style continuously-decaying amp envelope alternative
│   ├── ConcreteVoiceAllocator.h      # Voice-to-note allocation/oldest-voice-stealing, voice-count limit
│   ├── ConcreteMachines.h            # The twelve machine presets as data (name -> parameter values)
│   ├── ConcreteBusRouter.h           # Per-zone output-destination indirection (main bus only so far)
│   ├── Tests/                        # ConcreteTests (DSP/data seams) + ConcreteProcessorTests
│   └── Tools/RenderIR.cpp            # ConcreteRenderIR: offline MIDI-driven render console app
├── analysis/                          # Python spectral verification (see "Tests and offline rendering")
├── rendered-audio/                    # ConcreteRenderIR output (gitignored, regenerable)
└── test-assets/                       # Generated test WAVs (gitignored - see analysis/make_test_assets.py)
```

## License

[AGPLv3](../LICENSE), same as the rest of this repo.
