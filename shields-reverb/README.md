# Shields

*Diffuse Reverb.* (Highlight colour: `#D74377`.)

A diffuse algorithmic reverb (AU / VST3 / Standalone) emulating the Alesis Midiverb II "Bloom"
algorithm (presets 45 and 49): energy density builds slowly before decaying, rather than a
discrete-tap swelling delay or an envelope applied to a normal reverb tail. The buildup emerges
from a bank of short feedback combs ahead of an 8-line, Hadamard-mixed feedback delay network
(FDN) tank - see "How it works" below for why the buildup needed that burst stage specifically,
and what "buildup" actually means here. Default parameters are tuned against real Midiverb II
captures (`reference-irs/`, scored via `tools/compare_irs.py`): ~0.94 envelope correlation against
both preset 45 and preset 49 at time of writing.

See the [root README](../README.md) for shared build requirements, the exFAT/apostrophe build
gotchas, and running tests across all plugins at once.

## Building

```sh
cd shields-reverb
cmake -B build -G Xcode
cmake --build build --config Release --target Shields_All
```

To build a single format only: `--target Shields_AU`, `Shields_VST3`, or `Shields_Standalone`.

## Installation

`COPY_PLUGIN_AFTER_BUILD` is enabled, so a successful build automatically copies the plugin into
the standard user plugin directories:

- **AU:** `~/Library/Audio/Plug-Ins/Components/Shields.component`
- **VST3:** `~/Library/Audio/Plug-Ins/VST3/Shields.vst3`

Restart your DAW (or run AU validation, below) after installing. The Standalone app is built to
`build/Shields_artefacts/Release/Standalone/Shields.app`.

## Launching the Standalone app

```sh
cmake --build build --config Debug --target Shields_Standalone
open build/Shields_artefacts/Debug/Standalone/Shields.app
```

## Validating the AU (auval)

```sh
auval -v aufx Shld WJag
```

## Offline validation workflow

This plugin's DSP is tuned against real hardware captures rather than by ear alone. Two extra
pieces support that, on top of the usual `ShieldsTests` unit-test target:

1. **`reference-irs/`** - ground-truth Midiverb II impulse response captures. See that folder's own
   README for expected filenames. Never read at runtime; the algorithm is real-time and parametric,
   not a convolution reverb.
2. **`ShieldsRenderIR`** (a console app, not a plugin format) - feeds an impulse through the real
   `ShieldsAudioProcessor` and writes the result to WAV:

   ```sh
   cmake --build build --config Release --target ShieldsRenderIR
   build/ShieldsRenderIR_artefacts/Release/ShieldsRenderIR --out rendered-irs/mine.wav --seconds 4
   ```

   Run with no parameter flags, this renders the plugin's actual current defaults (flags override
   individual parameters, e.g. `--feedback 90 --size 1.5`, for A/B testing against a fixed
   baseline). Rerunning to the same `--out` path correctly overwrites it - `File::createOutputStream()`
   appends by default, which silently corrupted comparisons during tuning until this tool started
   deleting the target file first; worth knowing if you ever touch this file.
3. **`tools/compare_irs.py`** - scores a rendered IR against a reference one (RMS envelope overlay,
   echo-density-over-time overlay, spectrogram, envelope correlation, log-spectral distance):

   ```sh
   python3 -m venv .venv && source .venv/bin/activate && pip install -r tools/requirements.txt
   python3 tools/compare_irs.py rendered-irs/mine.wav reference-irs/preset-45.wav
   ```

Rerun steps 2-3 after every parameter/topology change while tuning - that's the point of having
them as scripts rather than one-off checks.

## How it works

The core is `ShieldsFDNEngine`, in two stages:

1. **A burst comb bank** (6 short, mutually-prime feedback combs per channel, fed by a 3-stage
   input allpass diffuser) - this is what actually produces the audible swell. Each comb turns the
   input into its own train of exponentially-decaying repeats; summed across several mutually-prime
   delay lengths (no single repeat rate dominating into a metallic ring), the windowed RMS of that
   sum genuinely rises for a while as more repeats overlap, before falling once decay outpaces new
   overlap. Its own decay time - independent of the main tank's much longer tail - is scaled by
   `Size`, exactly matching the spec's "Size is the de facto attack-time control."
2. **The main tank**: 8 delay lines (mutually prime lengths, so no periodic reinforcement/metallic
   ringing), mixed every sample by a fixed 8x8 Hadamard matrix (energy-preserving, so stability is
   governed purely by the scalar Feedback gain and the per-line one-pole Damping filter, not by the
   matrix). This is responsible for the long decay tail, not the attack.

