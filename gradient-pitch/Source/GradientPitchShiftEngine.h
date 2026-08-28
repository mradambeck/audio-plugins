#pragma once

#include "GradientDelayBuffer.h"

#include <cstdint>

// The "core engine" - a single mono unit implementing the H910/H949-style dual-tap crossfading
// pitch shifter, with Feedback, Splice mode, and Drift. This is the class duplicated for dual mode
// (Milestone 6), so it deliberately has no dependency on APVTS or JUCE beyond GradientDelayBuffer,
// and no static/shared mutable state anywhere - every piece of state is a plain member, safe to
// have two independent instances running at once. Drift's own RNG in particular must be a plain
// per-instance member (not shared/static) so two engines don't drift in lockstep.
//
// Crossfade design: each tap's gain is an independent function of its OWN current distance to its
// nearest reset boundary (0 or rampWindowSamples) - full gain once it's more than
// crossfadeLengthSamplesEffective away from either boundary, fading linearly to 0 right at the
// boundary. The two gains are then normalized to sum to exactly 1 every sample. Because the two
// taps are always exactly half a window apart, whenever one tap is fading out near its boundary
// the other is comfortably in its own full-gain zone (nowhere near a boundary) - so the blend is
// always between "a tap easing toward silence" and "an already-stable tap", never between two
// taps at very different delay values. (An earlier, stateful design that blended between
// whichever tap was "active" and a fixed one-shot crossfade timer got this wrong: it held the
// outgoing tap and force-completed the fade on a timer decoupled from the tap's own position,
// which could blend across nearly half the ramp window's worth of delay difference - a large,
// wrong-direction swing baked into every splice, independent of crossfade length. This stateless,
// per-tap, distance-based, normalized design has no such state to get out of sync in the first
// place - reset detection is simply "wrap immediately on crossing the boundary", which is safe
// because gain is already ~0 there.
class GradientPitchShiftEngine
{
public:
    // Glitch: short fixed crossfade, wraps immediately on crossing the boundary (H910 character,
    // audible seam by design). De-glitch soft: same immediate wrap, but the user's (longer)
    // crossfadeLengthMs replaces the fixed constant (H949 alg-1 - smoother, "swimming" at extremes).
    // De-glitch smart: same user crossfade length as soft, PLUS the wrap itself is delayed (up to a
    // small search budget) until the tap's own read content hits a zero-crossing or low-energy
    // moment - a cheap, real-time-feasible stand-in for H949's autocorrelation-based ALG-3, per the
    // implementation plan's explicit simplification.
    enum class SpliceMode { glitch, deglitchSoft, deglitchSmart };

    void prepare(double sampleRate) noexcept;
    void reset() noexcept;

    // Test/tooling-only override for the address-derived RNG seed (see prepare()) - lets offline
    // renders (GradientRenderIR) reproduce bit-identical Drift sequences across separate process
    // invocations, where ASLR would otherwise vary this instance's own address every run. Never
    // called from PluginProcessor's live audio path; production behavior (each instance seeded
    // from its own address, so two simultaneous engines never lock-step - see GradientDriftTests)
    // is unchanged. Call after prepare(), which is the only other place driftRngState is set.
    void setDriftSeedForTesting(uint32_t seed) noexcept;

    // Per-block parameter setters, called once per block from the processor rather than having
    // the engine read juce::AudioProcessorValueTreeState itself - keeps this class trivially
    // testable and duplicable.
    void setPitchSemitones(float coarseSemitones, float fineCents) noexcept;
    void setDelayTimeMs(float delayMs) noexcept;
    void setFeedback(float feedbackPercent) noexcept;
    void setMix(float mixPercent) noexcept;
    void setOutputTrimDb(float trimDb) noexcept;
    void setSpliceMode(SpliceMode mode) noexcept;
    void setCrossfadeLengthMs(float ms) noexcept;
    void setDrift(float driftPercent) noexcept;

    // externalFeedbackSample is 0 in mono (Milestones 1-5); Milestone 6d's cross-feedback wiring
    // passes the other engine's previous-sample wet output here, summed into the SAME feedback
    // junction as this engine's own regeneration (see process()) - not a redesign, since the API
    // already had this parameter from Milestone 2 onward.
    float process(float drySample, float externalFeedbackSample = 0.0f) noexcept;

    // The wet (post-pitch-shift, pre-Mix) signal from the most recent process() call - this is
    // what both this engine's own Feedback (Milestone 3) and dual mode's cross-feedback
    // (Milestone 6d) tap, not the dry/wet-mixed value process() returns.
    float getLastWetSample() const noexcept { return lastWetSample; }

