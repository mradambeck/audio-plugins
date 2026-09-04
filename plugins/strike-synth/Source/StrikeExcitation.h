#pragma once

#include <cstdint>

// The Excitation seam: "generate one sample of excitation per tick, shaped by a live
// Attack/Decay/Sustain/Release envelope." Deliberately no JUCE include at all (own xorshift PRNG
// instead of juce::Random) - matches plugins/gradient-pitch/GradientPitchShiftEngine's convention of
// keeping standalone DSP classes free of any JUCE dependency, so they build/test fast in
// isolation and stay trivially swappable.
//
// This used to interpolate an attack ramp and a decay-to-silence ramp, both racing toward
// bowAmount = 1 with no explicit sustain level - which turned out to be a real, audible bug: the
// attack time was interpolated linearly in time, but the decay time was interpolated linearly in
// its own coefficient, and those two curves don't move together (a decay coefficient lerp is
// dominated by its "fast" endpoint until bowAmount is within a fraction of a percent of 1.0). The
// result was a loud pluck at bowAmount=0, a loud sustained bow at bowAmount=1, and a dead zone of
// much quieter output through most of the middle of the range - a slow attack paired with a decay
// that hadn't actually slowed down yet. The fix is a proper ADSR: an explicit `sustainLevel`
// (interpolated directly by bowAmount) decouples "how loud does it stay" from "how fast does it
// get there" - loudness at any point in time is now governed by that one monotonic parameter, not
// an emergent race between two independently-moving time constants. Every stage is a one-pole
// recurrence (not a closed-form/elapsed-sample-counter formula - bowAmount is live and can change
// every sample via PluginProcessor's smoothing, and a one-pole recurrence stays continuous no
// matter how its own coefficient or target moves tick-to-tick, where a closed-form re-evaluation
// from a fixed t would not).
//
// Renamed from NoiseExcitation once the Bow side of the Pluck/Bow morph stopped being purely noise
// -driven: past a certain point, "noise" no longer described this class's dominant behavior for a
// bowed note (see nextFrictionSample()'s own comment). The Pluck side (the ADSR-enveloped burst
// below) is completely unchanged.
//
// To add a new excitation variant (a sample-based burst, etc.), write a new class matching this
// same method set (prepare/reset/setBrightness/setBowAmount/setBowForce/getBowAmount/
// setBaseDuration/noteOff/nextExcitationSample/setSeed) and swap the template argument in
// StrikeVoice.h's StrikeStringLineChannel instantiation - nothing in StrikeLoopFilter.h,
// StrikeStringLine.h, or StrikeVoice.h needs to change. setSeed() (see its own comment) joined
// this set once the Feedback Topology seam grew a second option that needs two independently-noisy
// Excitation instances - every Excitation variant needs it now, not just this one.
class StrikeExcitation
{
public:
    void prepare(double sampleRate) noexcept;
    void reset() noexcept;

    // 0 = fully darkened (one-pole lowpassed) noise, 1 = raw white noise. Applied inside
    // nextNoiseSample() (the Pluck side's own excitation source, unaffected by Bow Force/the
    // friction model below).
    void setBrightness(float amount01) noexcept;

    // Affects BOTH the Pluck burst's own noise (nextNoiseSample(), colored BEFORE Brightness's own
    // lowpass is applied on top) and the Bow model's own bow-noise term (nextBowNoiseSample(),
    // colored BEFORE its own fixed lowpass) - one shared control across the whole Pluck<->Bow
    // range, not two independent color knobs. Noise Color and Brightness/the bow-noise lowpass are
    // independent axes either way (Noise Color = the source's own inherent spectral tilt; the
    // existing lowpass stages = how much extra darkening on top of whichever color is chosen), not
    // two versions of the same knob. 0 = White (flat spectrum, this class's original/default noise -
    // bit-exact with every existing preset/test at this setting, on BOTH sides), 1 = Pink (~1/f,
    // Paul Kellet's widely-published 3-pole "economy" approximation - warmer, less hissy), 2 =
    // Brown/Red (~1/f^2, a single low-cutoff one-pole - bassier/rumblier still). Added directly in
    // response to the user judging the Pluck burst itself "pretty noisy" even after the Bow-noise
    // retune; originally Pluck-only, then extended to the Bow side too once the user noticed a
    // bowed note didn't change much (correctly - full bow blends the Pluck component to zero).
    void setNoiseColor(int color) noexcept { noiseColor = color; }