No delay-line modulation anywhere in either stage - deliberately static/unmodulated, matching the
original hardware's grainy character.

**Delay lengths are deliberately non-integer milliseconds.** They started out as whole-ms values
(easy to read, still mutually prime as integers) - 19/23/29/31/37/41/43/47 for the tank,
13/37/61/89/113/149 for the burst bank - but that turned out to be its own bug: any comb filter
whose period is exactly N whole milliseconds has a tooth at its own Nth harmonic landing almost
exactly on 1000Hz (N cycles at 1000Hz = N ms = one full period), regardless of N. Since every line
in both stages was a whole-ms value, all 14 of them reinforced each other at 1000/2000/3000Hz etc
on top of each other - confirmed by rendering each burst line in isolation (feedback=0, every other
line silenced) and finding the same ~1000Hz peak 30-40dB above the noise floor regardless of which
single line was active, versus real Midiverb captures in `reference-irs/` whose peaks never exceed
~12dB. Mutual primality of the *millisecond* values never protected against this - it's an
alignment with the sample rate itself, not between the lines. Nudging every length off the
whole-ms grid (see `baseLineLengthsMs`/`baseBurstLengthsMs` in `ShieldsFDNEngine.h`) fixed it, and as
a side effect improved the match against both reference IRs (envelope correlation 0.937->0.943,
log-spectral distance ~6.7dB->~5.1dB) - the coincidental ringing wasn't just audible, it was also
pulling the model away from the real hardware's own (much smoother) spectrum. A max-gain cap on the
burst bank (`maxBurstGain`) stayed in as a secondary safeguard against any one line's feedback gain
creeping too close to instability, though it turned out to be a minor contributor next to this.

**Why two stages, not one:** an orthogonal (energy-preserving) cross-mix scaled by a single scalar
feedback gain is provably front-loaded - its raw RMS envelope can only ever be highest at the
moment of injection and fall from there (this is a real mathematical property of that topology, not
a tuning issue). An early version relied on the tank alone, on the theory that *echo density* -
not raw RMS - was Bloom's real signature; that produced measurable density growth but, once real
reference IRs were available to check against, the hardware's own RMS envelope turned out to
genuinely swell too, not just its density. The burst stage above is what closes that gap: it's a
real per-sample recursive filter driven by the actual input, not a precomputed gain curve, so it
doesn't run afoul of "don't fake the swell with an envelope."

Lo-fi coloration (bandwidth-limiting lowpass + bit-depth quantization) is applied to the wet output
after the FDN. Defaults (Damping 20%, Bandwidth 19kHz, Bit Depth 13) came out of sweeping each
parameter against `tools/compare_irs.py`'s log-spectral-distance score on both reference IRs -
notably, both reference captures scored *worse* under heavier damping/narrower bandwidth than the
spec's "~15kHz, fairly damped" assumption suggested; a little bit-depth grain (not none) did help.
See `PluginProcessor.cpp`'s `createParameterLayout()` for the per-parameter notes.

