# Intruder

*Non-Linear Gated Reverb.* (Highlight colour: `#CFD640`.)

A gated algorithmic reverb (AU / VST3 / Standalone) emulating the AMS RMX16 "Non-Lin 2" program:
a dense diffuse tank whose audible envelope is a separate hold-then-decay shape layered on top,
not the tank's own natural decay - see "How it works" below for why, and what that measurement
actually showed. Built empirically from 19 real hardware captures (`ir-captures/`, analysis
pipeline in `analysis/`) rather than guessed from the manual: the filename-encoded "decay" label,
H value, and "Tighter" flag were all reverse-engineered from the audio first (`analysis/findings.md`),
and the DSP mapping/tuning was iterated against those captures afterward (`analysis/validate.py`,
`analysis/validation_report.md`).

See the [root README](../README.md) for shared build requirements, the exFAT/apostrophe build
gotchas, and running tests across all plugins at once.

## Building

```sh
cd intruder-gated-reverb
cmake -B build -G Xcode
cmake --build build --config Release --target Intruder_All
```

To build a single format only: `--target Intruder_AU`, `Intruder_VST3`, or `Intruder_Standalone`.

## Installation

`COPY_PLUGIN_AFTER_BUILD` is enabled, so a successful build automatically copies the plugin into
the standard user plugin directories:

- **AU:** `~/Library/Audio/Plug-Ins/Components/Intruder - Reverb.component`
- **VST3:** `~/Library/Audio/Plug-Ins/VST3/Intruder - Reverb.vst3`

Restart your DAW (or run AU validation, below) after installing. The Standalone app is built to
`build/Intruder_artefacts/Release/Standalone/Intruder - Reverb.app`.

## Launching the Standalone app

```sh
cmake --build build --config Debug --target Intruder_Standalone
open build/Intruder_artefacts/Debug/Standalone/*.app
```

(The product name has spaces in it - `Intruder - Reverb.app` - so if you're typing the path out by
hand rather than globbing it, it needs to stay quoted as one argument:
`open "build/Intruder_artefacts/Debug/Standalone/Intruder - Reverb.app"`. A copy-paste that
silently converts straight quotes to curly ones will break that quoting - the glob form above
sidesteps the issue entirely.)

## Validating the AU (auval)

```sh
auval -v aufx Intr WJag
```

## Offline analysis & validation workflow

This plugin's DSP is derived and tuned against real hardware captures rather than by ear alone.
All of it lives under `analysis/` (its own Python venv - `python3 -m venv analysis/venv && source
analysis/venv/bin/activate && pip install -r ../common/tools/requirements.txt`, plus `soundfile`):

1. **`ir-captures/`** - the 19 ground-truth AMS RMX16 "Non-Lin 2" captures this whole plugin is
   built from. Filenames encode decay label / H (dB) / an optional Tighter flag - see
   `analysis/inventory.py`. Never read at runtime; the algorithm is real-time and parametric, not
   a convolution reverb.
2. **`analysis/inventory.py`** - parses the 19 filenames into a table, confirms sample
   rate/format consistency, and prints which (decay, H, Tighter) combinations exist.
3. **`analysis/analyze_irs.py`** - the Phase 1 feature-extraction pipeline: RT60 (both a narrow
   Schroeder fit and a wider, more representative `full_decay_rt60`), Hilbert/RMS envelope shape,
   spectral tilt over time, early-reflection taps, echo-density buildup. Writes
   `analysis/features.json` and one plot per capture under `analysis/plots/`.
4. **`analysis/findings.md`** - the Phase 2 write-up of what H and Tighter actually turned out to
   be, derived from that feature table (see "How it works" below for the headline results).