    // 0 = pure pluck: fast attack, decays fully away (sustain level 0) over a time tied to this
    // note's own period - see setBaseDuration(). 1 = pure bow: slow attack, settles into and holds
    // at full-amplitude sustain indefinitely, now driven by a real stick-slip friction model (see
    // nextFrictionSample()) rather than continuous noise injection. Every stage's time constants
    // AND the sustain level itself move with this, live, every tick - see nextExcitationSample().
    void setBowAmount(float amount01) noexcept;
    float getBowAmount() const noexcept { return bowAmount; }

    // Bow-side only (no effect at bowAmount=0, gated the same way Resonance has no effect at
    // Loop Filter Type=Two-Point-Average) - maps to the friction curve's own slope (STK's
    // "Bow Pressure" control, see nextFrictionSample()'s own comment), NOT bow speed/amplitude,
    // which stays governed by bowAmount/the existing envelope. A real bow's force/pressure and
    // its speed are genuinely independent physical axes - this is a new, dedicated control rather
    // than overloading bowAmount with a second meaning (see this class's own comment for why that
    // would risk re-deriving this file's own historical bug #2).
    void setBowForce(float amount01) noexcept { bowForce = amount01; }

    // Sets the RNG seed directly - not part of every Excitation variant's normal per-note
    // lifecycle (Structure/Position/etc. never call this); used by StrikeVoice's dual-topology
    // orchestrator to give its second line's Excitation a genuinely different noise sequence from
    // the first, called once at prepare() time. Without this, two identically-constructed,
    // identically-driven Excitation instances would produce bit-identical noise, making
    // cross-coupling and detune both silently inaudible - see StrikeVoice.h's own comment.
    // xorshift32 is degenerate at seed 0, so 0 is remapped to 1 (matching this class's own default).
    // Also reseeds bowNoiseRngState (offset by a fixed constant so it isn't just a copy of rngState's
    // own sequence) - without this, two Excitation instances constructed identically but reseeded
    // only via this call (as StrikeVoice's lineB is, at prepare() time) would share the FRICTION
    // model's noise floor bit-for-bit even though their Pluck-side noise correctly diverges, making
    // Dual topology silently collapse toward Single at bowAmount=1 (no independent bow character -
    // caught by StrikeVoiceTests.cpp's own "Topology=Dual produces measurably different output"
    // test).
    void setSeed(uint32_t seed) noexcept
    {
        rngState = seed != 0 ? seed : 1;
        const auto offsetSeed = seed + 0x9e3779b9u;
        bowNoiseRngState = offsetSeed != 0 ? offsetSeed : 1;
    }

    // Starts a fresh Attack stage for a new note, and tells this excitation what "one period"
    // means for the note currently ringing (so bowAmount=0's decay time scales with pitch the way
    // the original one-shot burst's fixed delaySamples-length window did). Called once per
    // noteOn, never mid-block.
    void setBaseDuration(int delaySamples) noexcept;

    // Starts the Release stage - the envelope ramps from wherever it currently is down to 0 over
    // a bowAmount-dependent release time, instead of being cut off instantly (which could click
    // at high sustain levels). Called once per noteOff. Safe to call even if already idle/released.
    void noteOff() noexcept;

