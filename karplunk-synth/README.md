# Karplunk

An extended Karplus-Strong physical-modeling string synth (AU / VST3 / Standalone). This is a
**base scaffold, not a finished instrument**: a correct, stable, 8-voice implementation built
around four clean experimentation seams (excitation source, loop filter, delay-line tuning, and
feedback topology) so each can be swapped later without touching the other three. See "Future
swap-in points" below before extending any of them.

See the [root README](../README.md) for shared build requirements, the exFAT/apostrophe build
gotchas, and running tests across all plugins at once.

**Roadmap**: polyphony (8 voices, basic oldest-voice-stealing - see `KarplunkVoiceAllocator.h`), a
Pluck/Bow excitation morph control, Mutable Instruments Rings-style Structure/Position timbre
controls, and a Poly/Mono switch (`KarplunkMonoNoteStack.h`) are done. Glide/portamento for Mono is
next. No installer, UI polish (mockup-first hardware-panel pass), or preset system yet - all
explicitly out of scope until asked for.

## Building

```sh
cd karplunk-synth
cmake -B build -G Xcode
cmake --build build --config Release --target Karplunk_All
```

To build a single format only: `--target Karplunk_AU`, `Karplunk_VST3`, or `Karplunk_Standalone`.

## Installation

`COPY_PLUGIN_AFTER_BUILD` is enabled, so a successful build automatically copies the plugin into
the standard user plugin directories:

- **AU:** `~/Library/Audio/Plug-Ins/Components/Karplunk.component`
- **VST3:** `~/Library/Audio/Plug-Ins/VST3/Karplunk.vst3`

Restart your DAW (or run AU validation, below) after installing. The Standalone app is built to
`build/Karplunk_artefacts/Release/Standalone/Karplunk.app`.

## Launching the Standalone app

```sh
cmake --build build --config Debug --target Karplunk_Standalone
open build/Karplunk_artefacts/Debug/Standalone/Karplunk.app
```

## Validating the AU (auval)

```sh
auval -v aumu Karp WJag
```

Note the AU type is `aumu` (music device/synth), not `aufx` - Karplunk is `IS_SYNTH TRUE`.

## How it works

