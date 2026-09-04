#pragma once

#include <algorithm>
#include <array>

#include "../../common/dsp/CircularDelayBuffer.h"
#include "../../common/dsp/OnePoleFilter.h"
#include "../../common/dsp/TiltFilter.h"

// The AMS RMX16 "Non-Lin 2" reverb core: a 16-line feedback delay network (FDN, see numLines'
// comment for why 8 wasn't enough) with Hadamard mixing, a short input diffuser, a bass/treble
// tilt filter (H), and - the part that actually
// makes this read as Non-Lin 2 rather than a generic FDN reverb - a separate multiplicative
// envelope shaper (hold, then decay) layered on top of the FDN's own near-steady-state tail.
//
// Architecture is driven directly by analysis/findings.md (empirical measurement of the 19
// captured reference IRs), not guessed:
//   - Envelope shape: every capture showed a hold near the peak (flat within -5..-10dB for
//     roughly the first quarter of the decay) before a knee into a steeper fall - NOT a plain
//     exponential. That is why the envelope is a separate hold+decay stage rather than left to
//     the FDN's own decay.
//   - H ("Tilt"): a broadband tilt pivoting near 1-4kHz (bass up + treble down as H goes more
//     negative), present from the very first energy in the signal, not just a slow darkening of
//     the tail - hence TiltFilter applied at the input AND folded into the per-line feedback
//     damping, not a plain one-pole lowpass buried only in the feedback loop.
//   - Tighter: scales the envelope's hold-time fraction (shorter hold = faster time-to-dense-
//     diffusion, matching every one of 9 measured pairs in findings.md) AND raises the input
//     diffuser's allpass density toward that same denser/smoother field more directly (added
//     2026-08-29 - hold-time scaling alone was real but too subtle to hear as a "Smoothing"
//     control (named "Diffusion" at the time), see setSpacingMultiplier()'s implementation
//     comment).
//
// No discrete early-reflection taps: an earlier version fed a fixed multi-tap pattern (matching
// findings.md's measured ER taps) straight to the output alongside the tank. Adam's ear caught
// what that measurement-driven choice missed - discrete taps are just delayed/scaled copies of
// the input, so however accurate to the captures, they read as audible slapback echoes of the
// source ("this sounds nothing like it should... it should just be the gated reverb tail"), not
// reverb character. Removed entirely rather than just turned down, on direct instruction - the
// tank's own natural buildup (through its diffuser and shortest lines) is what supplies the
// early-decay energy now.
class IntruderFDNEngine
{
public:
    IntruderFDNEngine() = default;

    void prepare(double sampleRate);
    void reset();

    // Seconds - this is the plugin's own perceptually-meaningful decay control (how long the
    // shaped envelope takes to fall to silence), not a literal reproduction of the hardware's
    // compressed 0.1-9.8 display scale (see findings.md's "Decay" section for why that scale
    // isn't seconds at all). Continuous and extrapolable well past the ~0.2-0.5s measured range.
    void setDecaySeconds(float seconds);

    void setPreDelayMs(float ms);

    // tiltDb drives TiltFilter (bass up / treble down as this goes negative, pivoting at ~2kHz)
    // and the FDN feedback damping weight. This is a DSP-ready coefficient, NOT the raw H control
    // value - the engine has no interpolation/measured-data logic of its own by design (see
    // IntruderParameterMap.h's class comment for why that's kept separate). The caller (the
    // processor) is responsible for mapping H through IntruderParameterMap::mapTiltDbFromH() first.
    void setTilt(float tiltDb);

    // Threshold, in dBFS, for the input transient detector's rising edge (a new gate-open "hit").
    // NOT hardware-derived - see triggerHysteresisGapDb's comment in the .cpp for why this is a
    // user-facing control rather than a fixed internal constant. The falling (release) edge sits a
    // fixed 12dB below whatever this is set to, not independently exposed.
    void setTriggerFloorDb(float db);