    // Called once per render tick, from the very first sample after noteOn, unconditionally -
    // StrikeStringLineChannel no longer gates this by a `held` flag. An idle (never-triggered)
    // excitation returns exactly 0; a fully-released one converges to (very close to) 0 on its
    // own and just stays there. Never allocates.
    //
    // `stringSignal` is new: the string's own current content (StrikeStringLineChannel passes
    // `filtered` - post-Structure-dispersion, post-loop-filter, the same signal Position/Structure
    // already treat as "the string's own current value" elsewhere in this codebase). The friction
    // model needs this to compute relative bow/string velocity - see nextFrictionSample()'s own
    // comment for why this is the direct single-rail analogue of what real two-rail digital
    // waveguide bow models read from their own two rails, not an invented shortcut. The Pluck side
    // (bowAmount=0) never reads this parameter at all - bit-exact with this class's pre-friction
    // behavior at bowAmount=0, by explicit gate, not by algebraic coincidence.
    float nextExcitationSample(float velocity01, float stringSignal) noexcept;

private:
    float nextNoiseSample() noexcept;
    float nextBowNoiseSample() noexcept;

    // Pink noise: Paul Kellet's "economy" 3-pole IIR approximation of a 1/f spectrum (a real,
    // widely-published technique - see e.g. musicdsp.org's pink-noise-generation entry - not
    // invented here), driven by the same raw white sample nextNoiseSample() already generated.
    // Each stage is a stable one-pole IIR (coefficients < 1) fed a bounded input, so the sum is
    // BIBO-bounded by construction - pinkNormalizationGain (see its own comment) was still measured,
    // not assumed, to keep its actual output range comparable to White's [-1,1].
    float nextPinkNoiseSample(float white) noexcept;

    // Brown/Red noise: a single fixed, LOW-cutoff one-pole lowpass on raw white noise - the
    // standard, simplest real-time-safe approximation of a 1/f^2 spectrum (a true integrator/random
    // walk has an unbounded-variance pole exactly at z=1 and is unsafe for a note that can be held
    // indefinitely; a stable one-pole avoids that while still giving the same -6dB/octave rolloff
    // character above its cutoff). Deliberately a lower, FIXED coefficient than Brightness's own
    // minimum (brownLowpassCoeff below), not a duplicate of Brightness at its darkest setting - see
    // this class's own header comment for why Noise Color and Brightness are independent axes.
    float nextBrownNoiseSample(float white) noexcept;

    // Bow-side counterparts of the two methods above - identical math/coefficients, but their own
    // independent filter state (bowPinkB0/B1/B2, bowBrownState below), since the bow-noise term
    // advances on a different schedule (only when bowAmount > 0) and a different raw white stream
    // than the Pluck side's own noise.
    float nextBowPinkNoiseSample(float white) noexcept;
    float nextBowBrownNoiseSample(float white) noexcept;

