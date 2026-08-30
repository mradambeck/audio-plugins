# Strike

An extended Karplus-Strong physical-modeling string synth (AU / VST3 / Standalone). This is a
**base scaffold, not a finished instrument**: a correct, stable, 8-voice implementation built
around four clean experimentation seams (excitation source, loop filter, delay-line tuning, and
feedback topology) so each can be swapped later without touching the other three. See "Future
swap-in points" below before extending any of them.

See the [root README](../README.md) for shared build requirements, the exFAT/apostrophe build
gotchas, and running tests across all plugins at once.

**Roadmap**: polyphony (8 voices, basic oldest-voice-stealing - see `StrikeVoiceAllocator.h`), a
Pluck/Bow excitation morph control, Mutable Instruments Rings-style Structure/Position timbre
controls, a Poly/Mono switch (`StrikeMonoNoteStack.h`), Mono Glide/portamento, a Waveshaper
(`StrikeWaveshaper.h`, Fold/Fuzz/Saturate/BitCrush via a runtime dropdown), an in-loop Ring
Modulator (`StrikeRingModulator.h`), a Dual Cross-Coupled Feedback Topology (a live "Single/
Dual" dropdown alongside Cross-Couple, Couple Delay, and Detune controls - see `StrikeVoice.h`),
and a Resonant Loop Filter (a live "Loop Filter Type" dropdown alongside Resonance and Formant
Freq controls - see `StrikeLoopFilter.h`) are done, along with the hardware-panel UI, factory
presets, and the installer.

## Building

```sh
cd strike-synth
cmake -B build -G Xcode
cmake --build build --config Release --target Strike_All
```

To build a single format only: `--target Strike_AU`, `Strike_VST3`, or `Strike_Standalone`.

## Installation

`COPY_PLUGIN_AFTER_BUILD` is enabled, so a successful build automatically copies the plugin into
the standard user plugin directories:

- **AU:** `~/Library/Audio/Plug-Ins/Components/Strike.component`
- **VST3:** `~/Library/Audio/Plug-Ins/VST3/Strike.vst3`

Restart your DAW (or run AU validation, below) after installing. The Standalone app is built to
`build/Strike_artefacts/Release/Standalone/Strike.app`.

## Launching the Standalone app

```sh
cmake --build build --config Debug --target Strike_Standalone
open build/Strike_artefacts/Debug/Standalone/Strike.app
```

## Validating the AU (auval)

```sh
auval -v aumu Strk WJag
```

Note the AU type is `aumu` (music device/synth), not `aufx` - Strike is `IS_SYNTH TRUE`.

## How it works

The core algorithm (`Source/StrikeVoice.h`'s `StrikeStringLineChannel`, one per string - see
below for how `StrikeVoice` composes one or two of these) is the classic Karplus-
Strong loop: a delay line seeded with an excitation burst, feeding back through a loop filter that
shapes decay and timbre. Each MIDI note-on computes a delay length from the note's frequency
(`sampleRate / frequencyHz`, no Jaffe/Smith tuning correction yet - see the swap table), primes the
delay line directly with excitation samples (bypassing the loop filter for that initial fill), and
each subsequent sample reads the delay line, runs it through the loop filter, and writes the
result back in - `read -> process -> write`, the same order already used in this catalog's own
`caverns-delay/Source/PluginProcessor.cpp`.

`PluginProcessor` owns a fixed pool of 8 `Voice`s and a `StrikeVoiceAllocator` (its own small,
framework-free, independently-tested class - see `Source/StrikeVoiceAllocator.h`) that decides
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
   `StrikeVoiceTests.cpp`'s dead-zone regression test) - confirmed smoothly, strictly increasing
   with no dip anywhere in the range.
4. Even with that fixed, a user report - "as you turn the Pluck/Bow knob, the volume goes up and
   down drastically" - led to a fourth measurement pass that found a *different* phenomenon
   entirely: raw noise driving a high-Q resonant loop naturally produces audible loudness
   fluctuation ("noise through a narrow filter warbles"), confirmed by holding `bowAmount` (and
   every other parameter) perfectly still and measuring window-to-window RMS anyway - up to ~50%
   (~3.4dB) swings with nothing moving at all. Turning the knob wasn't introducing or worsening
   this; it was just making an always-present characteristic audible. Tamed (not eliminated - some
   natural "shimmer" is the correct character for a noise-excited bowed string) with a fast/slow
   envelope-ratio output leveler in `StrikeVoice::renderNextSample()` (voice-level, not per-string,
   since a voice only ever has one audible output stream) - it operates on
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
allpass stages** (`StrikeDispersionFilter`) instead of one large one: a single-sample allpass's
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
perceptible.** Rendered real audio through the actual `StrikeAudioProcessor` (not just the
isolated Voice class - see `StrikeProcessorTests`) and had the user listen to a same-note,
Structure-only A/B: tonally indistinguishable. The allpass cascade genuinely does what it claims
(measured, verified two independent ways), but a few cents of harmonic stretch on a decaying pluck
is simply too subtle a cue for a human ear to reliably pick out. Re-reading Rings' `string.cc`
directly with that specific question in mind revealed the answer: **real Rings does not try to
keep the fundamental locked as dispersion increases.** Above 75% dispersion, it deliberately FMs
the delay length itself with lowpassed noise (`delay_fm`) - genuine, intentional pitch instability
is the actual audible "unstable/breaking up" character real hardware relies on at high Structure,
layered on top of (not instead of) the allpass stretch. Ported that mechanism directly (same
formula: `noiseAmount = (4*(structure-0.75))^2 * 0.025`), using a fixed noise-lowpass coefficient
(0.25) rather than coupling it to Strike's own Brightness knob, a different, independent control.
Verified this produces a genuine *time-varying* pitch wobble (not just a bigger static shift, which
a single long measurement window could mask) via short consecutive-window pitch tracking within
one held note - confirmed working by the user by ear afterward. Below 75%, Structure is completely
unaffected by this - it's additive, not a retuning of the existing cascade.

Real Rings' Structure range also spans negative "dispersion" (a nonlinear bridge-curving
distortion, sitar-like buzz) - **out of scope for this pass**, since Structure's 0-100% range only
ever corresponds to Rings' *positive* dispersion range, where bridge curving never engages either
(see the swap-in table for a possible future extension).