The core algorithm (`Source/KarplunkVoice.h`'s `SingleLineKarplunkVoice`) is the classic Karplus-
Strong loop: a delay line seeded with an excitation burst, feeding back through a loop filter that
shapes decay and timbre. Each MIDI note-on computes a delay length from the note's frequency
(`sampleRate / frequencyHz`, no Jaffe/Smith tuning correction yet - see the swap table), primes the
delay line directly with excitation samples (bypassing the loop filter for that initial fill), and
each subsequent sample reads the delay line, runs it through the loop filter, and writes the
result back in - `read -> process -> write`, the same order already used in this catalog's own
`caverns-delay/Source/PluginProcessor.cpp`.

`PluginProcessor` owns a fixed pool of 8 `Voice`s and a `KarplunkVoiceAllocator` (its own small,
framework-free, independently-tested class - see `Source/KarplunkVoiceAllocator.h`) that decides
which voice a new note-on should use: any voice not currently sounding first, or the
oldest-triggered voice if all 8 are busy (basic oldest-voice-stealing, not release-aware). The
summed output of all 8 voices is scaled by a fixed `1/sqrt(8)` headroom factor before Output Level,
so a full chord at max velocity doesn't clip harder than a single note did.

**Pluck / Bow**: a real Karplus-Strong pluck excites the string once and lets it ring freely; a
bowed string is driven by *continuous* stick-slip friction for as long as the bow is drawn, so it
sustains indefinitely while held and only starts decaying once released. Rather than blending two
separate signal paths, the Pluck/Bow knob continuously reshapes **one** excitation generator through
a classic **Attack / Decay / Sustain / Release envelope** (see `NoiseExcitation::nextExcitationSample()`
and its class header comment for the full stage-by-stage design): fast attack and a sustain
level of 0 at 0% (the envelope decays fully away, tied to the note's own period via
`setBaseDuration()` - a pluck); slow attack and a sustain level of 1 at 100% (the envelope settles
into and holds full amplitude indefinitely - a bow). Every stage is a one-pole recurrence (not a
closed-form formula - `bowAmount` is live and can change every sample via `PluginProcessor`'s
smoothing, and a recurrence stays continuous no matter how its own coefficient or target moves
tick-to-tick, where a closed-form re-evaluation would not), and `noteOff()` now triggers a real
Release stage on the excitation itself (ramping to 0 over a bowAmount-dependent time) instead of an
instant cutoff, so a held bow note doesn't click when released. There's no bulk-priming step at
`noteOn()` either - the enveloped generator provides its own energy from the very first sample,
mechanically equivalent to the old bulk-write priming for the whole first lap around the delay line
(`stringLine.read()` returns exactly 0 until the first injected sample has looped around once, so
the loop filter sees silence and the injection reaches the string raw/unfiltered either way).

**This feature has needed four rounds of actually rendering and measuring it, not just reasoning
about it, to get right** - each bug was real, audible, and invisible to unit tests that only check
boundedness/stability rather than the actual loudness curve:

1. The first implementation scaled continuous injection by `sqrt(1 - loopGain)` alone, reasoned
   from treating the loop like a simple single-pole feedback. Wrong by ~30-40dB in practice - a
   held bow note came out roughly 30dB quieter than a plucked note (audible as "the pluck just
   fades out").
2. When that two-path design was collapsed into one unified envelope, interpolating `sqrt(1 -
   loopGain)` alone from 1.0 while leaving it *active* at `bowAmount = 0` crushed a plucked note's
   own peak loudness by up to ~14x at high Decay - the old design's one-shot burst had never been
   compensated at all. Fixed by interpolating the *combined target value*
   (`continuousLevelAnalog * sqrt(1 - loopGain)`, `continuousLevelAnalog` re-tuned to `4.0`) from
   1.0, not the two factors separately.
3. That same envelope design (an attack ramp and a decay-to-silence ramp, both racing toward
   `bowAmount = 1`) interpolated the attack time linearly in time but the decay time linearly in
   its own coefficient - two curves that don't move together, since a decay-coefficient lerp is
   dominated by its fast endpoint until `bowAmount` is within a fraction of a percent of 1.0. The
   result: a loud pluck, a loud full bow, and a dramatic, audible volume drop through most of the
   middle of the range (user report: "from about 2% - 90% of the knob there's a huge volume
   drop"). Fixed by switching to a proper ADSR with an explicit, directly-interpolated
   `sustainLevel = bowAmount` - loudness at any point in time is now governed by that one monotonic
   parameter, not an emergent race between two independently-moving time constants. Verified by
   sweeping `bowAmount` from 0 to 1 and measuring settled RMS at each step (see
   `KarplunkVoiceTests.cpp`'s dead-zone regression test) - confirmed smoothly, strictly increasing
   with no dip anywhere in the range.
4. Even with that fixed, a user report - "as you turn the Pluck/Bow knob, the volume goes up and
   down drastically" - led to a fourth measurement pass that found a *different* phenomenon
   entirely: raw noise driving a high-Q resonant loop naturally produces audible loudness
   fluctuation ("noise through a narrow filter warbles"), confirmed by holding `bowAmount` (and
   every other parameter) perfectly still and measuring window-to-window RMS anyway - up to ~50%
   (~3.4dB) swings with nothing moving at all. Turning the knob wasn't introducing or worsening
   this; it was just making an always-present characteristic audible. Tamed (not eliminated - some
   natural "shimmer" is the correct character for a noise-excited bowed string) with a fast/slow
   envelope-ratio output leveler in `SingleLineKarplunkVoice::renderNextSample()` - it operates on
   a copy of the final output only, never feeding back into the string, so it can't alter the
   loop's own decay/stability/character. Down to ~1.2-1.6dB measured, verified by a permanent
   regression test.

A `tanh()` soft-clip on the injected contribution caps the worst-case crest factor of raw noise
driving a resonant loop; at `bowAmount = 0` it only engages for the (rare) near-full-velocity sample
that would already be near the top of its range, a light, always-on safety softening rather than a
structural change from the original one-shot burst.

**Known, unaddressed limitations:**

- A held bow note's loudness still fluctuates ~1.2-1.6dB window-to-window even after leveling
  (down from ~3.4dB unleveled) - accepted as the resonant loop's natural noise-driven character,
  not chased to zero (a fully flat/leveled bowed tone would sound unnaturally static).
- At `bowAmount = 0`, the pluck's decay is a smooth exponential taper rather than a hard cutoff
  after exactly one note period (an intentional, user-approved character change) - it never reaches
  exact silence, though it's inaudibly quiet within a handful of note periods.
- A high note's pluck peak lands measurably below a low note's for the same velocity (measured
  ~0.76 at C8 vs ~1.10 at A0, both at velocity = 1.0) - short notes have little room between the
  attack ramp finishing and the decay ramp already falling, so the peak never reaches as high as a
  low note's does. Accepted as a modest, pitch-dependent side effect; a smaller `durationMultiplier`
  or a shorter minimum attack could reduce it further if it becomes audible.
- A modest loudness gap remains across the Decay range (measured ~1-7dB, held note quieter at
  higher Decay) - accepted rather than chased further; the loop's real gain is frequency-
  dependent in a way that resists a clean closed-form compensation, and this residual is far
  smaller than the bugs above.
- Steady-state energy also scales with `sqrt(delaySamples)`, so a bowed low note and a bowed high
  note differ in sustained loudness by roughly an order of magnitude at the same Bow/velocity - see
  the swap-in table.
- The per-voice crest factor (a single bowed voice can peak well above its own steady-state RMS)
  isn't additionally limited when multiple bowed voices sum in `PluginProcessor` - `polyHeadroomGain`
  (sized for transient plucked chords) gives some protection but not a worst-case guarantee against
  simultaneous peak alignment across voices. Not observed to be a practical problem, not solved
  here - a real limiter/compressor stage would be the correct fix if it ever is.

**Voice-stealing tradeoff, worth knowing:** before Bow, the worst case was 8 voices simultaneously
*decaying* (bounded by `getTailLengthSeconds() = 8.0`s at max Decay). Bow makes "all 8 voices
genuinely busy indefinitely" a realistic sustained state (e.g. an 8-note held bowed chord with a
sustain pedal), so a 9th note now has meaningfully higher odds of audibly stealing a note the
performer is still holding down, rather than a plucked note's inaudible decay tail. This is an
existing, already-documented tradeoff - release-aware stealing is explicitly out of scope (see the
swap-in table's last row), not something Bow's addition changes the plan for.

**Structure / Position**: two more timbre controls, modeled on Mutable Instruments Rings (open
source, MIT licensed - `rings/dsp/string.cc` in `github.com/pichenettes/eurorack` was read
directly to ground this in the real algorithm, not a secondhand description) - both went through a
real bug and a real fix before landing here.

*Structure* introduces **inharmonicity** - real stiff strings (piano, bell-like material) have
partials that run sharp of exact integer multiples of the fundamental, a physical effect called
dispersion. **The first implementation matched Rings' own technique almost exactly - splitting the
delay into a shortened "main" portion plus one large, variable-length Schroeder allpass - and it
was wrong**: measured by actually tracking the fundamental's pitch across a Structure sweep, it
detuned notes by up to ~95 cents (nearly a semitone), non-monotonically. A large-delay allpass's
group delay is a function of frequency that oscillates rapidly as the delay grows, so "it
contributes about that much delay" only holds by coincidence at any given pitch. The fix - the
actual, standard technique from the physical-modeling literature (Jaffe & Smith 1983; Van Duyne &
Smith 1994) rather than Rings' own shortcut - is a **cascade of small (single-sample-delay)
allpass stages** (`KarplunkDispersionFilter`) instead of one large one: a single-sample allpass's
group delay is smooth and *monotonic* across the whole spectrum, so the cascade's exact
contribution at a note's own fundamental can be computed and compensated for reliably. **Structure
= 0% is still a bit-exact no-op**, protected by the same regression test as before.

The first correctly-tuned version (`numStages = 3`, a conservative gain chosen only to keep the
worst-case note comfortably clear of an internal safety guard) turned out to be too subtle - it
kept every note in tune, but left almost no measurable *difference* in delay across the harmonic
series, i.e. barely any real dispersion, just a small uniform shift. Rendering and measuring actual
per-harmonic pitch (not just the fundamental) showed why: pushing the per-stage gain up toward an
audible effect reintroduced real fundamental mistuning at some notes, but adding more cascaded
stages instead achieved a stronger stretch without pushing any single stage's own nonlinearity as
hard - reaching `numStages = 8` before gain became the binding constraint again.

**Still not pronounced enough at that setting, per the user** - measuring the individual harmonics
(not just how far apart the 1st and 9th partial were) revealed why: with Position's own default
(50%) already cancelling every even harmonic, the audible spectrum is mostly odd harmonics, and the
1st-vs-9th spread was concentrated almost entirely in the 9th partial - quiet, and largely
irrelevant to what's actually heard - while the loud, low-numbered harmonics (3rd, 5th) barely
moved. Pushing gain further (0.5, then 0.6) to stretch those louder harmonics measurably increased
fundamental drift too (up to ~30 cents at gain=0.6 on some notes) - switching the compensation from
group delay to *phase delay* (the mathematically correct quantity for a resonance condition, though
the two coincide closely for small gain) made no measurable difference, which ruled that out as the
cause; the actual true resonant frequency deviates from the simplifying assumption this design
makes in a way it doesn't attempt to solve. Settled on `maxDispersionGain = 0.5` as the practical
ceiling: a real, clean stretch on the loud harmonics (measured ~5-13 cents on the 3rd/5th/7th
partials) while every supported note's fundamental stays within ~12.5 cents of its own baseline at
worst (only right at the very top of the knob, on a few notes) - a small, deliberate compromise
in service of an effect that's actually audible.

*Position* models **exciting/listening to a string at a different point along its length** - the
classic reason two guitars plucked near the bridge vs. near the middle sound different; per the
underlying physics, exciting/reading exactly at the midpoint cancels every *even* harmonic (a
hollow, square-wave-like character). **The first implementation measurably didn't do this**: it
summed a second, shorter tap of the resonating string with the original, full-strength signal -
which sounds like it should blend in the effect, but a phase-shifted read of a periodic signal has
*identical* harmonic magnitudes to the original by mathematical necessity (shifting phase can't
remove energy from a harmonic, only rotate it) - only the *interference* between the two reads can
cancel anything, and the first version used the wrong sign, cancelling odd harmonics instead of
the physically-correct even ones (weak and the wrong character). Fixed by subtracting instead of
adding - verified directly by measuring individual harmonic magnitudes (Goertzel analysis): at
Position = 50%, the 2nd harmonic now drops to near-zero while the fundamental and 3rd harmonic
stay strong, matching the documented "hollow" behavior. Position still has **no neutral/bypass
setting** - every value changes the output to some degree - and the default (50%, the string's
midpoint) is a deliberate musical choice, not a "no effect" one.

**Even the tuned gain=0.5 allpass cascade turned out to be a real DSP effect that still wasn't
perceptible.** Rendered real audio through the actual `KarplunkAudioProcessor` (not just the
isolated Voice class - see `KarplunkProcessorTests`) and had the user listen to a same-note,
Structure-only A/B: tonally indistinguishable. The allpass cascade genuinely does what it claims
(measured, verified two independent ways), but a few cents of harmonic stretch on a decaying pluck
is simply too subtle a cue for a human ear to reliably pick out. Re-reading Rings' `string.cc`
directly with that specific question in mind revealed the answer: **real Rings does not try to
keep the fundamental locked as dispersion increases.** Above 75% dispersion, it deliberately FMs
the delay length itself with lowpassed noise (`delay_fm`) - genuine, intentional pitch instability
is the actual audible "unstable/breaking up" character real hardware relies on at high Structure,
layered on top of (not instead of) the allpass stretch. Ported that mechanism directly (same
formula: `noiseAmount = (4*(structure-0.75))^2 * 0.025`), using a fixed noise-lowpass coefficient
(0.25) rather than coupling it to Karplunk's own Brightness knob, a different, independent control.
Verified this produces a genuine *time-varying* pitch wobble (not just a bigger static shift, which
a single long measurement window could mask) via short consecutive-window pitch tracking within
one held note - confirmed working by the user by ear afterward. Below 75%, Structure is completely
unaffected by this - it's additive, not a retuning of the existing cascade.

Real Rings' Structure range also spans negative "dispersion" (a nonlinear bridge-curving
distortion, sitar-like buzz) - **out of scope for this pass**, since Structure's 0-100% range only
ever corresponds to Rings' *positive* dispersion range, where bridge curving never engages either
(see the swap-in table for a possible future extension).

**Poly / Mono**: an 8-voice pool (Poly, the default) is the original base-scaffold behaviour.
Mono drives a single voice through `KarplunkMonoNoteStack` - classic last-note-priority: the most
recently pressed held note always sounds, and releasing it retriggers whichever earlier note is
still held (hold A, hold B, release B -> A re-plucks) rather than leaving it silently ringing or
cutting to nothing. Toggling the mode mid-performance is treated as an implicit all-notes-off
(`PluginProcessor::processBlock()` detects the change once per block) rather than trying to
reconcile Poly's voice-allocator state with Mono's note stack. Mono also skips the 8-voice
headroom reduction entirely (only one voice ever sounds), so a Mono note isn't quieter than the
same note played in Poly for no reason.

**Four swappable areas**, each isolated so the others never need to change:

1. **Excitation** (`KarplunkExcitation.h`) - "generate one sample of excitation per tick, shaped by
   a live ADSR envelope." Base implementation: `NoiseExcitation`, a brightness-controllable
   lowpassed white-noise generator with a live Pluck/Bow envelope morph (see above).
2. **Loop Filter** (`KarplunkLoopFilter.h`) - "process one sample through the feedback path." Base
   implementation: `TwoPointAverageLoopFilter`, the classic `y[n] = g * 0.5*(x[n] + x[n-1])`
   one-zero averager - brightness-dependent decay is real Karplus-Strong physics here, not a bug
   (see the class's own comment).
3. **Delay Tuning** (`KarplunkStringLine.h`) - fractional-delay interpolation, isolated behind an
   `Interpolator` template parameter. Base implementation: `LinearInterpolator`. This is a
   hand-rolled ring buffer (`std::vector<float>` + a non-consuming `read()`), **not** a wrapper
   around `juce::dsp::DelayLine` - found empirically while building this class that `DelayLine`'s
   `popSample()` is a strictly causal, state-consuming read that cannot support seeding a burst of
   samples before any of them are read back, which is exactly what Karplus-Strong's noteOn priming
   needs (see the class's own comment for the full explanation - it's not obvious from JUCE's own
   header). Also offers `readAt()`, a second, stateless read at any explicit delay length - what
   makes Position's alternate string tap and Structure's shortened main-tap read possible without
   disturbing the delay length that sets the note's pitch.
4. **Feedback Topology** (`KarplunkVoice.h`) - signal routing. The base scaffold is
   `SingleLineKarplunkVoice`, a single delay line in a loop. Unlike the other three areas, a new
   topology (e.g. dual cross-coupled lines) is a **new class**, not a template parameter, since it
   changes member layout, not just behaviour - see the swap table.

No polymorphism (no `virtual`, no `std::function`-as-strategy) is used anywhere - all three
per-sample seams are compile-time template parameters on `SingleLineKarplunkVoice`, matching this
catalog's established DSP style: small, concrete, framework-free classes composed by value (see
`gradient-pitch/Source/GradientDelayBuffer.h` / `GradientPitchShiftEngine.h`), which is also why
`KarplunkStringLine` ended up hand-rolled the same way `GradientDelayBuffer` is, rather than
wrapping a JUCE class as originally planned.

## Future swap-in points

| Area                                        | What changes                                                                                 | Real-time implication                                                                                                                                                                                                                                                                                                                                      |
| ------------------------------------------- | -------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Excitation                                  | Noise -> filtered noise / sample burst; also, a held bow note's loudness could gain a `/ sqrt(delaySamples)` term to flatten the still-unaddressed pitch-dependent sustained-loudness gap | None - `nextExcitationSample()` is a bounded per-tick call with fixed-size state, no scratch buffer needed even for a variant with a longer/continuous shape.                                                                                                                                                                                              |
| Loop Filter                                 | Two-point average -> one-pole/comb/resonant/asymmetric                                       | Fixed bounded state (a few extra floats) - size in `prepare()`. A comb/resonant filter needing its own tap needs that tap preallocated the same way `KarplunkStringLine` is.                                                                                                                                                                               |
| Delay Tuning                                | Linear -> higher-order (Lagrange-style) interpolation. The Jaffe/Smith dispersion technique originally anticipated here is now built (`KarplunkDispersionFilter`, driving the Structure control - a cascade of small allpass stages, not `KarplunkStringLine`-backed at all any more), plus Rings-accurate noise-driven delay-length FM above Structure=75% (see "How it works" below) - not as an `Interpolator` swap, but as a separate class composed by value in `SingleLineKarplunkVoice`, closer in shape to a Feedback Topology addition. A future extension: Structure's negative-dispersion range (nonlinear "bridge curving" distortion, present in Rings for negative dispersion values only) - out of scope here since Structure's 0-100% range only ever corresponds to Rings' *positive* dispersion range, where bridge curving never engages either. | A pure-function interpolator (Linear, Lagrange) is a free template-argument swap, no new state. `KarplunkDispersionFilter`'s per-stage state is a handful of fixed-size floats - no delay line/ring buffer at all, real-time safe by construction. |
| Feedback Topology                           | Single loop -> dual cross-coupled lines -> nonlinear waveshaping in the loop                 | Dual cross-coupled = a **new class** reusing the same three area-components by value, ~2x buffer footprint (still trivial - see `SingleLineKarplunkVoice::requiredCapacitySamples()`'s sizing table in its own comment) + a small fixed cross-mix matrix. Waveshaping in the loop adds only per-sample math, no new buffering.                             |
| More voices / a different stealing strategy | `numVoices` constant -> a larger pool; basic oldest-voice-stealing -> release-aware stealing | `KarplunkVoiceAllocator<N>`'s array members grow with `N`, still fixed-size and stack/member-allocated, no runtime allocation. Release-aware stealing (prefer stealing an already-released note over one still held) would need `KarplunkVoiceAllocator` to also track release state, not just age - a real but bounded change confined to that one class. |

## Parameters

| Parameter        | Range        | Default | Description                                                                                                                                                                                  |
| ---------------- | ------------ | ------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Decay            | 0 - 100%     | 60%     | Loop gain - controls sustain length. Decay time is pitch-dependent at a fixed setting (higher notes decay faster in real time) - see `TwoPointAverageLoopFilter::processSample()`'s comment. |
| Output Level     | -60 to +6 dB | -6 dB   | Post-voice output gain                                                                                                                                                                       |
| Pluck Brightness | 0 - 100%     | 100%    | Excitation tone - 0 is heavily lowpassed noise, 100% is raw white noise. Only takes effect on the next pluck.                                                                                |
| Pluck / Bow      | 0 - 100%     | 0%      | 0% is a pure pluck (the original base-scaffold behaviour). 100% is a pure bow - continuous excitation sustains the note for as long as it's held, decaying only after release. Live-adjustable, unlike Brightness. See "How it works" above.                                                                                |
| Structure        | 0 - 100%     | 0%      | Inharmonicity/dispersion - 0% is a bit-exact no-op (pure harmonic partials), 100% is maximally stretched/metallic. Live-adjustable. See "How it works" above.                                                                                |
| Position         | 0 - 100%     | 50%     | Where the string is excited/listened to - 50% (the midpoint) is a hollower, more harmonic character; the ends are fuller. No neutral/bypass value - every setting changes the output. Live-adjustable. See "How it works" above.                                                                                |
| Mono             | Off / On     | Off     | Off (Poly) is the original 8-voice-pool behaviour. On (Mono) drives a single voice with classic last-note-priority: holding two notes sounds only the most recent, and releasing it retriggers whichever earlier note is still held, rather than leaving it silently ringing or cutting to silence. See `KarplunkMonoNoteStack.h`. |

Pitch is MIDI-driven, not a knob. No dry/wet (a self-generating voice has no dry signal to blend
against yet - see the "How it works" section). No Glide yet (every note-on/retrigger is a fresh
pluck, not a pitch to glide toward - a natural next addition specifically for Mono, not yet built).

## Project structure

```
karplunk-synth/
├── CMakeLists.txt
├── Source/
│   ├── KarplunkExcitation.h/.cpp   # Excitation seam: NoiseExcitation (pluck burst + bow morph)
│   ├── KarplunkLoopFilter.h/.cpp   # Loop Filter seam: TwoPointAverageLoopFilter
│   ├── KarplunkStringLine.h        # Delay Tuning seam: hand-rolled ring buffer, template Interpolator
│   ├── KarplunkVoice.h             # Feedback Topology (base case): SingleLineKarplunkVoice,
│   │                                 # + KarplunkDispersionFilter (Structure's allpass primitive)
│   ├── KarplunkVoiceAllocator.h    # Voice-to-note allocation/oldest-voice-stealing for the pool (Poly)
│   ├── KarplunkMonoNoteStack.h     # Last-note-priority/retrigger note tracking for Mono mode
│   ├── PluginProcessor.h/.cpp      # Parameter state, MIDI dispatch, owns the 8-voice pool
│   ├── PluginEditor.h/.cpp         # Minimal functional UI (no hardware-panel mockup pass yet)
│   ├── KarplunkLookAndFeel.h/.cpp  # Thin subclass of the shared HardwarePanelLookAndFeel (theme only)
│   ├── KarplunkBuildNumber.h       # Incrementing marker shown in the editor, so a stale/cached
│   │                                 # plugin build is visible from the UI itself
│   └── Tests/                      # KarplunkTests: headless UnitTest console app (DSP seams only);
│                                     # KarplunkProcessorTests: drives the real KarplunkAudioProcessor
│                                     # end-to-end (APVTS/MIDI/processBlock), not just the DSP seams
```

## License

[AGPLv3](../LICENSE), same as the rest of this repo.