    // Stick-slip friction bow model - ported directly from Perry Cook & Gary Scavone's STK
    // (Synthesis ToolKit) `BowTable`/`Bowed` classes, themselves a real-time-safe implementation of
    // the McIntyre/Schumacher/Woodhouse friction-curve formulation described in Julius O. Smith's
    // "Digital Waveguide Modeling of Bowed Strings" (ccrma.stanford.edu/~jos/BowedStrings/). Real
    // bow-string friction depends on the RELATIVE velocity between bow and string at the contact
    // point - this is a genuinely different mechanism from continuous noise injection, which is why
    // it needs `stringSignal` (the loop's own current content) as an input the Pluck side, and every
    // other seam in this codebase, has never needed.
    //
    // Real-time-safety note: the true physics has the friction force depend on the string's
    // response to that SAME force within the same instant - a genuine algebraic loop. Real digital
    // waveguide implementations (confirmed directly in STK's own shipped `Bowed::tick()`) sidestep
    // this by using the PREVIOUS tick's own string signal as the velocity proxy, at the cost of a
    // one-sample delay - exactly what happens here too, since `stringSignal` is `filtered` from
    // THIS tick's own already-computed value, read before this contribution is added to it (see the
    // call site in StrikeStringLineChannel::renderChannelSample()).
    //
    // The friction curve itself, `rho(vDelta) = clamp((|slope*vDelta+offset|+0.75)^-4, 0.01, 0.98)`,
    // is STK's own `BowTable::tick()` formula verbatim (not re-derived) - unconditionally bounded by
    // construction, not merely by the explicit clamp: since `|slope*vDelta+offset| >= 0` always,
    // the base is always `>= 0.75`, so the raw power is always in `(0, 0.75^-4]` for ANY finite
    // vDelta/slope/offset, before the clamp even runs - the same "bounded regardless of input"
    // property every Waveshaper curve has. The injected value `vDelta * rho(vDelta)` is NOT itself
    // unconditionally bounded (rho is bounded but vDelta isn't, since it depends on stringSignal) -
    // what keeps the whole system safe is the EXISTING tanh() cap at the injection site in
    // StrikeStringLineChannel::renderChannelSample(), which was already relied on for raw noise
    // and transfers unchanged here (tanh doesn't care what produced its argument): any finite
    // excitation output gets capped to a bounded per-sample contribution, and combined with the
    // loop's own already-proven per-pass contraction (|H(w)| <= 0.9995, unaffected by what's
    // injected), steady-state amplitude is bounded by (bounded forcing)/(1-contraction) - finite,
    // by construction.
    //
    // A real, measured bug this bounded-but-deterministic property did NOT anticipate: a held bow
    // note went to exact silence. Root cause, confirmed by a faithful offline simulation of the real
    // delay-line/loop-filter/friction chain (not a crude single-pole approximation): with a purely
    // deterministic friction curve and no time-varying asymmetry, vDelta*rho(vDelta) converges to a
    // near-constant value once the envelope settles, so every new write around the delay line
    // injects nearly the identical amount - the entire ring buffer converges toward uniform DC
    // content. That's a real, stable fixed point (not silence on its own), but StrikeStringLineChannel::
    // positionOutput() reads a SECOND tap from a different point in the SAME buffer specifically to
    // cancel periodic content - against a perfectly uniform buffer, that second tap reads the
    // identical value, so `forOutput - positionTap` cancels EXACTLY to zero. A genuine two-rail
    // (bidirectional-waveguide) bow model, like STK's own `Bowed` class actually is, can't degenerate
    // this way (bow force acts on the difference of two independently-delayed traveling waves, which
    // structurally can't collapse to a shared constant) - this single-delay-line adaptation has no
    // such structural guard.
    //
    // The fix: superimpose a small amount of genuine stochastic "bow noise" (see nextBowNoiseSample())
    // onto the deterministic friction curve - physically authentic (real bow/string contact has
    // surface-roughness/micro-slip randomness on top of the smooth stick-slip curve, and STK's own
    // Bowed model layers noise in for the same reason), and it breaks the buffer's uniformity so
    // Position's tap can no longer cancel it to exact zero. Confirmed by the same offline simulation
    // before implementing here: bowNoiseAmount=0 measured EXACTLY zero post-Position RMS at every
    // damping tested; nonzero amounts restored a stable, non-decaying RMS scaling roughly linearly
    // with the noise amount (already reflecting the loop's own resonant buildup, the same mechanism
    // continuousLevelAnalog relies on elsewhere) - not a guess, a measured relationship.
    float nextFrictionSample(float stringSignal) noexcept;

    uint32_t rngState = 1;
    float lowpassState = 0.0f;

    // 0 = White, 1 = Pink, 2 = Brown - see setNoiseColor()'s own comment.
    int noiseColor = 0;
    float pinkB0 = 0.0f;
    float pinkB1 = 0.0f;
    float pinkB2 = 0.0f;
    float brownState = 0.0f;