    // Multiplies the envelope's hold-time fraction (1.0 = no effect, <1.0 = tighter/faster time-
    // to-decay) AND raises the input diffuser's allpass coefficient toward a denser/smoother early
    // field as it drops below 1.0 - see the .cpp's implementation comment for why both are driven
    // from this one value rather than exposing a second control. Same separation-of-concerns as
    // setTilt(): this is the already-mapped multiplier, not a raw Tighter 0-1 value - see
    // IntruderParameterMap::mapTighterToSpacingMultiplier().
    void setSpacingMultiplier(float multiplier);

    // In-place stereo process: L/R in, replaced with the wet signal out. Dry/wet mixing and
    // input/output gain happen in the processor, not here, so this class stays a pure "wet
    // generator", same convention as ShieldsFDNEngine.
    void processStereo(float* left, float* right, int numSamples);

private:
    // 8 lines proved insufficient modal density - Phase 6 validation against the 19 reference
    // captures (analysis/validation_report.md) measured spectral flatness off by up to -49dB at
    // longer decay settings (comb-filtered/tonal tail instead of the reference's dense diffuse
    // wash), and delay-line wobble alone (see wobbleDepthMs below) only closed part of that gap.
    // Doubling to 16 lines roughly doubles the tank's own modal density directly, which is the
    // more structural fix - re-verified empirically afterward, not just assumed to help.
    static constexpr int numLines = 16;

    // Mutually prime, non-whole-millisecond line lengths, spread ~12-115ms (geometrically spaced
    // plus per-line jitter so no two lines share a simple ratio) - avoids periodic reinforcement/
    // comb peaks at 1kHz multiples (see shields-reverb's ShieldsFDNEngine for the reasoning this
    // convention is copied from) and spreads modal density across the widest practical frequency
    // range for a plate/room-sized tail.
    static constexpr std::array<float, numLines> baseLineLengthsMs {
        12.3f, 14.4f, 16.4f, 19.5f, 22.0f, 26.0f, 29.9f, 35.2f,
        40.1f, 47.0f, 54.8f, 63.2f, 73.7f, 85.3f, 99.5f, 115.3f
    };

    // Slow, per-line, mutually-detuned sinusoidal drift on each tank line's read position - blurs
    // the fixed resonant peaks an FDN of this size inevitably has at high feedback into motion,
    // rather than letting them ring statically. Same technique as shields-reverb's opt-in "Wobble"
    // (off by default there, since Shields' real hardware reference is itself static/grainy by
    // design) - here it's always on, since Phase 6 validation directly measured the opposite
    // problem: without it, spectral flatness vs. the real RMX16 captures was off by up to -49dB at
    // longer decay settings (the tail reads as a few ringing tones, not the reference's dense
    // diffuse wash) - see analysis/validation_report.md. Rates deliberately mutually non-
    // commensurate (no shared factors) so all 16 lines drift out of phase with each other rather
    // than breathing in lockstep, same rationale as the line lengths themselves.
    //
    // At full depth continuously, this caused a second bug (Adam: "a weird little ping of volume
    // increase... more noticeable on extended/held notes"): on a long, actively-changing decay
    // the modulation is masked by the tail's own falling level, but once the sustain floor (see
    // baseHoldFraction's comment) holds the level flat for as long as a note is held, the SAME
    // modulation has nothing to hide behind and reads as an obvious, unwanted slow amplitude
    // "breathing" - confirmed empirically (not assumed) by rendering a 20s held note and diffing
    // against a wobbleDepthMs=0 render: identical shape, and the flat-sustain portion went from
    // wandering over a 20-30dB range to a dead-flat line with Wobble zeroed. A uniformly lower
    // depth (tried 1.5ms) reduced but didn't eliminate it, and would also water down the
    // resonance-blurring benefit Phase 6 relies on during real decay - see
    // getWobbleDepthSamples() instead: full depth while the envelope is actively changing (hold
    // or natural decay, where Phase 6 measured this mattering and the change masks the
    // modulation), scaled down once the envelope has actually settled flat at the sustain floor
    // (a state Phase 6's impulse-only validation never exercised in the first place, so nothing
    // there was tuned against it).
    static constexpr std::array<float, numLines> wobbleRateHz {
        0.079f, 0.089f, 0.101f, 0.113f, 0.127f, 0.139f, 0.151f, 0.163f,
        0.173f, 0.181f, 0.191f, 0.199f, 0.211f, 0.223f, 0.229f, 0.241f
    };
    // TEMPORARY - 0 to A/B test whether Wobble is the source of the "warbly" character Adam's
    // hearing (2026-08-28). Was 4.5f. Revert to 4.5f (or decide on a different value) once that's
    // confirmed either way - zero loses the resonance-blurring benefit this was added for
    // (see wobbleRateHz's comment).
    static constexpr float wobbleDepthMs = 0.0f;
    // 0: fully off once flat-sustaining, not just reduced. A partial factor (tried 0.12) still
    // left a slow, clearly audible amplitude swing over a full modulation cycle (4-12s - see
    // wobbleRateHz) - the interference between 16 long delay lines adds up over a full period
    // even from a small per-sample depth, it doesn't shrink away proportionally. Zero is also the
    // musically sound choice here, not just the cleanest measurement: flat-sustain has no
    // decaying resonance left to blur into motion in the first place (that's what Wobble is for
    // during the hold/decay phase), so there's nothing lost by switching it off, only the
    // artifact.
    static constexpr float sustainWobbleDepthFactor = 0.0f;

