# Karplunk

An extended Karplus-Strong physical-modeling string synth (AU / VST3 / Standalone). This is a
**base scaffold, not a finished instrument**: a correct, stable, 8-voice implementation built
around four clean experimentation seams (excitation source, loop filter, delay-line tuning, and
feedback topology) so each can be swapped later without touching the other three. See "Future
swap-in points" below before extending any of them.

See the [root README](../README.md) for shared build requirements, the exFAT/apostrophe build
gotchas, and running tests across all plugins at once.

**Roadmap**: polyphony (8 voices, basic oldest-voice-stealing - see `KarplunkVoiceAllocator.h`) is
done. No installer, UI polish (mockup-first hardware-panel pass), or preset system yet - all
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

**Four swappable areas**, each isolated so the others never need to change:

1. **Excitation** (`KarplunkExcitation.h`) - "generate N samples to seed the delay line." Base
   implementation: `NoiseBurstExcitation`, a brightness-controllable lowpassed white-noise burst.
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
   header).
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
| Excitation                                  | Noise -> filtered noise / sample burst                                                       | None if bounded to the preallocated seed length; a longer/continuous burst needs its own `prepare()`-sized scratch buffer.                                                                                                                                                                                                                                 |
| Loop Filter                                 | Two-point average -> one-pole/comb/resonant/asymmetric                                       | Fixed bounded state (a few extra floats) - size in `prepare()`. A comb/resonant filter needing its own tap needs that tap preallocated the same way `KarplunkStringLine` is.                                                                                                                                                                               |
| Delay Tuning                                | Linear -> higher-order (Lagrange-style) -> future Jaffe/Smith stretched-allpass              | A pure-function interpolator (Linear, Lagrange) is a free template-argument swap, no new state. A true stretched-allpass needs its own _persistent_ state between calls (unlike this seam's current pure-function shape) and would need its own hybrid delay/filter class - still real-time safe as long as new state is sized in `prepare()`.             |
| Feedback Topology                           | Single loop -> dual cross-coupled lines -> nonlinear waveshaping in the loop                 | Dual cross-coupled = a **new class** reusing the same three area-components by value, ~2x buffer footprint (still trivial - see `SingleLineKarplunkVoice::requiredCapacitySamples()`'s sizing table in its own comment) + a small fixed cross-mix matrix. Waveshaping in the loop adds only per-sample math, no new buffering.                             |
| More voices / a different stealing strategy | `numVoices` constant -> a larger pool; basic oldest-voice-stealing -> release-aware stealing | `KarplunkVoiceAllocator<N>`'s array members grow with `N`, still fixed-size and stack/member-allocated, no runtime allocation. Release-aware stealing (prefer stealing an already-released note over one still held) would need `KarplunkVoiceAllocator` to also track release state, not just age - a real but bounded change confined to that one class. |

## Parameters

| Parameter        | Range        | Default | Description                                                                                                                                                                                  |
| ---------------- | ------------ | ------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Decay            | 0 - 100%     | 60%     | Loop gain - controls sustain length. Decay time is pitch-dependent at a fixed setting (higher notes decay faster in real time) - see `TwoPointAverageLoopFilter::processSample()`'s comment. |
| Output Level     | -60 to +6 dB | -6 dB   | Post-voice output gain                                                                                                                                                                       |
| Pluck Brightness | 0 - 100%     | 100%    | Excitation tone - 0 is heavily lowpassed noise, 100% is raw white noise. Only takes effect on the next pluck.                                                                                |

Pitch is MIDI-driven, not a knob. No dry/wet (a self-generating voice has no dry signal to blend
against yet - see the "How it works" section). No Glide (every note-on is a fresh pluck, not a
pitch to glide toward).

## Project structure

```
karplunk-synth/
├── CMakeLists.txt
├── Source/
│   ├── KarplunkExcitation.h/.cpp   # Excitation seam: NoiseBurstExcitation
│   ├── KarplunkLoopFilter.h/.cpp   # Loop Filter seam: TwoPointAverageLoopFilter
│   ├── KarplunkStringLine.h        # Delay Tuning seam: hand-rolled ring buffer, template Interpolator
│   ├── KarplunkVoice.h             # Feedback Topology (base case): SingleLineKarplunkVoice
│   ├── KarplunkVoiceAllocator.h    # Voice-to-note allocation/oldest-voice-stealing for the pool
│   ├── PluginProcessor.h/.cpp      # Parameter state, MIDI dispatch, owns the 8-voice pool
│   ├── PluginEditor.h/.cpp         # Minimal functional UI (no hardware-panel mockup pass yet)
│   ├── KarplunkLookAndFeel.h/.cpp  # Thin subclass of the shared HardwarePanelLookAndFeel (theme only)
│   └── Tests/                      # KarplunkTests: headless UnitTest console app (DSP seams only)
```

## License

[AGPLv3](../LICENSE), same as the rest of this repo.
