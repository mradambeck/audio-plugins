#pragma once

#include <cstdint>

// The Excitation seam: "generate one sample of excitation per tick, shaped by a live
// Attack/Decay/Sustain/Release envelope." Deliberately no JUCE include at all (own xorshift PRNG
// instead of juce::Random) - matches gradient-pitch/GradientPitchShiftEngine's convention of
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
// To add a new excitation variant (filtered noise with a different colour, a sample-based burst,
// etc.), write a new class matching this same method set (prepare/reset/setBrightness/
// setBowAmount/getBowAmount/setBaseDuration/noteOff/nextExcitationSample/setSeed) and swap the
// template argument in KarplunkVoice.h's KarplunkStringLineChannel instantiation - nothing in
// KarplunkLoopFilter.h, KarplunkStringLine.h, or KarplunkVoice.h needs to change. setSeed() (see
// its own comment) joined this set only once the Feedback Topology seam grew a second option
// that needs two independently-noisy Excitation instances - every Excitation variant needs it now,
// not just NoiseExcitation.
class NoiseExcitation
{
public:
    void prepare(double sampleRate) noexcept;
    void reset() noexcept;

    // 0 = fully darkened (one-pole lowpassed) noise, 1 = raw white noise. Applied inside
    // nextExcitationSample(), not as a separate post-process pass.
    void setBrightness(float amount01) noexcept;

    // 0 = pure pluck: fast attack, decays fully away (sustain level 0) over a time tied to this
    // note's own period - see setBaseDuration(). 1 = pure bow: slow attack, settles into and holds
    // at full-amplitude sustain indefinitely. Every stage's time constants AND the sustain level
    // itself move with this, live, every tick - see nextExcitationSample().
    void setBowAmount(float amount01) noexcept;
    float getBowAmount() const noexcept { return bowAmount; }

    // Sets the RNG seed directly - not part of every Excitation variant's normal per-note
    // lifecycle (Structure/Position/etc. never call this); used by KarplunkVoice's dual-topology
    // orchestrator to give its second line's Excitation a genuinely different noise sequence from
    // the first, called once at prepare() time. Without this, two identically-constructed,
    // identically-driven Excitation instances would produce bit-identical noise, making
    // cross-coupling and detune both silently inaudible - see KarplunkVoice.h's own comment.
    // xorshift32 is degenerate at seed 0, so 0 is remapped to 1 (matching this class's own default).
    void setSeed(uint32_t seed) noexcept { rngState = seed != 0 ? seed : 1; }

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
    // KarplunkStringLineChannel no longer gates this by a `held` flag. An idle (never-triggered)
    // excitation returns exactly 0; a fully-released one converges to (very close to) 0 on its
    // own and just stays there. Never allocates.
    float nextExcitationSample(float velocity01) noexcept;

private:
    float nextNoiseSample() noexcept;

    uint32_t rngState = 1;
    float lowpassState = 0.0f;
    float brightness = 1.0f;
    float bowAmount = 0.0f;

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
};