    static constexpr std::array<float, 3> inputAllpassDelaysMs { 3.7f, 2.3f, 1.7f };

    struct AllpassStage
    {
        wildjag::dsp::CircularDelayBuffer buffer;
        int delaySamples = 1;
        float coefficient = 0.5f;

        // delaySamplesIn is the actual delay to use - NOT a capacity hint (that distinction matters:
        // an earlier version passed a capacity-with-headroom value straight through as the delay,
        // silently adding a few extra samples to every allpass stage).
        void prepare(int delaySamplesIn)
        {
            delaySamples = std::max(1, delaySamplesIn);
            buffer.setSize(delaySamples + 1);
        }

        void reset() { buffer.reset(); }

        float processSample(float x)
        {
            const auto bufOut = buffer.read(delaySamples - 1);
            const auto y = -coefficient * x + bufOut;
            buffer.write(x + coefficient * y);
            return y;
        }
    };

    // 8x8 Hadamard matrix (+-1 entries, Sylvester construction), normalised by 1/sqrt(8) at use
    // time - an orthogonal, energy-preserving mix (same rationale as shields-reverb's).
    static const std::array<std::array<float, numLines>, numLines> hadamard;

    double sampleRateHz = 44100.0;
    bool prepared = false;

    std::array<wildjag::dsp::CircularDelayBuffer, numLines> lineBuffers;
    std::array<int, numLines> lineDelaySamples {};
    std::array<float, numLines> feedbackGain {};
    std::array<wildjag::dsp::OnePoleFilter, numLines> dampingFilter;
    std::array<float, numLines> wobblePhase {};
    std::array<float, numLines> wobblePhaseStep {}; // radians/sample, computed in prepare()
    float wobbleDepthSamplesF = 0.0f; // computed in prepare()

    std::array<AllpassStage, 3> allpassL, allpassR;

    wildjag::dsp::CircularDelayBuffer preDelayBufferL, preDelayBufferR;
    int preDelaySamples = 0;
    int maxPreDelaySamples = 1;

    wildjag::dsp::TiltFilter tiltL, tiltR;