5. **`IntruderRenderIR`** (a console app, not a plugin format) - feeds a signal through the real
   `IntruderAudioProcessor` and writes the result to WAV:

   ```sh
   cmake --build build --config Release --target IntruderRenderIR
   build/IntruderRenderIR_artefacts/Release/IntruderRenderIR --out rendered/mine.wav --decaySeconds 2 --tiltDb -4
   ```

   `--testSignal impulse` (default) renders a single-sample impulse. `--testSignal sustained`
   renders a held tone with a couple of silent gaps - the signal that originally exposed the
   envelope-retrigger bug described below; use it when testing anything envelope/retrigger-related,
   an impulse alone won't exercise that path. Flags map 1:1 onto the plugin's own parameter IDs;
   see the file's header comment for the full list.
6. **`analysis/validate.py`** - Phase 6: renders the plugin at each of the 19 reference settings
   (mapping each reference's Decay to its own `full_decay_rt60_s`, H directly to the Low/High
   parameter, Tighter to 0/100% on the Smoothing parameter) and compares against the real capture
   on envelope correlation, log-spectral distance/EQ balance (both via
   `../common/tools/compare_wavs.py`), crest factor (compression/dynamics), and spectral flatness
   (harmonic/resonant character) - not amplitude alone. Writes `analysis/validation_report.md` and
   per-setting plots under `analysis/validation_plots/`.