    // Required delay-buffer capacity (in samples) for a given sample rate and the parameter
    // ranges this engine supports - exposed so the processor can size the buffer once in
    // prepareToPlay per the implementation plan's summed-worst-case sizing rule.
    static int requiredBufferCapacitySamples(double sampleRate) noexcept;

private:
    float readTap(int tapIndex, float driftOffsetSamples) const noexcept;
    static float trapezoidGain(float delay, float window, float crossfadeLength) noexcept;
    float getEffectiveCrossfadeSamples() const noexcept;

    // Advances one tap by rampRate and handles its boundary crossing per the current splice mode -
    // immediate wrap for glitch/soft, or (smart) freezing at the boundary and searching for a
    // favourable moment (see advanceTap()'s definition for the full search logic).
    void advanceTap(int tapIndex, bool pitchingUp, float driftOffsetSamples) noexcept;

    // Drift's noise source: a tiny, fast, per-instance xorshift PRNG rather than juce::Random, to
    // keep this class free of any JUCE dependency beyond GradientDelayBuffer (the same reasoning
    // that's kept the rest of the engine JUCE-independent since Milestone 2). Seeded from this
    // instance's own address in prepare() so two engines (dual mode, Milestone 6) never drift in
    // lockstep - the real reason Alloy's own Age technique insists on a per-instance RNG.
    float nextDriftNoise() noexcept;

    // Fixed (non-user-controllable) safety stage for the feedback path - tanh soft-clips runaway
    // regeneration and the one-pole lowpass darkens each pass, matching Caverns' own feedback
    // safety-limiting philosophy so self-oscillation is stable rather than a digital-clipping mess.
    float safetyDarken(float x) noexcept;

    // DC blocker for the feedback bus, found necessary by ear + a targeted test: pitch-shifting a
    // pure DC (or near-DC) signal costs ZERO energy at the splice (blending two identical values
    // loses nothing), so DC entirely bypasses the splice-loss mechanism that limits ordinary tonal
    // self-oscillation - meaning a silent, sub-audible DC/near-DC bias can self-sustain even at
    // Feedback settings well below what any audible tone needs (confirmed: stuck at 100% feedback,
    // far under the ~150-200% threshold real tones need), persisting indefinitely and not decaying
    // when Feedback is lowered. Standard fix, present in nearly every analog delay pedal's feedback
    // path: a one-pole DC-blocking highpass, fixed cutoff well below musical bass content.
    float dcBlock(float x) noexcept;

    // Final output safety ceiling - a fixed, gentle tanh applied to the returned sample, separate
    // from safetyDarken() (which shapes the internal feedback bus, not the final output). Without
    // this, self-oscillating feedback combined with a positive Output Trim can genuinely exceed
    // +-1.0 for a sustained period (confirmed: RMS ~1.4 measured on a real self-oscillating case) -
    // which host/OS/audio-interface protective limiting can react to by muting output entirely,
    // surfacing as "the plugin just stops working" rather than an audible clip. tanh at unity drive
    // is close to transparent for normal signal levels and only engages near/above full scale.
    static float outputSafetyLimit(float x) noexcept;

    GradientDelayBuffer buffer;
    double sampleRateHz = 44100.0;

    // Live parameter values, set per block.
    float pitchSemitones = 0.0f;
    float pitchFineCents = 0.0f;
    float delayTimeSamples = 0.0f;
    float feedbackGain = 0.0f;
    float mixAmount = 0.5f;
    float outputGain = 1.0f;
    float lastOutputTrimDb = 0.0f; // cache so setOutputTrimDb() can skip a redundant pow() below
    SpliceMode spliceMode = SpliceMode::glitch;
    float crossfadeLengthMs = fixedCrossfadeMs; // live only for deglitchSoft/deglitchSmart
    float driftAmount = 0.0f; // 0-1, from setDrift()

    // Derived from pitch each time setPitchSemitones() is called.
    float rampRate = 0.0f;                          // delay-samples change per audio-sample
    float rampWindowSamples = 0.0f;                  // full excursion each tap sweeps before reset

    // Tap state - persists sample to sample. Both taps always continuously ramp and wrap freely
    // (glitch/soft) or briefly hold at the boundary while deglitchSmart searches for a splice
    // point; there's no "active tap" concept, just two independent gain-weighted reads.
    float tapDelay[2] { 0.0f, 0.0f };

    // De-glitch smart's per-tap splice search state - see advanceTap().
    bool awaitingSplice[2] { false, false };
    int spliceSearchCountdown[2] { 0, 0 };
    float lastTapSample[2] { 0.0f, 0.0f };
    int maxSpliceSearchSamples = 0; // computed in prepare()