**Fixed output EQ:** a low-shelf (350Hz, +7dB) and high-shelf (7kHz, -5dB), always active and not
user-exposed, correcting a broadband tonal gap between this topology's raw output and the real
hardware - found by extending `compare_irs.py` with a frequency-resolved spectral-difference plot
(see that script's own comments). Two things had to happen before this correction meant anything:
first, matching overall level between the two signals before comparing tone at all (an unmatched
level offset otherwise shows up as a uniform shift across every band, masking whatever the real
shape difference is); second, comparing 1/3-octave-*smoothed* energy rather than raw FFT bins - two
different delay-network topologies have resonant peaks at different exact frequencies, so a raw
bin-by-bin comparison is dominated by peak-vs-null misalignment noise (confirmed empirically: the
raw per-bin difference curve was spiky +-20-40dB bin to bin) rather than genuine broadband colour.
Once smoothed, the actual gap was clear and much smaller than the raw comparison suggested: light
in the bass below ~400Hz, roughly balanced through the mids, excessive above ~6-8kHz - exactly what
the shelf pair targets. Not exposed as parameters since they're compensating for an inherent
character gap between this topology and the real unit, not something a player would want to sweep.

**Wobble (optional, off by default):** an 8-line FDN at high Feedback has real, audible resonant
peaks - confirmed both in this engine's own rendered IRs (peaks up to ~35-40dB above the noise
floor at typical settings) and, less severely, in the real Midiverb captures themselves (~8-12dB
peaks) - so a *little* of this is authentic small-FDN/hardware character, not a bug. How prominent
it got here traced to the burst/tank delay lengths originally being whole milliseconds (see
`ShieldsFDNEngine.h`'s `baseBurstLengthsMs` comment for the full story - fixed by nudging every length
off that grid) and to the burst bank's shortest line sitting close to its stability ceiling (fixed
with `maxBurstGain`). What's left after both of those fixes is the genuine, static-topology
resonance the original spec anticipated when it flagged "optional modulation" as a possible later
addition. Wobble is that addition: a slow (<0.4Hz per line), small (~1.5ms depth at 100%), mutually-
detuned sinusoidal drift on each of the main tank's 8 read positions (not the burst bank - its lines
are short-lived transients, not where a sustained resonance would live), blurring those peaks into
motion. At 0% it's not just "very quiet" - the read path never even switches to fractional
interpolation, so the engine renders bit-identically to how it did before Wobble existed. At 100% on
a representative high-Feedback setting, measured peak prominence drops from ~35dB to ~20dB (median
~15dB to ~9dB, close to the real hardware's own ~7dB median) - see `ShieldsFDNEngine::setWobble()`'s
comment for the mechanism.

The UI is the full hardware-panel treatment (see the `juce-hardware-panel-ui` skill; accent colour
`#D74377`), built from the approved mockup at `mockups/shields-mockup-v1.html` after the core
algorithm was validated against the reference IRs. Five sections - Diffusion (Diffusion/Size),
Decay (Feedback/Damping), Tone (Bandwidth/Bit Depth), Motion (Wobble, a single knob), Mix
(independent Dry/Wet knobs, matching Caverns' Dry/Wet convention - not a single crossfading Mix
parameter). `ShieldsLookAndFeel` is a thin subclass of the shared `wildjag::HardwarePanelLookAndFeel`
supplying Shields's accent pair and the two embedded fonts (shared Oxanium/Oswald from
`common/Assets/`, no plugin-specific typeface).

## Parameters

| Parameter | Range | Default | Description |
|---|---|---|---|
| Diffusion | 0.3 - 0.7 | 0.5 | Input allpass diffuser coefficient; centered near 0.5 per the Bloom spec |
| Feedback | 0 - 100% | 99% | Decay/tail-length control (internally capped below unity gain) |
| Size | 0.25x - 4.0x | 1.0x | Scales the burst stage and FDN's delay line lengths - the de facto attack-time control |
| Damping | 0 - 100% | 20% | Per-line feedback-path one-pole lowpass - controls HF decay rate |
| Bandwidth | 1kHz - 20kHz | 19kHz | Output lowpass cutoff (lo-fi bandwidth limit) |
| Bit Depth | 4 - 16 bit | 13 bit | Output quantization depth (lo-fi grain) |
| Wobble | 0 - 100% | 0% | Optional main-tank delay-line modulation - blurs resonant peaks into motion; off by default, renders bit-identically to no-Wobble at 0% |
| Dry | 0 - 100% | 100% | Dry signal gain |
| Wet | 0 - 200% | 40% | Wet (processed) signal gain - independent of Dry, can exceed unity |
| Bypass | on/off | off | |

Feedback/Damping/Bandwidth/Bit Depth defaults are the result of tuning against `reference-irs/`
(see "How it works" above) - Diffusion/Size stayed at their spec-suggested values since sweeping
them didn't improve the match further.

## Project structure

```
shields-reverb/
├── CMakeLists.txt
├── Source/
│   ├── PluginProcessor.h/.cpp   # Parameter state, dry/wet mix, buffer plumbing
│   ├── ShieldsFDNEngine.h/.cpp    # The diffuse reverb core (see "How it works" above)
│   ├── ShieldsLookAndFeel.h/.cpp  # Thin subclass of the shared HardwarePanelLookAndFeel
│   ├── PluginEditor.h/.cpp      # Hardware-panel UI (see mockups/shields-mockup-v1.html)
│   ├── Tests/                   # ShieldsTests: headless UnitTest console app (DSP core only)
│   └── Tools/RenderIR.cpp       # ShieldsRenderIR: offline IR-render console app
├── mockups/shields-mockup-v1.html  # Approved HTML/CSS mockup the real UI was built from
├── reference-irs/                # Ground-truth Midiverb II captures (see its own README)
├── rendered-irs/                 # ShieldsRenderIR output (gitignored, regenerable)
├── tools/compare_irs.py          # Offline IR comparison/scoring script
└── installer/                    # This plugin's .pkg installer (see installers/README.md)
```

## License

[AGPLv3](../LICENSE), same as the rest of this repo.