Rerun steps 5-6 after any DSP change - that's the point of having them as scripts rather than
one-off checks. `analysis/validation_report.md`'s "Status" section documents the actual tuning
history (what was tried, what moved the numbers, what's still a known limit) rather than just a
final snapshot - read it before assuming a metric that looks off is a regression you introduced.

## How it works

The core is `IntruderFDNEngine`, driven directly by `analysis/findings.md`'s empirical measurements,
not assumptions:

- **Envelope shape**: every one of the 19 captures showed a hold near the peak (flat within
  -5..-10dB for roughly the first quarter of the decay) before a knee into a steeper fall - not a
  plain exponential. That's why the envelope is a *separate* multiplicative hold-then-decay stage
  (`processStereo()`'s hold/decay/retrigger logic) layered on top of the FDN tank's own decay,
  rather than left to the tank's decay alone.
- **The tank itself is deliberately sustained well past the audible envelope** (`updateFeedbackGains()`'s
  `tankSustainMultiplier`) so the envelope - not the tank's own feedback gain - is what shapes the
  audible contour. That tank/envelope split turned out to be the dominant lever on how resonant
  the tail sounds: see `analysis/validation_report.md`'s "Status" section for how sweeping this one
  constant (4.0x down to 1.3x) improved measured spectral flatness by ~19dB on average, more than
  either adding delay-line modulation or doubling the line count did on their own.
- **"Decay" is not the hardware's literal seconds label.** The filename label spans 0.1-9.8s but
  measured RT60 only spans ~0.19-0.49s - the label is the RMX16's own display scale, not literal
  seconds. The plugin's own Decay control is deliberately redefined in continuous, musically-useful
  seconds instead of replicating that compressed scale; `analysis/findings.md`'s
  `decayLabelToRT60`/`IntruderReferenceData.h` table is kept for cross-validation only, not used to
  drive the DSP.
- **"Low/High" (the hardware's H) is a bass/treble tilt, not HF-only damping.** Onset spectral tilt
  tracks H alone, independent of Decay, and breaking it into bands showed bass and treble moving in
  *opposite* directions around a ~1-4kHz pivot (more negative H = more bass, less treble) - a plain
  one-pole lowpass can't produce that. `TiltFilter` implements this properly (see
  `common/dsp/TiltFilter.h`); `IntruderParameterMap::mapTiltDbFromH()` maps the H control through
  the actual measured (nonlinear - the slope roughly doubles between -3..0dB and -9..-3dB) curve
  rather than a linear guess.
- **"Smoothing" (named "Diffusion" until 2026-08-29 - Adam's naming call; the hardware's Tighter)
  compresses echo-density buildup timing, not decay or tone.** Every one of the 9 measured on/off
  pairs showed the same thing: earlier, faster echo-density buildup, averaging to ~76% of the
  untightened rise time. `IntruderParameterMap::mapTighterToSpacingMultiplier()` interpolates from
  1.0 (no effect) to that measured 0.76 ratio - not an invented intermediate curve, since Tighter
  is a binary flag on the real hardware and that ratio is the one thing actually measured. The
  plugin applies that ratio to the envelope's hold-time fraction
  (`IntruderFDNEngine::setSpacingMultiplier()`), not to discrete tap spacing - see the note on
  early reflections below. Hold-time scaling alone turned out too subtle to hear as a "Smoothing"
  control (a 100ms-scale shift in the hold/decay knee, easy to miss by ear) - the same ratio also
  raises the input diffuser's allpass coefficient (0.5 at Smoothing 0% up to 0.7 at 100%) toward a
  denser, smoother early field, which is a more directly audible reading of "faster echo-density
  buildup" than timing alone. This goes beyond the two measured hardware endpoints (an allpass
  coefficient isn't something findings.md measured at all), unlike every other mapping in this
  section.
- **"Threshold" is NOT on the real hardware** - IMPLEMENTATION.md's control set has no
  sensitivity/threshold knob at all. The plugin's own gate-retrigger detector (a peak follower
  watching the dry input, driving the hold/decay/release state machine in `processStereo()`) needs
  one since a single internal constant can't fit every source's gain staging. It uses hysteresis
  (a Schmitt trigger: Threshold is the rising "new hit" level, a fixed 5dB below it is the falling
  "genuinely stopped" level) rather than one shared threshold - a single shared threshold measurably
  chattered (repeatedly false-retriggered) whenever real program material hovered near it. Push
  Threshold low enough that the falling level drops below wherever a given source's own quiet
  passages actually sit, and the gate stops finding a genuine "off" to detect at all (opens once,
  stays open) - the exposed range (-41..0dB) is trimmed to stay clear of that failure mode on
  typical material, not a hard guarantee for every source.

**Envelope retrigger, and a bug caught by testing with realistic (not just impulse) input:** the
envelope retriggers on a rising input transient - the standard gated-reverb idiom - but a purely
impulse-only test signal never exercises what happens with a *sustained* input. Testing with
`IntruderRenderIR --testSignal sustained` (a held tone, not a hit) found the wet output decaying
to near-silence once and simply staying there for as long as the input kept playing, since nothing
in a continuously-loud signal ever re-crosses the retrigger threshold. Tying release to "input just
stopped" was the wrong fix - confirmed by the same empirical test - since it broke the far more
common case (a short transient's tail should ring out over the *full* Decay time, not get cut short
just because the transient itself was brief). The actual fix: the envelope also auto-restarts
whenever its current cycle has already run to completion *and* the input is still active, so a
single hit is completely unaffected (its cycle always finishes long before input stops) while a
sustained note re-triggers roughly every Decay-length instead of dying out.

Bass/treble tilt (`TiltFilter`) is applied immediately after pre-delay, before the signal reaches
the diffuser feeding the tank - findings.md showed H affects the very first energy in the signal,
not just a slow darkening of the tail, so tilting only the tank-feed path (an earlier version)
measurably undershot the real onset-tilt target.

**No discrete early-reflection taps.** An earlier version fed a fixed multi-tap pattern (matching
findings.md's measured ER taps in the real captures) straight to the output alongside the tank.
However measurement-accurate, discrete taps are just delayed/scaled copies of the input - they read
as audible slapback echoes of the source rather than reverb. Removed entirely rather than turned
down (see `IntruderFDNEngine`'s class comment); the wet output is now purely the diffuser-fed tank.
`analysis/analyze_irs.py` still extracts ER taps from the real captures as a Phase 1 measurement -
that's a property of the reference hardware, independent of how the plugin's own signal path is
built.

Delay-line modulation ("Wobble", always on, unlike shields-reverb's opt-in version) and a widened,
16-line tank (up from an initial 8) both exist specifically to counter FDN resonance at longer
Decay settings - see the tank-sustain note above for why the feedback-gain change mattered more
than either of these on their own.

The UI is the full hardware-panel treatment (see the `juce-hardware-panel-ui` skill; accent colour
`#CFD640`), built from the approved mockup at `mockups/intruder-mockup-v1.html`. Three sections -
Timing (Decay, Pre-Delay, stacked), Character (Low/High, Smoothing, stacked), Levels (Gain/Volume
as a smaller pair, Blend as a larger "hero" knob below, matching the plugin's primary continuous
control per the same hierarchy convention as Caverns' larger L/R Time knobs). No preset combo,
matching shields-reverb's precedent (`getNumPrograms() == 1` here too). `IntruderLookAndFeel` is a
thin subclass of the shared `wildjag::HardwarePanelLookAndFeel` supplying Intruder's accent pair
and the two embedded fonts (shared Oxanium/Oswald from `common/Assets/`, no plugin-specific
typeface).

## Parameters

| Parameter | Range | Default | Description |
|---|---|---|---|
| Decay | 0.3 - 10.0 s | 1.5 s | Tail length, in continuous seconds (not the hardware's compressed 0.1-9.8 label - see "How it works") |
| Pre-Delay | 0 - 100 ms | 0 ms | Delay before the reverb signal starts |
| Low/High | -12 - +3 dB | 0 dB | Bass/treble tilt (the hardware's "H") - negative darkens (bass up, treble down), pivoting ~2kHz |
| Smoothing | 0 - 100% | 0% | Echo-density buildup speed (the hardware's "Tighter") - scales both the envelope's hold time and the input diffuser's density |
| Gain | -24 - +24 dB | 0 dB | Input trim, applied before the reverb engine (and before the gate-retrigger detector sees the signal) |
| Volume | -24 - +24 dB | 0 dB | Output trim, applied after the reverb engine, before the dry/wet blend |
| Threshold | -41 - 0 dB | -36 dB | Input level the gate-retrigger detector needs to see to treat something as a new hit - NOT on the real hardware, see "How it works" |
| Blend | 0 - 100% | 50% | Dry/wet mix |
| Bypass | on/off | off | |

## Project structure

```
intruder-gated-reverb/
├── CMakeLists.txt
├── Source/
│   ├── PluginProcessor.h/.cpp        # Parameter state, dry/wet mix, buffer plumbing
│   ├── IntruderFDNEngine.h/.cpp      # The gated reverb core (see "How it works" above)
│   ├── IntruderParameterMap.h/.cpp   # Maps UI values (H, Tighter) to DSP coefficients via the
│   │                                 # measured reference curves - kept separate from the DSP
│   │                                 # core so the mapping can be refit without touching it
│   ├── IntruderReferenceData.h       # The measured (H, tilt) / Tighter-ratio / decay-label-to-RT60
│   │                                 # points IntruderParameterMap.cpp interpolates between
│   ├── IntruderLookAndFeel.h/.cpp    # Thin subclass of the shared HardwarePanelLookAndFeel
│   ├── PluginEditor.h/.cpp           # Hardware-panel UI (see mockups/intruder-mockup-v1.html)
│   ├── Tests/                        # IntruderTests: headless UnitTest console app (DSP + mapping)
│   └── Tools/RenderIR.cpp            # IntruderRenderIR: offline render console app
├── mockups/intruder-mockup-v1.html   # Approved HTML/CSS mockup the real UI was built from
├── ir-captures/                      # Ground-truth AMS RMX16 "Non-Lin 2" captures
└── analysis/                         # Python analysis/validation pipeline (see above) - its own
                                       # venv, gitignored
```

No `installer/` yet (unlike this catalog's other finished plugins) - not set up for Intruder as of
this writing.

Offline IR comparison/scoring script: `../common/tools/compare_wavs.py` (shared across the catalog,
not plugin-local).

## License

[AGPLv3](../LICENSE), same as the rest of this repo.
