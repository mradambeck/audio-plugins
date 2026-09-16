# Concrete

A vintage sampler emulation instrument (AU / VST3 / Standalone) modeling the playback
architectures of twelve classic 1980s hardware samplers - the pitch-shifting mechanism,
quantization scheme, and analog filter behavior of each machine, not just a bitcrusher preset.

All 9 build phases are done: the sample engine (three pitch-engine modes, bit-depth reduction and
companding, the offline capture pass, five playback-side filter models, voice architecture with
choke groups and a K250-style contoured amp envelope, the twelve hand-authored machine presets),
per-pad sample loading (a 4x4 pad grid where any pad can hold its own independent sample, not just
a transposed copy of the main one), and a full hardware-panel UI - see
[`concrete-sampler-plugin-plan.md`](concrete-sampler-plugin-plan.md) for the complete build history
and design rationale.

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

**The panel.** The LCD screen (top left) is how every per-sample setting gets edited - it has four
pages (Sample/Machine/Filter/Capture, switchable via the physical SoftKeys row underneath it or by
clicking a footer tab on the screen itself): Sample shows the waveform, filename, embedded-vs-path
status, duration, a moving playhead while something's playing, and the Root/One-Shot/Loop/Coarse/
Fine/Volume fields; Machine shows the pitch engine, base rate, bit depth, quantizer mode, and amp
envelope; Filter shows cutoff/resonance/env-amount/key-track; Capture shows the offline resample/
drive/quantize controls plus the Save Sample embed toggle and its live payload-size readout. The
physical DirectionalPad moves which field is selected; the DataKnob changes the selected field's
value - together they're the only way to edit anything the screen shows (there's no mouse-drag-to-
adjust on the fields themselves, only tap-to-select).

**The pads.** The 4x4 grid below the screen is the trigger surface - by default all 16 play the one
loaded sample chromatically from its root (the classic SP-1200/MPC "tune the pad" workflow), but
dragging an audio file onto any single pad (or right-clicking it for a Load.../Clear/Copy/Paste
menu) gives that pad its own independent sample, transposition, and settings, completely separate
from the main sample and every other pad - hitting that pad switches the LCD screen (and the
physical One-Shot/Loop/Sample Volume controls) to show and edit THAT pad's sample instead. A
looping pad's sound keeps playing indefinitely; hitting the same pad again stops it rather than
starting a second overlapping copy, and turning its Loop field off (or switching Machine, for a
looping pad specifically) takes effect immediately rather than waiting for the next trigger.