    // Separate, dedicated RNG state for the friction model's bow-noise term (see
    // nextFrictionSample()'s own comment) - kept independent of rngState/lowpassState above (the
    // Pluck side's own noise source, shaped by Brightness) so Brightness (a pluck-specific tone
    // control) can't accidentally alter the bow's noise floor. Fixed nonzero seed, distinct from
    // rngState's own default - xorshift32 is degenerate at 0.
    uint32_t bowNoiseRngState = 0x9e3779b9u;

    // Bow-side color state (see setNoiseColor()'s own comment) - separate from the Pluck side's
    // pinkB0/B1/B2/brownState above, since the two run independently (Pluck's own noise advances
    // unconditionally every tick; the bow-noise term only advances at bowAmount > 0), driven by
    // different raw white streams (rngState vs bowNoiseRngState).
    float bowPinkB0 = 0.0f;
    float bowPinkB1 = 0.0f;
    float bowPinkB2 = 0.0f;
    float bowBrownState = 0.0f;

    // One-pole lowpass state for the bow-noise term - see nextBowNoiseSample()'s own comment for
    // why raw white noise measured as reading like hiss layered on the tone rather than woven-in
    // bow character, and why filtering it (real bow surface-noise isn't flat white noise either)
    // was the fix, confirmed by ear.
    float bowNoiseLowpassState = 0.0f;

    // Both derived from Brightness, cached in setBrightness() rather than recomputed every sample
    // in nextNoiseSample() (see that method's own comment) - 1.0/1.0 are Brightness=1.0's own exact
    // values (a bit-exact passthrough), matching this class's default before setBrightness() is
    // ever called.
    float lowpassAlpha = 1.0f;
    float brightnessCompensationGain = 1.0f;
    float bowAmount = 0.0f;

    // STK's own "Bow Pressure" default (slope=3.0, the midpoint of its 1.0-5.0 span) - a real,
    // literature-anchored default, not invented - see setBowForce()'s own comment.
    float bowForce = 0.5f;

    double sampleRateHz = 44100.0;
    float baseDurationSamples = 100.0f;

    enum class Stage { Idle, Attack, DecayToSustain, Release };
    Stage stage = Stage::Idle;
    float envelope = 0.0f;

    // Fixed attack/decay/release-shaping constants - see nextExcitationSample() for how they
    // combine with bowAmount and this note's own pitch. All three stages interpolate their time
    // constants the same way (linearly in time, not in coefficient) specifically to avoid the
    // mismatched-curves bug described in the class comment above.
    static constexpr float fastAttackSamples = 2.0f;      // near-instant - matches the old burst's
                                                           // implicit instant attack
    static constexpr float slowAttackSeconds = 0.15f;     // ~150ms slow attack at full bow

    // durationMultiplier ties bowAmount=0's decay time to this note's own period, same as the
    // original one-shot burst's implicit duration. slowDecaySeconds only needs to be a modest,
    // musical "settle into sustain" time now (not an extreme multi-second value) - sustainLevel,
    // not decay time, is what makes a held bow note actually stay loud.
    static constexpr float durationMultiplier = 2.0f;
    static constexpr float slowDecaySeconds = 0.3f;

    // A short but nonzero release even at bowAmount=0 avoids a click if noteOff() lands while the
    // envelope is still non-negligible; slowReleaseSeconds gives a full bow's release a gentle,
    // audible "lifting the bow off the string" tail instead of an instant cutoff.
    static constexpr float fastReleaseSeconds = 0.005f;
    static constexpr float slowReleaseSeconds = 0.1f;