    // Cumulative real-time samples each tap has spent searching, ever. Only ever compared as a
    // DIFFERENCE between the two taps (see advanceTap()) to throttle a tap's search budget once
    // it's drifted ahead of its partner in accumulated search time - without this, independent
    // per-tap search timing random-walks the two taps out of their exact half-window phase
    // relationship, which both audibly surfaces as extra irregular dropouts (confirmed by ear) and
    // costs feedback sustain (confirmed by GradientFeedbackTests).
    float searchDebt[2] { 0.0f, 0.0f };

    // Drift state: a one-pole-lowpassed random walk (Alloy's "Age" technique), NOT a literal LFO -
    // slow, organic, non-periodic wander. Applied as a small additive offset onto the value fed to
    // both the reset-boundary test and the actual buffer read (see advanceTap()/readTap()), per the
    // implementation plan's Milestone 2 decision to keep reset detection value-driven specifically
    // so Drift could be added this way, without touching the reset-detection mechanism itself.
    uint32_t driftRngState = 1;
    float driftState = 0.0f;
    float driftCoeff = 1.0f;             // one-pole lowpass coefficient, computed in prepare()
    float maxDriftExcursionSamples = 0.0f; // computed in prepare(), matches the buffer's reserved headroom

    float lastWetSample = 0.0f;

    // Feedback path state.
    float darkeningFilterState = 0.0f;
    float darkeningCoeff = 1.0f; // one-pole lowpass coefficient, computed in prepare()
    float dcBlockerPrevInput = 0.0f;
    float dcBlockerPrevOutput = 0.0f;
    float dcBlockerR = 0.995f; // computed in prepare() from dcBlockerCutoffHz

    // Internal, not user-exposed until Milestone 4 gives Splice mode a real crossfade-length
    // parameter (Glitch mode will keep using a short fixed value close to this one; the two
    // de-glitch modes will use the user's knob instead).
    static constexpr float rampWindowMs = 30.0f;
    static constexpr float fixedCrossfadeMs = 5.0f;

    // De-glitch smart's splice-timing search: how long (at most) a tap may hold at its boundary
    // waiting for a zero-crossing/low-energy moment before the wrap is forced anyway, and the
    // amplitude below which a sample counts as "low energy" on its own (a cheap secondary trigger
    // for signals that don't clearly zero-cross, e.g. DC-biased or already-near-silent content).
    // Kept deliberately short (1ms, not the first-cut 5ms): each tap searches independently, so
    // every wrap's data-dependent extra delay nudges the two taps' relative phase slightly out of
    // their exact half-window alignment, and that drift accumulates over many wraps. Empirically,
    // 1ms preserves the full pitch-accuracy improvement (real zero-crossings are found quickly in
    // practice) while measurably reducing the drift's cost to feedback sustain - see
    // GradientFeedbackTests for the measurement. A nonzero window always accumulates some drift;
    // this is a structural cost of independent per-tap search versus true coupled autocorrelation.
    static constexpr float maxSpliceSearchMs = 1.0f;
    static constexpr float lowEnergyThreshold = 0.01f;

    // Fixed feedback-safety constants. darkeningCutoffHz matches Caverns' own darkening filter,
    // but fixedSafetyDrive does NOT reuse Caverns' ~1.6 value - empirically (see GradientFeedbackTests
    // and the implementation plan's Milestone 3 notes), a tanh feedback loop settles to a stable
    // equilibrium amplitude once feedback exceeds unity, and that equilibrium loudness is inversely
    // proportional to drive. Caverns' 1.6 left Gradient's loop converging to a quiet, barely-audible
    // plateau at 115% feedback instead of a satisfying self-oscillation (measured ~0.16 RMS on a
    // test tone); 0.6 raises that to ~0.44 RMS while every sample stays hard-bounded below
    // 1/drive = 1.67, safely short of digital clipping.
    static constexpr float fixedSafetyDrive = 0.6f;
    static constexpr float darkeningCutoffHz = 4200.0f;

    // Well below any musical bass content, but high enough to actually clear DC/near-DC buildup
    // within a reasonable time (a 20Hz one-pole highpass settles in tens of milliseconds).
    static constexpr float dcBlockerCutoffHz = 20.0f;

    // Drift's lowpass cutoff. What actually makes Drift audible is its RATE of change (d(delay)/dt
    // - the same quantity that drives pitch shift via rampRate), not just how far it wanders: a
    // slow-but-tiny cutoff (the original 0.3Hz) produced a rate of change roughly 150x smaller than
    // a real pitch-shift ramp, which by-ear testing confirmed was inaudible even at 100% Drift.
    // 1Hz keeps the walk clearly non-periodic (it's filtered noise, not a sine oscillator - raising
    // the cutoff doesn't introduce periodicity) while giving it enough rate of change, combined
    // with maxDriftExcursionMs, to be unmistakably audible at higher settings.
    static constexpr float driftLowpassCutoffHz = 1.0f;
};