    // Envelope shaper: attack is effectively instant (every capture rises to peak within a few
    // ms - see findings.md), then a hold at unity gain, then a decay down to a sustain floor
    // (NOT to silence - see sustainLevelLinear below) for as long as the input keeps playing, then
    // a release to true silence once the input actually stops. holdSeconds and decaySeconds both
    // scale with the Decay control; holdSeconds is additionally scaled by spacingMultiplier (see
    // setSpacingMultiplier()'s comment).
    //
    // This took three attempts - the first two are worth keeping on record so a fourth doesn't
    // repeat them. A continuously-held/sustained input (a note held well past one Decay cycle,
    // not a drum-hit-style transient) only retriggered once at the first rising edge, since
    // nothing in a continuously-loud signal re-crosses the trigger threshold afterward - the wet
    // signal decayed once and stayed quiet for as long as the input kept playing. (1) Releasing
    // as soon as the input dropped below the threshold seemed like a fix but broke the far more
    // common case instead: it cut a plain impulse's tail from ~2s down to ~0.1s, since normal
    // transients drop below threshold almost immediately after the hit. (2) Auto-restarting the
    // cycle whenever it had run to completion AND the input was still active fixed the sustained
    // case without touching impulse decay, but produced an obvious, unwanted rhythmic
    // tremolo/pumping on ANY sustained input - confirmed by ear on the Standalone app, default
    // 1.5s Decay, pumping rate tracking the Decay knob exactly - which is worse than the bug it
    // fixed. Both reverted. The actual fix (per Adam: "it's reverb, shouldn't it maintain a sound
    // vs retriggering?") is a genuine sustain level: decay down to a floor and HOLD there smoothly
    // while input remains active (no retriggering, no periodic re-attack), only fading to true
    // silence once the input actually stops. The key difference from attempt (1) that avoids
    // breaking short transients: release is only armed once the natural decay curve has actually
    // reached the floor (see the release-arming check in processStereo()) - a short hit's input
    // drops back below threshold long before its own decay gets anywhere near the floor, so this
    // correctly does nothing for that case and the full natural decay proceeds untouched, exactly
    // as attempt (1) failed to preserve.
    static constexpr float baseHoldFraction = 0.3f; // fraction of decaySecondsParam spent at unity gain before decaying, at spacingMultiplier == 1

    // -20dB: audibly a settle/gate character while a note is held, not silence, but still clearly
    // quieter than the hold level - matches "maintain a sound" without undermining Decay's own
    // character (a short Decay setting still reads as tight/gated once past its own hold+knee).
    static constexpr float sustainLevelLinear = 0.1f;
    float decaySecondsParam = 2.0f;
    float spacingMultiplier = 1.0f;
    double envelopePhaseSamples = 0.0;
    float envelopeGain = 1.0f;
    wildjag::dsp::OnePoleFilter envelopeSmoother; // smooths the hold->decay knee to avoid a click

    // Release state: armed only when the falling edge (input stopping) coincides with the decay
    // curve already having reached the sustain floor - i.e. only for a genuinely-held note, never
    // for a short transient's own natural (much quicker) input decay. releaseStartGain captures
    // the actual smoothed gain in play at that instant, so the release ramp is continuous - no
    // jump - regardless of how long the note had been sustaining at the floor beforehand.
    bool inRelease = false;
    float releaseStartGain = 0.0f;
    double releasePhaseSamples = 0.0;

    float tiltDbParam = 0.0f;

    // Input transient detector driving envelope retrigger - a standard one-pole peak follower
    // with independent attack/release weights (fast attack so real transients are caught
    // promptly, slow release so it doesn't chatter on the reverb's own residual tail bleeding
    // back into the input... note this tracks the DRY input specifically, not the wet output, so
    // there's no self-feedback loop here regardless). aboveTriggerFloor drives two things (see
    // baseHoldFraction's comment for the full history): a below-to-above transition (re)starts a
    // hold/decay cycle from the top (a new hit); an above-to-below transition arms the release
    // ramp, but only when the decay has actually reached the sustain floor by that point.
    float followerState = 0.0f;
    float followerAttackWeight = 1.0f;
    float followerReleaseWeight = 1.0f;
    bool aboveTriggerFloor = false;

    // Defaults match this plugin's original fixed -36/-48dBFS behavior for anything that never
    // calls setTriggerFloorDb() (e.g. IntruderFDNEngineTests constructs the engine directly) - the
    // processor always calls it in practice, driven by the "Threshold" parameter.
    float triggerFloorLinear = 0.0158f; // ~ -36 dBFS
    float triggerReleaseFloorLinear = 0.0040f; // ~ -48 dBFS

    void updateLineLengths();
    void updateFeedbackGains();
};