    // Friction model constants - ported from STK's own defaults/span where noted, otherwise
    // reasoned starting points pending the mandatory render/measure pass this project's own
    // established discipline requires before treating any of these as final (see git history/PR
    // discussion for the actual measured numbers once that pass happens).
    // vBow's own scale. NOT set to Strike's own signal amplitude directly (~0.3-1.6 pre-
    // waveshape, see README) - checked numerically before picking this (see git history/PR
    // discussion): the friction curve's own "sticking" region is narrow (rho only stays away from
    // its 0.01 floor for small |vDelta|), so a vBow of 1.0 pushes vDelta straight into the floor
    // across nearly the entire Bow Force range even with stringSignal=0 (no cancellation at all) -
    // a sustained bow tone would go almost silent right as the envelope reaches full sustain,
    // reintroducing this file's own historical bug #3 in a new form. 0.2 keeps rho off the floor
    // across the full Bow Force span at stringSignal=0 (the isolated worst case, no help from the
    // loop's own signal tracking the bow) - still a reasoned starting point pending the mandatory
    // render/measure pass, not a final, measured value.
    static constexpr float maxBowVelocityAnalog = 0.2f;
    static constexpr float stringVelocityGain = 1.0f;      // scales stringSignal into the same range.
    static constexpr float minFrictionSlope = 1.0f;        // STK's own BowTable::setSlope() span.
    static constexpr float maxFrictionSlope = 5.0f;
    static constexpr float frictionOffset = 0.0f;          // symmetric friction curve, matching
                                                            // STK's own default (no directional-bow
                                                            // asymmetry control exposed).
    static constexpr float frictionMinOutput = 0.01f;      // STK's own BowTable defaults, ported
    static constexpr float frictionMaxOutput = 0.98f;      // directly, not re-derived.
    static constexpr float frictionLevelAnalog = 1.0f;     // NEW loudness constant, analogous to
                                                            // StrikeVoice.h's continuousLevelAnalog
                                                            // but for the friction mechanism
                                                            // specifically - MUST be retuned by the
                                                            // mandatory loudness-parity pass.

    // Bow-noise amount - see nextFrictionSample()'s own comment for why this exists at all (without
    // it, a held bow note measures EXACT silence, not just quiet). The first render/listen pass
    // (at 0.55, unfiltered) confirmed the loudness-parity tests but was judged, by ear, to read as
    // continuous hiss layered on the tone rather than bow character - continuous noise fed into a
    // resonant loop doesn't get absorbed into the pitched resonance the way a one-shot pluck burst
    // does (each tick replenishes fresh broadband energy, so it never fully settles into pure
    // resonance the way a burst's decay does). Filtering it (bowNoiseLowpassCoeff below) attenuates
    // its own RMS substantially, so this was raised back up (1.1) to restore the same measured
    // loudness-parity numbers the original unfiltered pass had - the filtering changes the noise's
    // COLOR (breath-like, not hiss-like), not how loud the final tone needs to be to pass the
    // existing loudness tests.
    static constexpr float bowNoiseAmount = 1.1f;

    // One-pole lowpass coefficient for the bow-noise term (see bowNoiseLowpassState's own comment) -
    // shapes the noise toward low-frequency "breath"/texture rather than full-bandwidth hiss, both
    // because that's closer to how real bow surface-noise actually sounds and because it reduced
    // how much the noise read as literal static once mixed with the resonant tone. Reasoned
    // starting point pending confirmation by ear, same convention as every other new constant here.
    static constexpr float bowNoiseLowpassCoeff = 0.15f;

    // Pink noise's own DC-normalized output range differs substantially from White's own [-1,1]
    // (each of the three IIR stages has a large individual DC gain, e.g. pinkB0's alone is
    // ~0.099/(1-0.99765)~=42x) - measured (not assumed) via a dense render before picking this,
    // same discipline as every other new loudness constant in this codebase.
    static constexpr float pinkNormalizationGain = 0.354f;

    // Fixed, low cutoff - deliberately well below Brightness's own darkest setting (minLowpassAlpha
    // = 0.02 in the .cpp) so Brown reads as a genuinely different, bassier color, not a duplicate of
    // "Brightness = 0". Measured (not assumed) to keep RMS comparable to White's own.
    static constexpr float brownLowpassCoeff = 0.006f;
    static constexpr float brownNormalizationGain = 19.5f;
};