**The Machine selector** (top right) is the primary "pick a character" control - selecting one of
the twelve applies its whole parameter set (pitch engine, base rate, bit depth, companding, filter
model, voice count, amp envelope) in one go, WITHOUT touching the loaded sample or its Coarse/Fine/
Root/Loop settings - everything a machine sets is still a normal secondary control underneath, so a
machine is a starting point, not a locked mode. It starts on "(Custom)," a deliberate non-machine
placeholder (see `ConcreteAudioProcessor::machineParamID`'s own comment) so a fresh instance keeps
every control's own independent default rather than being silently colored by whichever machine is
listed first; your host's own native program/preset list offers the same twelve machines too, kept
in sync with this selector automatically.

**Session block** (right column, below the Machine selector and Edit section): One-Shot and Loop
toggle the currently-displayed sample's own playback mode (mirroring the LCD's own fields);
Resample re-runs the capture pass; Save Sample toggles whether the current session embeds its
sample data or only references the file path; Clear Sample clears whichever sample the screen is
currently showing (a pad's own sample, or the main one - never the whole kit at once); Stop All
immediately silences anything currently playing.

**Capture-pass and Bit Depth/Quantizer Mode changes trigger a background re-bake** of the affected
zone (there's a bake-in-progress indicator on the Capture page) - the live filter controls, Voice
Count, Amp Envelope, and Machine's own pitch-engine/base-rate settings take effect on the very next
note instead, with no re-bake at all, EXCEPT for an already-looping sample, which adopts a live
Machine change (pitch engine, base rate, filter model, amp envelope) immediately rather than
waiting for its next trigger - see `ConcreteVoice::refreshLiveLoopState()`.

**Bit Depth/Quantizer Mode have no audible effect until Capture Bypass is off.** Every machine
preset ships with the capture pass bypassed by design - Bit Depth/Quantizer Mode are baked in as
PART of that same pass, so a machine's own 12-bit/8-bit/13-bit storage character (its defining
trait in the machine table) only becomes audible once you turn Capture Bypass off yourself on the
Capture page. Picking a machine still sets the right Bit Depth/Quantizer Mode/Capture Transpose
values in advance, ready for whenever you do.

**The Standalone app remembers whatever you left it at, not the compiled-in defaults.** Every
plugin in this catalog persists its full state (via the same `getStateInformation()` a DAW session
save would use, including any embedded sample audio - see Architecture #2 in the plan doc) to
`~/Library/Application Support/Concrete.settings` between launches. For a genuinely clean slate
(also discards every embedded sample - reload afterward): `rm ~/Library/Application\ Support/Concrete.settings`.

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
save/reload, per-pad sample loading, the live-looping behaviors above).

**`ConcreteRenderIR`** is a third console app (not a plugin format) that loads a real sample and
renders MIDI-triggered playback through the actual processor to a WAV file - this is what the
`analysis/` scripts below use for spectral verification, and it's also the fastest way to audition
a change by ear without opening a DAW:

```sh
cmake --build build --config Release --target ConcreteRenderIR
build/ConcreteRenderIR_artefacts/Release/ConcreteRenderIR \
  --out rendered-audio/mine.wav --sample /path/to/sample.wav --note 60 --seconds 3 \
  --pitchEngineMode 1 --baseRate 44100
```

`--pitchEngineMode` is `0` (Reference), `1` (Mode A), `2` (Mode B), or `3` (Mode C). `--note`/
`--velocity` trigger a single held note (no note-off); `--sequence
"note:velocity:onSeconds:durationSeconds,..."` scripts a multi-note sequence instead. `--coarseTune`/
`--fineTune` (semitones/cents) apply to zone 0 right after `--sample` loads, since those are
per-zone state rather than APVTS parameters (see the Parameters table below). Any other
`--<paramID>` flag maps directly onto the plugin's own APVTS parameter IDs in native units.

`analysis/` holds the Python spectral-verification suite (`concrete_analysis.py`'s FFT/THD/null-
test primitives, and one `verify_phaseN.py` script per completed build phase) that
`ConcreteRenderIR` output gets checked against - see that phase's section in
`concrete-sampler-plugin-plan.md` for what each script verifies. Set up its venv once:

```sh
cd analysis
python3 -m venv venv && source venv/bin/activate
pip install -r ../../common/tools/requirements.txt
python3 verify_phase7.py   # or verify_phase0.py / verify_phase1.py / ... / verify_phase6.py
```

`verify_phase7.py` also writes `analysis/validation_report.md` and `analysis/validation_results.json`
(the twelve-machine spectral summary, pairwise null tests, and targeted checks against the plan's
machine table).

## Parameters

Most per-sample settings (Root Note, One-Shot, Loop Enabled, Coarse Tune, Fine Tune, Volume) are
**per-zone state edited via the LCD screen's Sample page** (physical DirectionalPad+DataKnob), not
APVTS parameters - each pad's own sample carries its own independent values, which wouldn't be
possible if these were single global automatable parameters (an earlier revision made Coarse/Fine
global APVTS parameters and found the bug the hard way: tuning one pad's sample silently retuned
every other pad's and the main sample's pitch too). The table below covers what IS a real APVTS
parameter - global, host-automatable, one value shared by every zone.

| Parameter | Range | Default | Description |
|---|---|---|---|
| Machine | (Custom) / the twelve machines below | (Custom) | Applies a whole preset's worth of the OTHER parameters below in one go (pitch engine, base rate, bit depth, quantizer mode, capture transpose, filter model, voice count, amp envelope) - see `ConcreteMachines.h`. Never touches the loaded sample, its per-zone state, or Capture Bypass's own on/off state beyond what the machine itself sets (always on - see the plan's table notes). "(Custom)" is a deliberate no-op placeholder, not a thirteenth machine, so a fresh instance isn't silently colored before you've touched anything. The host's own native program/preset list offers the same twelve, kept in sync automatically. An already-looping sample adopts a Machine change's pitch engine/base rate/filter model/amp envelope live, rather than waiting for its next trigger. |
| Pitch Engine | Reference / Mode A / Mode B / Mode C | Reference | Which playback engine renders the note - see `ConcretePitchEngine.h`. Reference is Phase 1's clean, high-quality interpolation (no real machine preset ever selects it). Mode A holds the last sample value at a variable clock that scales with transposition (zero-order hold, no anti-imaging). Mode B keeps a fixed output tick rate and pitches by dropping/repeating samples within it. Mode C is a 64x-oversampled 1-bit delta-sigma modulator with noise shaping. |
| Base Rate | 4,000 - 100,000 Hz | 44,100 Hz | The assumed machine capture/effective sampling rate for Modes A/B/C - controls ONLY artifact character (how coarse the zero-order hold/decimation is), never root-pitch playback speed, which always tracks the loaded file's own real rate regardless of this control. Defaults to 44.1kHz, matching a typical loaded file, so the default is transparent (no artifact) until Base Rate is deliberately lowered to emulate a narrower-bandwidth machine (matches the SP-1200's 26.04kHz, for example) or a machine preset sets its own default. Has no effect in Reference mode. |
| Bit Depth | 1-16 bit | 16-bit | The storage word width the capture pass quantizes each zone's working buffer to - see `ConcreteQuantizer.h`. Baked offline into the working buffer, not applied live - changing it triggers a background re-bake. Defaults to 16-bit, which is transparent (noise floor around -96dBFS); the machine presets set the real depths (8, 12, 13-bit) from the machine table, though those have no audible effect until Capture Bypass is off (bit-depth reduction is baked in as part of the SAME capture pass every machine ships with off). |
| Quantizer Mode | Linear / Companded | Linear | Linear is direct mid-tread rounding at Bit Depth - a constant quantization step regardless of signal level. Companded compresses (mu-law-shaped) before quantizing and expands after, the E-mu Emulator II/Emax storage scheme - quantization error shrinks at low signal levels at the cost of some full-scale headroom. No dither anywhere in either mode, matching the real machines. |
| Capture Transpose | 0-24 semitones | 5 | The 33->45rpm-style pitch-up amount applied (per iteration) before "capturing" - see `ConcreteCapturePass.h`. Has no effect while Capture Bypass is on. |
| Capture Drive | 0-24 dB | 0 dB | Saturation drive into the capture stage, plus an accompanying high-frequency rolloff that increases with drive (modeling "sampling hot rolled off the top end on playback," e.g. the MPC60). 0dB is a no-op. |
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
| Master Volume | 0-120% | 100% | A flat output-gain stage applied after every voice renders - host-automatable and session-saved, unlike Sample Volume (a per-zone gain, see above). |

## Project structure

```
concrete-sampler/
├── CMakeLists.txt
├── concrete-sampler-plugin-plan.md   # The full 9-phase build plan and current progress
├── ui-plan.md                         # Phase 8's hardware-panel UI design spec
├── Source/
│   ├── PluginProcessor.h/.cpp         # Parameter state, MIDI dispatch, sample loading, voice pool
│   ├── PluginEditor.h/.cpp            # Hardware-panel editor shell (EditorContent + ResizableZoomHandler)
│   ├── ConcreteLookAndFeel.h/.cpp     # Thin subclass of the shared HardwarePanelLookAndFeel
│   ├── ConcreteScreen.h/.cpp          # The LCD: boot sequence + Sample/Machine/Filter/Capture pages
│   ├── ConcretePadGrid.h/.cpp         # The 4x4 trigger surface + per-pad drag-and-drop/context menu
│   ├── ConcreteSoftKeys.h/.cpp        # Physical page-select row mirroring the LCD's own footer tabs
│   ├── ConcreteMachineSelector.h/.cpp # The Machine picker's physical panel widget
│   ├── ConcreteDirectionalPad.h/.cpp  # Moves which LCD field is selected
│   ├── ConcreteDataKnob.h/.cpp        # Adjusts the selected LCD field's value
│   ├── ConcreteKnob.h/.cpp            # Cutoff/Resonance rotary knobs
│   ├── ConcreteFader.h/.cpp           # Sample/Master Volume vertical faders
│   ├── ConcretePanelButton.h/.cpp     # One-Shot/Loop/Resample/Save Sample/Clear Sample/Stop All
│   ├── ConcreteSampleZone.h           # One playable region: buffer, key/velocity range, tune/level/pan
│   ├── ConcreteSampleSet.h            # The zone list + note/velocity lookup (narrowest-match-wins)
│   ├── ConcreteSampleIO.h/.cpp        # File loading, FLAC session-embedding, relocate handling
│   ├── ConcreteVoice.h/.cpp           # One note's playback: pitch engine + filter + amp envelope
│   ├── ConcretePitchEngine.h/.cpp     # Reference/Mode A/Mode B/Mode C playback engines
│   ├── ConcreteQuantizer.h            # Linear bit-depth reduction + mu-law compand/expand curve
│   ├── ConcreteCapturePass.h/.cpp     # Offline resample -> drive -> quantize working-buffer bake
│   ├── ConcreteFilterModels.h         # SSM/CEM Loss/CEM Compensated/Digital+VCA/One-Pole/Bypass
│   ├── ConcreteContourEnvelope.h      # K250-style continuously-decaying amp envelope alternative
│   ├── ConcreteVoiceAllocator.h       # Voice-to-note allocation/oldest-voice-stealing, voice-count limit
│   ├── ConcreteMachines.h             # The twelve machine presets as data (name -> parameter values)
│   ├── ConcreteBusRouter.h            # Per-zone output-destination indirection (main bus only so far)
│   ├── Tests/                         # ConcreteTests (DSP/data seams) + ConcreteProcessorTests
│   └── Tools/RenderIR.cpp             # ConcreteRenderIR: offline MIDI-driven render console app
├── analysis/                          # Python spectral verification (see "Tests and offline rendering")
├── rendered-audio/                    # ConcreteRenderIR output (gitignored, regenerable)
└── test-assets/                       # Generated test WAVs (gitignored - see analysis/make_test_assets.py)
```

## License

[AGPLv3](../LICENSE), same as the rest of this repo.