**Poly / Mono**: an 8-voice pool (Poly, the default) is the original base-scaffold behaviour.
Mono drives a single voice through `StrikeMonoNoteStack` - classic last-note-priority: the most
recently pressed held note always sounds, and releasing it retriggers whichever earlier note is
still held (hold A, hold B, release B -> A re-plucks) rather than leaving it silently ringing or
cutting to nothing. Toggling the mode mid-performance is treated as an implicit all-notes-off
(`PluginProcessor::processBlock()` detects the change once per block) rather than trying to
reconcile Poly's voice-allocator state with Mono's note stack. Mono also skips the 8-voice
headroom reduction entirely (only one voice ever sounds), so a Mono note isn't quieter than the
same note played in Poly for no reason.

**Glide** (Mono-only): a legato retrigger between two held notes still fires a fresh pluck (the
chosen design - not a true "no re-pluck" legato glide), but the PITCH approaches its new target
smoothly over Glide Time rather than jumping instantly, so the fresh pluck's own attack transient
audibly bends in pitch. A fresh note struck from silence is unaffected regardless of Glide Time -
there's nothing to glide from, matching standard "auto-glide" convention on hardware mono synths
(`StrikeVoice::noteOn()` checks `isActive()` itself to tell the two cases apart).
Smoothed in PITCH space (a one-pole toward the target MIDI note, converted to delay samples every
tick), not delay-samples space directly - delay length and pitch aren't linearly related, so a
one-pole on delay samples wouldn't give the familiar decelerating-as-it-approaches-the-target
portamento character a real analog circuit (an RC-smoothed 1V/oct control voltage) produces; a
one-pole in pitch space does. Verified through the real processor: a 300ms glide from C4 to G4
measured 268.9Hz / 306.2Hz / 386.8Hz at 10ms / 150ms / 1.5s in, matching the closed-form one-pole
trajectory almost exactly. Defaults to 0ms (off) - preserves Mono's exact instant-retrigger
behavior until explicitly dialed in.

**Waveshape**: nonlinear wavefolding (`StrikeWaveFolder`) inside the feedback loop itself, not
applied to the output afterward - the whole point is that the distortion becomes part of what the
string is actually resonating with, compounding every pass around the loop rather than a one-shot
effect. Chosen over soft saturation or hard/asymmetric clipping (the other two options discussed)
specifically because folding reflects the signal back on itself past a threshold rather than
compressing/flattening it, producing denser, often inharmonic-sounding overtones - the most
dramatic timbral departure from a physically-plausible plucked string of the three. Implemented as
a closed-form triangle-wave fold (`threshold * (2/pi) * asin(sin(driven * pi/(2*threshold)))`, no
iterative "reflect until in range" loop), which makes the output *unconditionally* bounded to
`+-threshold` for any input magnitude - the loop's own recirculating energy cannot blow up through
this stage, however hard it's driven. 0% is a bit-exact no-op (the waveshaper is never even called
at that setting).

**Went through two full measure-and-fix rounds before landing here, neither guessable from the
formula alone.** First: at high drive, any signal quiet enough to never actually reach the fold
point still passes through a genuine linear gain boost baked into the pre-fold scaling - sitting
inside the loop, that extra gain compounded every pass, measured making a sustained note ~4x
louder at Waveshape=100% than at 0% (at maximum Decay/Bow) rather than just differently colored.
Fixed by dividing the folded result back down by the same drive factor before writing it back into
the loop - quiet/never-folded content returns to near-unity gain (the two effects cancel).

Second, after the user asked for the fold to be pushed much further (measured how far it was
actually reaching first: a typical bowed note peaks around 0.3-1.6 before waveshaping, and the
original `maxDrive=8` only pushes that into the first reflection or two - a decaying pluck's quiet
tail, around 0.01-0.07, barely reaches the fold point at all) - `maxDrive` was raised 8->32 for a
much denser fold across a much wider range of playing dynamics, and the SAME `/drive` compensation
that fixed the loop-safety bug turned out to crush the *audible* fold almost to silence at this
higher drive (a different problem from the first one, not the same bug returning). The fix
separates the two concerns explicitly rather than re-tuning one shared number: the signal written
back into the loop still gets FULL drive compensation (`driveCompensation=1` - this is what
actually needs to stay safe against loop-gain runaway), while the signal used for the audible
output gets none (`driveCompensation=0` - output is never fed back, so there's no runaway risk to
guard against, only "does it sound right," and full compensation there was measured crushing the
effect specifically). Both calls share the identical fold curve/character - only how much of a
loud, genuinely-folding signal's loudness is handed back to the listener differs. Verified this
didn't reintroduce the original runaway bug: the worst case (max Decay, full Bow) now measures
~3x louder at Waveshape=100%, not ~4x-and-compounding - a real, accepted, bounded loudness increase
at the most extreme setting, not the unbounded-feeling kind the first fix eliminated.

**Third: the user reported, correctly, that Waveshape sounded like it was only coloring the
initial pluck/bow trigger, not the ongoing resonance** - confirmed by measuring the settled decay
tail in isolation (a plucked note, well after its own excitation burst has died away) and finding
its harmonic content essentially unaffected by Waveshape. The cause: a decaying Karplus-Strong
note spends nearly all its life at an amplitude far below any FIXED drive's fold threshold once
the initial excitation has died away (measured ~0.0004 mid-decay vs. 0.3-1.6 at the loud initial
transient) - a static `maxDrive`, however high, can only ever fold whatever's currently louder
than roughly `threshold/drive`, so in practice only the loud initial hit was ever genuinely folded.
Built and measured a fix for this (envelope-following drive normalization - a one-pole follower
tracking the note's own recent level, boosting quiet moments back toward the fold threshold before
folding, the classic "VCA/envelope into a wavefolder" West Coast synthesis patch) and confirmed it
worked exactly as intended (verified via the settled tail's harmonic BALANCE, not just magnitude,
actually shifting) - then reverted it at the user's explicit request: they found the plain,
fixed-drive character (fold strongest at the loud initial hit, cleaner as the note settles) more
musically usable than "folds throughout the whole decay." Kept as a documented, working, available
option (see `StrikeWaveFolder`'s own header comment and git history) rather than discarded - a
deliberate creative call, not a bug fix that didn't pan out.

**Waveshaper Type**: a runtime dropdown (Fold / Fuzz / Saturate / BitCrush), not a rebuild-to-change
compile-time swap like the other three seams - the user asked for a live selector, so
`StrikeStringLineChannel` owns all four concrete classes by value and branches on the choice each
sample (no virtual dispatch/`std::function`, matching this project's zero-polymorphism convention -
see `StrikeWaveshaper.h`'s own comment). `StrikeFuzz` is the "hard/asymmetric clipping" option
from the original three-way
discussion (soft saturation, hard/asymmetric clipping, folding) that led to `StrikeWaveFolder` -
heavy, asymmetric tanh clipping, the classic transistor-fuzz-pedal (Fuzz Face/Big Muff family)
character: dense odd harmonics from the saturation itself, plus even harmonics from clipping the
positive and negative half-cycles at different effective gains (a real single-ended transistor
stage doesn't treat both polarities identically, and Strike's asymmetry deliberately mimics
that). Genuinely different in KIND from folding, not just degree - clipping compresses/flattens
toward a ceiling (monotonic), where folding reflects back down past its own threshold - confirmed
directly via a permanent test rather than assumed from the two formulas looking different. Built
the driveCompensation split into `StrikeFuzz` from the start (full compensation for what
recirculates, little for what's heard) rather than waiting to rediscover the same loudness bug
`StrikeWaveFolder` needed two rounds to fix - tanh's own smooth saturation is unconditionally
bounded to +-1 the same way the fold's asin/sin curve is, so the same safety argument applies
without modification.

**The first version of Fuzz was, per the user, "very hissy"** - a real, expected consequence of
heavy clipping applied to a noise-EXCITED string (Strike's excitation is filtered white noise,
not a pure tone): clipping generates dense harmonic/intermodulation energy all the way up toward
Nyquist, and a plain tanh clip has nothing to tame that. Real fuzz pedals almost universally follow
their clip stage with a tone-shaping capacitor for exactly this reason - this wasn't a workaround,
it was the missing other half of a standard fuzz circuit. A single one-pole lowpass at 6kHz was
tried first and MEASURED (a raw-WAV DFT read back independently in Python, not just reasoning about
the coefficient) to still leave ~51% of the settled tail's spectral energy above 6kHz - a one-pole's
-6dB/octave rolloff is too gentle against how much a hard clip dumps near Nyquist, even though
"6kHz lowpass" sounds like it should obviously fix hiss. Fixed by cascading two identical one-pole
stages (-12dB/octave) at a lower 3kHz cutoff (`StrikeFuzz::updateFilter()`) - re-measured at
~93%/7% low/high energy split, a real, verified fix rather than a plausible-sounding one. The
filter operates on the shaped (post-clip, pre-divide) value once per sample, shared by both the
recirculating and output calls (dividing by a scalar afterward doesn't change frequency content),
matching the same "shared per-sample state, separate scaled accessor" shape the since-reverted
Fold envelope-following used, for the identical reason: `process()` is called twice per sample.

All three classes share the identical `process(amount01, driveCompensation)` contract (Fold's is
`process(x, amount01, driveCompensation)` - it's stateless, no per-sample filter to update)
specifically so a new type can be added to the dropdown later without touching the selection
mechanism.

`StrikeSaturator` is the third and last option from the original discussion: soft saturation, a
plain SYMMETRIC tanh curve (unlike Fuzz's deliberate asymmetry - both half-cycles get identical
treatment, a real odd function like Fold) with a much lower `maxDrive` than Fuzz's 50 - the point is
a mild, warm/rounded character, not a heavy clip. Despite being gentler in overall gain, a render at
the same worst-case condition that first surfaced Fuzz's hiss (full Bow) showed it needed the
IDENTICAL fix, not by assumption but by measurement: the unshaped baseline's own settled-tail energy
(Bow's continuous noise injection already has real high-frequency content on its own) sat 29% above
6kHz, but Saturate=100% pushed that to 72% - a real, substantial increase from the saturation stage
itself. Fixed with the same cascaded two-stage one-pole lowpass at 3kHz Fuzz uses (re-measured
afterward rather than assumed to transfer just because the mechanism looked the same) - down to
~5% above 6kHz, actually below the unshaped baseline's own high-band content. Lesson: "this curve
is gentler than Fuzz" was true of its gain/drive, but not of whether it needed the same tone-shaping
fix - the two questions turned out to be independent, and only measuring settled it.

The Waveshape knob's own 0-100% range is remapped per Waveshaper Type before reaching the curve
itself (Fold: 0-59%, Fuzz: 0-20%, Saturate: 0-30% of each class's own internal amount range) - the
user specified these directly after listening, so the knob's full physical travel stays musically
useful for each type instead of the usable range being crammed into the first fraction of the turn.

`StrikeBitCrush` is a fourth Waveshaper Type - not one of the original three curve options, but a
genuinely different KIND of degradation (sample-and-hold rate reduction + bit-depth quantization,
the classic "lo-fi crush" pair bundled under one knob) that fits the same runtime-selectable seam.
Unlike every other class in this file, it needs no `driveCompensation` split at all: quantization
can't amplify a signal the way a drive-scaled curve can, so there's no loop-gain-runaway risk to
guard against - `process()` takes no parameters, and the identical crushed value is used for both
the recirculating and output paths. The one thing it DOES need that the others don't: an explicit
clamp to +-1 in `updateFilter()`, since rounding an already-large input to the nearest quantization
step can push it slightly further from zero rather than saturate it - the opposite failure mode from
every curve-based waveshaper, where boundedness falls out of the curve's own math for free.

**Ring Modulator** (`StrikeRingModulator.h`) - multiplies a signal by a free-running sine
oscillator, the classic ring-mod effect (sum/difference sidebands, not harmonically related to the
input). This is its own area, not a fifth Waveshaper Type, for two reasons: it needs its own
Frequency control (20Hz-5kHz, skewed toward the lower/mid range) that none of the Waveshaper curves
have an equivalent of, and its safety story is fundamentally different from every one of them.
Applied IN-LOOP - the user's explicit choice over an output-only placement, presented as a real
design fork (see git history/PR discussion) - right after the Waveshaper, so the ring-modulated
signal becomes part of what's actually resonating rather than a bolted-on post-effect, compounding
every pass around the loop for a much more extreme/metallic character.

Real-time-safety story, unlike anything else in this codebase: since the oscillator's own value is
bounded to [-1, 1] and Ring Mod Amount to [0, 1], the resulting per-sample gain factor
(`1 + amount*(oscValue-1)`) is ITSELF always bounded to [-1, 1] - ring modulation can only ever
shrink or invert a signal passing through it, never amplify it. No `driveCompensation`-style split
is needed at all (unlike every Waveshaper curve), and this was confirmed empirically through the
real processor, not just algebraically: at Ring Mod=100% on the same worst-case settings (max Decay,
full Bow) used throughout this feature, the sustained loop measured markedly QUIETER than the
unmodulated equivalent, never louder.

Each of the 8 voices owns an independent oscillator instance rather than sharing one across the
whole instrument - matching every other per-voice seam in this codebase (Waveshaper, Structure's
dispersion noise, etc. are all per-voice, not shared), but a real, documented tradeoff: simultaneous
notes are each ring-modulated independently rather than phase-locked together, so a chord's ring-mod
character isn't perfectly coherent across voices the way a single shared modulator would produce.
The oscillator's phase deliberately resets to 0 on every `noteOn()` (matching this codebase's
"always retriggers, everything reset" policy) rather than free-running across notes, for
determinism/testability - not an attempt at realism (a real analog oscillator wouldn't reset either).

**Dual Cross-Coupled Feedback Topology** (`StrikeVoice.h`) - the base scaffold's original single
delay line in a loop is now one of TWO live-switchable topologies (a "Topology" dropdown, Single/
Dual, at the user's explicit request so both can be A/B'd by ear the same way Waveshaper Type is) -
a runtime choice, unlike Excitation/Loop Filter/Delay Tuning, for the same reason Waveshaper/Ring
Modulator are: it needs to be compared live, not rebuilt to switch. This required a real refactor:
`SingleLineStrikeVoice` split into `StrikeStringLineChannel` (everything genuinely PER-STRING -
one delay line, one loop filter, one excitation, one dispersion filter, a full set of Waveshapers,
one Ring Modulator) and `StrikeVoice` (the orchestrator, owning one or two channels and deciding
how they're wired together). The split is bit-exact for Single topology - `renderChannelSample()`
is the old `renderNextSample()`'s body unchanged, just with `stringLine.write()` pulled out into a
separate `writeBack()` call so the orchestrator can intercept the value in between, which is exactly
where cross-coupling happens.

Grounded in real coupled-string physics, not an invented formula: Gabriel Weinreich's "Coupled Piano
Strings" (JASA 62(6), 1977) measured that piano unison strings - nominally the SAME pitch - coupled
through a shared bridge decompose into common and differential vibration modes with genuinely
different decay rates, producing the audible "double decay"/beating a single string can't produce;
Julius O. Smith's "Physical Audio Signal Processing" (Appendix C.13, "Two Coupled Strings",
ccrma.stanford.edu/~jos/pasp/) formalizes this for digital waveguides as coupling at the point where
each string's own fully-processed outgoing wave meets a shared bridge junction - which is why
cross-coupling reads from `filtered` (post-Waveshaper, post-Ring-Mod, pre-write-back), not the raw
delayed read, and only ever needs ONE Cross-Couple knob, not a per-effect one:

```
writeBackA = (1-c)*filteredA + c*filteredB
writeBackB = (1-c)*filteredB + c*filteredA
```

Two genuinely independent lines are semantically required, not incidental - coupling two identical
signals does nothing (the formula above reduces to a no-op if filteredA==filteredB). Each channel's
Excitation and dispersion-noise RNG gets its own seed (`StrikeStringLineChannel::setNoiseSeed()`,
called once on line B at `prepare()` time) specifically so the two lines have something genuinely
different to exchange, even at Detune=0% - real coupled unisons are nominally same-pitch strings
(Weinreich), so Detune (0-100%, latched at noteOn like Brightness, ~50 cents max) is a secondary,
optional knob layered on top of coupling, not the primary mechanism.

Safety, unusually, needed NO tuned ceiling at all (unlike every Waveshaper's `maxDrive` or
Structure's `maxDispersionGain`) - two independent closed-form arguments, both holding for the
ENTIRE 0-100% Cross-Couple range: (1) `writeBackA`/`writeBackB` are exact convex combinations, which
can never exceed the larger of their two inputs in magnitude - since `filteredA`/`filteredB` are
each already unconditionally bounded before coupling ever runs, coupling cannot introduce a new way
to blow up; (2) decomposing into common mode `(A+B)/2` and differential mode `(A-B)/2`, coupling
leaves the common mode's round-trip gain completely unchanged and scales the differential mode's
gain by exactly `(1-2c)` - since both lines always get the identical Damping value (same loop gain
`g`, exactly, every sample) and `TwoPointAverageLoopFilter` hard-clamps `g` to `[0.90, 0.9995]`,
both modes stay strictly contractive for every Damping setting and the whole Cross-Couple range.
Confirmed empirically too, not just algebraically: at max Cross-Couple on the same worst-case
settings used throughout this file, output stayed well within the same peak bound every other
feature is held to.

**Cross-Couple's 0-100% range turned out to already BE the true mathematical maximum, not an
arbitrary conservative cap** (the user asked whether it could go further): the differential-mode
gain `g*(1-2c)` has `|1-2c|=1` at both endpoints (c=0 and c=1) - pushing `c` outside `[0,1]` makes
`|1-2c|>1`, which can push the differential mode's own gain above 1 (real instability). Unlike
every other tuned constant in this file (Waveshape's drive, Structure's dispersion gain), which
were reined in well short of an actual instability boundary, Cross-Couple's full knob range already
uses the entire span the closed-form proof covers - there's no headroom left to give it.

**Couple Delay** inserts a short (0-10ms), independent integer-sample delay into EACH direction of
the cross-coupling path (`StrikeShortDelay` - a plain ring buffer, no fractional interpolation,
since this shapes the coupling's own frequency response rather than a note's pitch), turning the
coupling from a flat, broadband effect into a genuinely HARMONIC-DEPENDENT one: a pure delay is a
phase shift, so different harmonics arrive at the coupling point at different relative phases,
reinforcing or partially cancelling depending on how the delay compares to each harmonic's own
period - the same phase-interference mechanism Position's own tap already uses, applied to the
coupling path instead of a listening tap. 0ms is a bit-exact match for the original (undelayed,
same-instant) coupling formula.

The safety argument extends cleanly to any delay amount, with the IDENTICAL bound, not a new one: a
k-sample delay is a pure phase rotation (`z^-k`) at any given frequency - it can't change
magnitude, only phase. The common/differential-mode transfer factors become `(1-c) + c*z^-k` and
`(1-c) - c*z^-k` instead of the plain real `(1-2c)` the undelayed case reduces to - but both are
still 2-tap FIR filters whose coefficient magnitudes sum to exactly `(1-c)+c=1`, so by the triangle
inequality both stay bounded to magnitude 1 at EVERY frequency and EVERY delay amount, not just
zero delay - the exact same bound as the undelayed proof, shown to hold regardless of delay. No new
ceiling needed here either - the same "already at its provably-safe maximum" property Cross-Couple
itself has.

**Resonant Loop Filter** (`StrikeLoopFilter.h`) - the Loop Filter seam's second live-switchable
option (a "Loop Filter Type" dropdown, Two-Point Average / Resonant, at the user's explicit
request - the same migration Waveshaper Type/Feedback Topology already made from a compile-time
template parameter to a runtime choice). This is the single most safety-critical addition to this
codebase: a resonant filter sits INSIDE the Karplus-Strong feedback loop, so if its own gain at its
resonant peak ever exceeded 1, that frequency's energy would grow without bound every pass - real,
audible runaway, not a subtle bug. Researched directly (not guessed) from Jaffe & Smith 1983
("Extensions of the Karplus-Strong Plucked-String Algorithm") and Julius O. Smith's PASP treatment
of the Extended KS loop filter, which states the requirement plainly - `|H_d(e^jwT)| <= 1` - and
confirmed every canonical EKS loop filter in that literature is a damping/lowpass design, never a
resonant boost; also cross-checked against Mutable Instruments Rings' own `string.cc` (already this
codebase's reference for Structure/Position), whose own "extra resonance" trick is applied to a
non-recirculating OUTPUT tap, not injected as gain inside the loop - independent confirmation that
resonant colouring's risk surface is normally kept outside (or provably bounded within) the
recirculating path.

`StrikeResonantLoopFilter` cascades the existing `TwoPointAverageLoopFilter` (keeps the
physically-correct "brightness fades as decay progresses" character exactly as before) with a new
`StrikeResonantPeakFilter` (an RBJ-cookbook constant-0dB-peak-gain resonant bandpass), mixed in by
Resonance (0% = bit-exact bypass; also drives Q, 0.7-10, for a single "more resonance = narrower/
ringier peak" knob) at a Formant Frequency (an absolute Hz value, 80Hz-8kHz, NOT tracking the
note's own pitch - see below for why that's a musical choice, not a stability one).

Derived, not assumed: at its own design frequency, the peak filter's magnitude is EXACTLY 1
(the transposed-direct-form-II biquad's numerator and denominator reduce to the identical complex
factor at that frequency - re-derived directly rather than trusted from the cookbook formula alone,
given how load-bearing this is), and its DC/Nyquist gain is exactly 0 in both cases - the standard
"single resonant maximum of exactly 1, nowhere higher" property of this filter family, verified by a
dense numeric frequency sweep in the test suite, not just trusted from the derivation (the same
"measured, not just reasoned" discipline this codebase applies even to already-provable properties).
The Resonance mix itself is a convex combination - `(1-r) + r*H_peak(w)` - bounded to magnitude 1 by
the triangle inequality for every frequency/r/Q/Formant Frequency, the identical algebraic pattern
Cross-Couple and Couple Delay already use. Combined with the existing filter's own proven `|H(w)|
<= 0.9995`, the total per-pass loop gain never exceeds 0.9995 at ANY frequency, ANY Damping,
Resonance, or Formant Frequency - no tuned ceiling needed on either new control, the same
"already at its provably-safe maximum" category Cross-Couple/Couple Delay occupy. This bound never
references note pitch or delay length at all, so it holds identically from A0 to C8 - which is
exactly why Formant Frequency can be a pure musical choice (absolute Hz) rather than something that
needs to track the note being played.

`getLoopGain()` (Bow's loudness-compensation formula depends on it) is redefined as this filter's
own DC gain - `g*(1-r)`, since the resonant peak has an exact DC zero regardless of Resonance/
Formant/Q - confirmed to keep `sqrt(1-loopGain)` well-behaved (always positive, never NaN) at every
control combination, and reduces to today's exact behavior at Resonance=0%.

**Six swappable areas**, each isolated so the others never need to change:

1. **Excitation** (`StrikeExcitation.h`) - "generate one sample of excitation per tick, shaped by
   a live ADSR envelope." Base implementation: `NoiseExcitation`, a brightness-controllable
   lowpassed white-noise generator with a live Pluck/Bow envelope morph (see above).
2. **Loop Filter** (`StrikeLoopFilter.h`) - "process one sample through the feedback path." Two
   implementations, selected at RUNTIME (a live "Loop Filter Type" dropdown - see above for why
   this seam migrated from a compile-time template parameter): `TwoPointAverageLoopFilter`, the
   classic `y[n] = g * 0.5*(x[n] + x[n-1])` one-zero averager (brightness-dependent decay is real
   Karplus-Strong physics here, not a bug - see the class's own comment), and
   `StrikeResonantLoopFilter`, which cascades that same filter with a resonant peak.
3. **Delay Tuning** (`StrikeStringLine.h`) - fractional-delay interpolation, isolated behind an
   `Interpolator` template parameter. Base implementation: `LinearInterpolator`. This is a
   hand-rolled ring buffer (`std::vector<float>` + a non-consuming `read()`), **not** a wrapper
   around `juce::dsp::DelayLine` - found empirically while building this class that `DelayLine`'s
   `popSample()` is a strictly causal, state-consuming read that cannot support seeding a burst of
   samples before any of them are read back, which is exactly what Karplus-Strong's noteOn priming
   needs (see the class's own comment for the full explanation - it's not obvious from JUCE's own
   header). Also offers `readAt()`, a second, stateless read at any explicit delay length - what
   makes Position's alternate string tap and Structure's shortened main-tap read possible without
   disturbing the delay length that sets the note's pitch.
4. **Feedback Topology** (`StrikeVoice.h`) - signal routing. Two implementations now exist,
   selected at RUNTIME (a live "Topology" dropdown, at the user's request - see above): Single (one
   `StrikeStringLineChannel` in a loop, the original base scaffold) and Dual (two channels,
   cross-coupled at their write-back point). `StrikeVoice` is the orchestrator that owns one or
   two `StrikeStringLineChannel`s and decides how they're wired - unlike Excitation/Loop Filter/
   Delay Tuning, a new topology changes member LAYOUT, not just behaviour, which is why this seam
   needed a real class split (see above) rather than a template parameter or a simple branch.
5. **Waveshaper** (`StrikeWaveshaper.h`) - "nonlinearly reshape one sample of the loop's own
   recirculating signal before it's written back." Four implementations, all always present and
   selected at RUNTIME (see above for why this seam differs from the other four): `StrikeWaveFolder`,
   `StrikeFuzz`, `StrikeSaturator`, and `StrikeBitCrush`. Most share the identical
   `process(amount01, driveCompensation)` contract (Fold's own `process(x, amount01,
   driveCompensation)` is stateless, matching `StrikeDispersionFilter`'s shape rather than the
   Loop Filter's setter-then-process split) - BitCrush is the one exception, needing no
   `driveCompensation` at all (see above for why).
6. **Ring Modulator** (`StrikeRingModulator.h`) - "multiply a signal by a free-running
   oscillator." Its own area rather than a Waveshaper Type - it needs a Frequency control none of
   the curves have an equivalent of, and (unlike every Waveshaper) is provably bounded by the
   input's own magnitude, so it needs no `driveCompensation`-style split either. Applied in-loop,
   after the Waveshaper (see above).

No polymorphism (no `virtual`, no `std::function`-as-strategy) is used anywhere - every per-sample
seam is either a compile-time template parameter on `StrikeStringLineChannel` or, for Waveshaper/
Ring Modulator/Feedback Topology/Loop Filter specifically, a runtime choice among concrete types/
paths owned by value with a plain branch - matching this
catalog's established DSP style: small, concrete, framework-free classes composed by value (see
`gradient-pitch/Source/GradientDelayBuffer.h` / `GradientPitchShiftEngine.h`), which is also why
`StrikeStringLine` ended up hand-rolled the same way `GradientDelayBuffer` is, rather than
wrapping a JUCE class as originally planned.

## Future swap-in points

| Area                                        | What changes                                                                                 | Real-time implication                                                                                                                                                                                                                                                                                                                                      |
| ------------------------------------------- | -------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Excitation                                  | Noise -> filtered noise / sample burst; also, a held bow note's loudness could gain a `/ sqrt(delaySamples)` term to flatten the still-unaddressed pitch-dependent sustained-loudness gap | None - `nextExcitationSample()` is a bounded per-tick call with fixed-size state, no scratch buffer needed even for a variant with a longer/continuous shape.                                                                                                                                                                                              |
| Loop Filter                                 | Two-point average -> resonant is built and live-switchable; a future third type (e.g. asymmetric, or a second internal comb/allpass creating non-harmonic peaks) needs its own closed-form stability argument, not assumed to inherit the resonant filter's | Both filters always present, fixed bounded state (a few extra floats) - size in `prepare()`. A future variant needing its own tap needs that tap preallocated the same way `StrikeStringLine` is - and, since this seam lives inside the feedback loop, needs its own proof that its own worst-case gain can't push the combined per-pass loop gain above 1 at any frequency (see "How it works" above for how the resonant filter's own proof works).                                                                                                                                                                               |
| Delay Tuning                                | Linear -> higher-order (Lagrange-style) interpolation. The Jaffe/Smith dispersion technique originally anticipated here is now built (`StrikeDispersionFilter`, driving the Structure control - a cascade of small allpass stages, not `StrikeStringLine`-backed at all any more), plus Rings-accurate noise-driven delay-length FM above Structure=75% (see "How it works" below) - not as an `Interpolator` swap, but as a separate class composed by value in `StrikeStringLineChannel`, closer in shape to a Feedback Topology addition. A future extension: Structure's negative-dispersion range (nonlinear "bridge curving" distortion, present in Rings for negative dispersion values only) - out of scope here since Structure's 0-100% range only ever corresponds to Rings' *positive* dispersion range, where bridge curving never engages either. | A pure-function interpolator (Linear, Lagrange) is a free template-argument swap, no new state. `StrikeDispersionFilter`'s per-stage state is a handful of fixed-size floats - no delay line/ring buffer at all, real-time safe by construction. |
| Waveshaper                                  | All four Waveshaper Types are built (Fold, Fuzz, Saturate, BitCrush) - a fifth would need its own driveCompensation/loop-safety story worked out, same as each of these did | Unlike the other three seams, this is a RUNTIME choice, not a compile-time `StrikeStringLineChannel` template parameter - `StrikeWaveshaper.h` explains why (the user asked for a live dropdown, not a rebuild). All four concrete classes are owned by value and selected via a plain branch (no virtual dispatch). Fold is stateless per-call (`process(x, amount01, driveCompensation)`); Fuzz and Saturator both need a per-sample post-clip lowpass (measured necessary for both, despite Saturator's much gentler drive), so they use `updateFilter(x, amount01)` once per sample plus a stateless `process(amount01, driveCompensation)` accessor instead; BitCrush needs updateFilter()/process() too (real sample-and-hold state) but no driveCompensation parameter at all (see "How it works" above) - a future variant follows whichever shape fits its own safety story. |
| Ring Modulator                              | Currently a fixed sine oscillator - a future variant could offer other waveforms (triangle/square for a buzzier, more harmonically-dense modulator), note-pitch tracking (a toggle to make it a harmonic effect rather than an inharmonic one), or a shared cross-voice oscillator instead of one per voice (trading the current per-voice independence for phase-locked chords) | `StrikeRingModulator.h`'s `updateOscillator()`/`process()` split isolates "what the oscillator IS" from "how it's applied" - a different waveform only touches `updateOscillator()`. A shared oscillator would need `PluginProcessor::processBlock()` to compute it once per sample and pass the value into every voice, a bigger structural change than any other feature in this file. |
| Feedback Topology                           | Single -> Dual cross-coupled is built and live-switchable; a future third topology (e.g. more than two lines) would need its own branch in `StrikeVoice::renderNextSample()` and its own safety argument, not assumed to inherit Dual's | `StrikeVoice` owns up to two `StrikeStringLineChannel`s by value, ~2x buffer footprint (still trivial - see `StrikeStringLineChannel::requiredCapacitySamples()`'s sizing table in its own comment). A third topology would likely want its own orchestrator class rather than a third branch inside this one, once the branching itself gets unwieldy. |
| More voices / a different stealing strategy | `numVoices` constant -> a larger pool; basic oldest-voice-stealing -> release-aware stealing | `StrikeVoiceAllocator<N>`'s array members grow with `N`, still fixed-size and stack/member-allocated, no runtime allocation. Release-aware stealing (prefer stealing an already-released note over one still held) would need `StrikeVoiceAllocator` to also track release state, not just age - a real but bounded change confined to that one class. |

## Parameters

| Parameter        | Range        | Default | Description                                                                                                                                                                                  |
| ---------------- | ------------ | ------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Decay            | 0 - 100%     | 60%     | Loop gain - controls sustain length. Decay time is pitch-dependent at a fixed setting (higher notes decay faster in real time) - see `TwoPointAverageLoopFilter::processSample()`'s comment. |
| Output Level     | -60 to +6 dB | -6 dB   | Post-voice output gain                                                                                                                                                                       |
| Pluck Brightness | 0 - 100%     | 100%    | Excitation tone - 0 is heavily lowpassed noise, 100% is raw white noise. Only takes effect on the next pluck.                                                                                |
| Pluck / Bow      | 0 - 100%     | 0%      | 0% is a pure pluck (the original base-scaffold behaviour). 100% is a pure bow - continuous excitation sustains the note for as long as it's held, decaying only after release. Live-adjustable, unlike Brightness. See "How it works" above.                                                                                |
| Structure        | 0 - 100%     | 0%      | Inharmonicity/dispersion - 0% is a bit-exact no-op (pure harmonic partials), 100% is maximally stretched/metallic. Live-adjustable. See "How it works" above.                                                                                |
| Position         | 0 - 100%     | 50%     | Where the string is excited/listened to - 50% (the midpoint) is a hollower, more harmonic character; the ends are fuller. No neutral/bypass value - every setting changes the output. Live-adjustable. See "How it works" above.                                                                                |
| Mono             | Off / On     | Off     | Off (Poly) is the original 8-voice-pool behaviour. On (Mono) drives a single voice with classic last-note-priority: holding two notes sounds only the most recent, and releasing it retriggers whichever earlier note is still held, rather than leaving it silently ringing or cutting to silence. See `StrikeMonoNoteStack.h`. |
| Glide Time       | 0 - 500ms    | 0ms     | Mono-only. A legato retrigger between two held notes still fires a fresh pluck, but its pitch approaches the new note smoothly over this time instead of jumping instantly. 0ms preserves Mono's original instant-retrigger behaviour. See "How it works" above.                                |
| Waveshape        | 0 - 100%     | 0%      | Amount of whichever Waveshaper Type is selected, applied inside the feedback loop. 0% is a bit-exact no-op. Live-adjustable. The knob's own 0-100% range is remapped per Waveshaper Type before reaching the curve itself (Fold: 0-59%, Fuzz: 0-20%, Saturate: 0-30% of each class's own internal amount range) - tuned directly by the user so the knob's full physical travel stays musically useful for each type, rather than the usable range being crammed into the first fraction of the turn. See "How it works" above.                                |
| Waveshaper Type  | Fold / Fuzz / Saturate / BitCrush  | Fold    | Fold: wavefolding - past a threshold the signal reflects back on itself instead of clipping, adding dense, often inharmonic overtones. Fuzz: heavy asymmetric-clip distortion - a transistor-fuzz-pedal character (dense odd harmonics, plus even harmonics from the asymmetry). Saturate: gentle symmetric tanh soft saturation - a mild, warm/rounded character. BitCrush: sample-and-hold rate reduction + bit-depth quantization - the classic "lo-fi" character. See "How it works" above.                                |
| Ring Mod         | 0 - 100%     | 0%      | Depth of the in-loop ring modulator (see "How it works" above) - multiplies the loop signal by a sine oscillator at Ring Mod Freq. 0% is a bit-exact no-op. Live-adjustable. Runs alongside whichever Waveshaper Type is selected, applied after it.                                |
| Ring Mod Freq    | 20Hz - 5kHz  | 200Hz   | Frequency of the ring modulator's own oscillator - deliberately NOT tracking the note's pitch (that's what produces ring mod's characteristic inharmonic sum/difference sidebands rather than a harmonic effect). Skewed range - most of the knob's travel sits in the lower/mid frequencies. Live-adjustable, audible mid-note.                                |
| Topology         | Single / Dual | Single | The Feedback Topology seam (see "How it works" above) - Single is the original one-line scaffold; Dual cross-couples two independently-excited lines. Live-switchable; changing it mid-performance is treated as an implicit all-notes-off (same mechanism as the Mono/Poly switch), since the two topologies hold genuinely different internal state.                                |
| Cross-Couple     | 0 - 100%     | 0%      | Dual-topology only (no effect at Topology=Single). How much of each line's write-back value comes from the OTHER line. 0% is a bit-exact no-op for line A alone (line B still rings independently and sums in). Provably safe across the FULL range - no ceiling needed, see "How it works" above. Live-adjustable.                                |
| Couple Delay     | 0 - 10ms     | 0ms     | Dual-topology only. A short delay inserted into the cross-coupling path itself (each direction independently) - turns the coupling from flat/broadband into harmonic-dependent, since a delay is a phase shift and different harmonics interfere differently depending on delay length vs. their own period. 0ms is a bit-exact match for the original undelayed coupling. Also provably safe across its full range - see "How it works" above. Live-adjustable.                                |
| Detune           | 0 - 100%     | 0%      | Dual-topology only. Offsets line B's pitch from line A's by up to ~50 cents at 100% (raised from an initial ~20 cent starting point once the user heard it and wanted more range - unlike Cross-Couple, Detune has no stability ceiling, so this is a pure "does it sound good" call). Latched at noteOn (not live), same convention as Brightness - real unison detuning isn't a performance gesture. 0% keeps both lines at the identical pitch (the primary, physically-grounded coupled-unison design - see "How it works" above).                                |
| Loop Filter Type | Two-Point Average / Resonant | Two-Point Average | Two-Point Average: the classic one-zero averager. Resonant: cascades that same filter with a resonant peak - see "How it works" above for its own closed-form safety proof. Live-switchable; a mid-note switch just leaves the unselected filter's own history momentarily stale until reselected (same accepted tradeoff Waveshaper Type has), no implicit all-notes-off needed.                                |
| Resonance        | 0 - 100%     | 0%      | Resonant-loop-filter-only (no effect at Loop Filter Type=Two-Point Average). How much of the resonant peak is mixed in (also drives Q, 0.7-10 - narrower/ringier at higher settings). Provably safe across the FULL range - no ceiling needed, see "How it works" above. Live-adjustable.                                |
| Formant Freq     | 80Hz - 8kHz  | 1000Hz  | Resonant-loop-filter-only. The resonant peak's own frequency - an absolute Hz value, deliberately NOT tracking the note's own pitch (the stability proof never references it, so this is a purely musical choice - see "How it works" above). Skewed range, live-adjustable.                                |

Pitch is MIDI-driven, not a knob. No dry/wet (a self-generating voice has no dry signal to blend
against yet - see the "How it works" section).

## Project structure

```
strike-synth/
├── CMakeLists.txt
├── Source/
│   ├── StrikeExcitation.h/.cpp   # Excitation seam: NoiseExcitation (pluck burst + bow morph)
│   ├── StrikeLoopFilter.h/.cpp   # Loop Filter seam: Two-Point Average / Resonant, runtime dropdown
│   ├── StrikeStringLine.h        # Delay Tuning seam: hand-rolled ring buffer, template Interpolator
│   ├── StrikeVoice.h             # Feedback Topology seam: StrikeStringLineChannel (per-string)
│   │                                 # + StrikeVoice (orchestrator, Single/Dual cross-coupled)
│   │                                 # + StrikeDispersionFilter (Structure's allpass primitive)
│   ├── StrikeWaveshaper.h        # Waveshaper seam: Fold/Fuzz/Saturate/BitCrush, runtime dropdown
│   ├── StrikeRingModulator.h     # In-loop ring modulator (own area, not a Waveshaper Type)
│   ├── StrikeVoiceAllocator.h    # Voice-to-note allocation/oldest-voice-stealing for the pool (Poly)
│   ├── StrikeMonoNoteStack.h     # Last-note-priority/retrigger note tracking for Mono mode
│   ├── PluginProcessor.h/.cpp      # Parameter state, MIDI dispatch, owns the 8-voice pool
│   ├── PluginEditor.h/.cpp         # Minimal functional UI (no hardware-panel mockup pass yet)
│   ├── StrikeLookAndFeel.h/.cpp  # Thin subclass of the shared HardwarePanelLookAndFeel (theme only)
│   ├── StrikeBuildNumber.h       # Incrementing marker shown in the editor, so a stale/cached
│   │                                 # plugin build is visible from the UI itself
│   └── Tests/                      # StrikeTests: headless UnitTest console app (DSP seams only);
│                                     # StrikeProcessorTests: drives the real StrikeAudioProcessor
│                                     # end-to-end (APVTS/MIDI/processBlock), not just the DSP seams
```

## License

[AGPLv3](../LICENSE), same as the rest of this repo.
