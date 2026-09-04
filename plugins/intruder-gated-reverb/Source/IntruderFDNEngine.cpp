#include "IntruderFDNEngine.h"

#include <algorithm>
#include <cmath>

namespace
{
    constexpr float pi = 3.14159265358979323846f;

    // H8 = H2 (kron) H4, the standard Sylvester-Hadamard construction, +-1 entries (normalised by
    // 1/sqrt(8) at use time in processStereo()) - same rationale as shields-reverb's: an orthogonal
    // mix keeps network stability governed purely by the scalar feedback gain and damping, not by
    // the matrix itself.
    constexpr int fdnLines = 8;

    // Tilt pivot frequency - matches the 2kHz split used throughout analysis/analyze_irs.py's
    // spectral-tilt measurement, so the plugin's own tilt behavior lines up with what was measured.
    constexpr float tiltPivotHz = 2000.0f;

    // Base per-line feedback damping (leaky-integrator weight) applied regardless of H - keeps the
    // tail from being infinitely bright even at H=0, matching every capture's tilt-over-time trend
    // being at worst mildly brightening, never flat/infinite bandwidth.
    constexpr float baseDampingWeight = 0.18f;

    // Extra damping weight added per dB of negative tilt, so H's effect compounds over the tail
    // (not just a fixed one-shot input coloration) - see findings.md's revised H recommendation.
    constexpr float dampingPerNegativeTiltDb = 0.03f;

    // Envelope: attack is a short ramp (every capture rose to peak within a few ms - "instant" at
    // audio-block resolution), and the hold->decay transition is smoothed by envelopeSmoother
    // rather than left as a hard corner, to avoid an audible click at the knee.
    constexpr float envelopeSmootherHz = 80.0f;

    // Input transient detector: a new reverb "trigger" (gate retrigger) fires when the fast input
    // envelope rises above triggerFloorLinear after having been below triggerReleaseFloorLinear -
    // the standard gated-reverb/noise-gate retrigger idiom, with hysteresis (a Schmitt trigger: a
    // higher rise threshold and a separate, lower fall threshold) rather than one shared threshold.
    // Found empirically to matter, not just theoretical: a sample-accurate simulation of a single
    // shared -36dBFS threshold against one of Adam's real loops ("BadVerb.wav", 2026-08-28) showed
    // the follower hovering in a quiet decaying tail between roughly -34 and -40dBFS for tens of ms
    // at a stretch, re-crossing a single -36dBFS line up to 7 times in under 60ms - each crossing
    // yanks the envelope back to full unity gain, which is exactly the "jumps in volume,
    // dramatically cuts" glitchy character Adam reported. Widening the gap between rise and fall
    // thresholds to 12dB (so the follower has to genuinely die away, not just dip a couple dB
    // below the rise point) cut spurious retriggers on that same file from 296 down to 26 over
    // 28s - roughly one per real musical hit, which is what a gate should be doing.
    //
    // -36dBFS itself is NOT a hardware-measured value - IMPLEMENTATION.md's real RMX16 control set
    // has no threshold/sensitivity knob at all, this is purely an internal constant this plugin
    // invented. Checking it against BadVerb.wav's actual level distribution found real, legitimate
    // content sitting close to that exact line (74 transients measured between -36 and -30dBFS, 86
    // more between -45 and -36dBFS) - a fixed number can't fit every source's gain staging, so it's
    // exposed as the "Threshold" parameter (setTriggerFloorDb()) rather than hardcoded. The
    // hysteresis gap stays fixed beneath whatever Threshold is set to, rather than also being
    // exposed, to keep the control surface to one knob.
    //
    // 12dB (chosen when Threshold was still fixed at -36dBFS) turned out to be too wide once
    // Threshold became adjustable down to -60dBFS: Adam found values around -47dBFS and below
    // stopped responding to new hits almost entirely. Simulating against BadVerb.wav explained why
    // - that file's own quiet passages average around -47 to -53dBFS (see the level-distribution
    // comment above), so a 12dB-below-Threshold release floor of -59dBFS or lower was essentially
    // unreachable by real program material: the gate opened once and then got stuck open, riding
    // one envelope cycle for the whole 28s file (1 retrigger total) instead of retriggering per
    // hit. 5dB keeps enough gap to still suppress the original chattering bug at -36dBFS (296 ->
    // ~2 spurious retriggers, vs. 12dB's ~0 - a small residual trade, not a regression to that bug).
    //
    // Correction (2026-08-29): an earlier version of this comment claimed 5dB fixed -47dBFS to 12
    // retriggers - that number came from a simulation that forgot the processor applies Gain
    // BEFORE the engine ever sees the signal (PluginProcessor::processBlock multiplies by
    // inputGain, then calls engine.processStereo()). Redone with Gain correctly included, -47dBFS
    // was STILL stuck at 1 retrigger even with the narrower gap - 5dB helped the chattering case,
    // not the stuck-open case, and a full retrigger-count sweep confirmed the stuck-open cliff on
    // BadVerb.wav actually starts around -50dBFS regardless of gap size. That's what
    // PluginProcessor.cpp's Threshold range (-46..-4dB) is now trimmed to stay clear of, rather
    // than relying on the gap alone to paper over it. This is a real material-dependent limit, not
    // something a fixed gap fully solves: push Threshold below wherever a given source's own quiet
    // passages actually sit and the gate will always eventually stop finding a genuine "off" to
    // detect, on any gap size - narrowing the exposed range just keeps a user from landing there
    // by accident.
    constexpr float triggerHysteresisGapDb = 5.0f;
    constexpr float inputFollowerAttackHz = 3000.0f; // fast: track transients, not just RMS
    constexpr float inputFollowerReleaseHz = 20.0f;
}

// H16 = H2 (kron) H8 (Sylvester construction), verified orthogonal (H*H^T = 16*I) via numpy before
// transcribing - see the class comment on numLines for why this grew from 8 to 16 lines.
const std::array<std::array<float, IntruderFDNEngine::numLines>, IntruderFDNEngine::numLines>
    IntruderFDNEngine::hadamard { {
        { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 },
        { 1, 1, 1, 1, -1, -1, -1, -1, 1, 1, 1, 1, -1, -1, -1, -1 },
        { 1, -1, 1, -1, 1, -1, 1, -1, 1, -1, 1, -1, 1, -1, 1, -1 },
        { 1, -1, 1, -1, -1, 1, -1, 1, 1, -1, 1, -1, -1, 1, -1, 1 },
        { 1, 1, -1, -1, 1, 1, -1, -1, 1, 1, -1, -1, 1, 1, -1, -1 },
        { 1, 1, -1, -1, -1, -1, 1, 1, 1, 1, -1, -1, -1, -1, 1, 1 },
        { 1, -1, -1, 1, 1, -1, -1, 1, 1, -1, -1, 1, 1, -1, -1, 1 },
        { 1, -1, -1, 1, -1, 1, 1, -1, 1, -1, -1, 1, -1, 1, 1, -1 },
        { 1, 1, 1, 1, 1, 1, 1, 1, -1, -1, -1, -1, -1, -1, -1, -1 },
        { 1, 1, 1, 1, -1, -1, -1, -1, -1, -1, -1, -1, 1, 1, 1, 1 },
        { 1, -1, 1, -1, 1, -1, 1, -1, -1, 1, -1, 1, -1, 1, -1, 1 },
        { 1, -1, 1, -1, -1, 1, -1, 1, -1, 1, -1, 1, 1, -1, 1, -1 },
        { 1, 1, -1, -1, 1, 1, -1, -1, -1, -1, 1, 1, -1, -1, 1, 1 },
        { 1, 1, -1, -1, -1, -1, 1, 1, -1, -1, 1, 1, 1, 1, -1, -1 },
        { 1, -1, -1, 1, 1, -1, -1, 1, -1, 1, 1, -1, -1, 1, 1, -1 },
        { 1, -1, -1, 1, -1, 1, 1, -1, -1, 1, 1, -1, 1, -1, -1, 1 },
    } };

void IntruderFDNEngine::prepare(double sampleRate)
{
    sampleRateHz = sampleRate;

    // +wobble headroom: each line's read position drifts by up to +-wobbleDepthMs (see the
    // header's "On diffusion" comment), and readInterpolated() additionally needs one extra
    // sample of headroom past the deepest delay it's asked for.
    const auto wobbleDepthSamples = (int) std::ceil(wobbleDepthMs * 0.001 * sampleRate);
    wobbleDepthSamplesF = (float) (wobbleDepthMs * 0.001 * sampleRate);
    for (int i = 0; i < numLines; ++i)
    {
        const auto maxDelaySamples = (int) std::ceil(baseLineLengthsMs[(size_t) i] * 0.001 * sampleRate) + wobbleDepthSamples + 4;
        lineBuffers[(size_t) i].setSize(maxDelaySamples);
        wobblePhaseStep[(size_t) i] = (float) (2.0 * pi * wobbleRateHz[(size_t) i] / sampleRate);
    }

    for (int i = 0; i < 3; ++i)
    {
        const auto delaySamples = std::max(1, (int) std::round(inputAllpassDelaysMs[(size_t) i] * 0.001 * sampleRate));
        allpassL[(size_t) i].prepare(delaySamples);
        allpassR[(size_t) i].prepare(delaySamples);
        allpassL[(size_t) i].coefficient = 0.5f;
        allpassR[(size_t) i].coefficient = 0.5f;
    }

    maxPreDelaySamples = (int) std::ceil(0.2 * sampleRate) + 4; // 200ms max pre-delay headroom
    preDelayBufferL.setSize(maxPreDelaySamples);
    preDelayBufferR.setSize(maxPreDelaySamples);

    tiltL.setPivotHz(tiltPivotHz, sampleRate);
    tiltR.setPivotHz(tiltPivotHz, sampleRate);

    envelopeSmoother.setCutoffHz(envelopeSmootherHz, sampleRate);

    followerAttackWeight = 1.0f - std::exp(-2.0f * pi * inputFollowerAttackHz / (float) sampleRate);
    followerReleaseWeight = 1.0f - std::exp(-2.0f * pi * inputFollowerReleaseHz / (float) sampleRate);

    prepared = true;

    updateLineLengths();
    updateFeedbackGains();

    reset();
}

void IntruderFDNEngine::reset()
{
    for (auto& buf : lineBuffers) buf.reset();
    for (auto& f : dampingFilter) f.reset();
    wobblePhase.fill(0.0f);
    for (auto& a : allpassL) a.reset();
    for (auto& a : allpassR) a.reset();
    preDelayBufferL.reset();
    preDelayBufferR.reset();
    tiltL.reset();
    tiltR.reset();
    envelopeSmoother.reset(0.0f);
    envelopeGain = 0.0f;
    envelopePhaseSamples = 0.0;
    followerState = 0.0f;
    aboveTriggerFloor = false;
    inRelease = false;
    releaseStartGain = 0.0f;
    releasePhaseSamples = 0.0;
}

void IntruderFDNEngine::setDecaySeconds(float seconds)
{
    decaySecondsParam = std::max(0.05f, seconds);
    if (prepared)
        updateFeedbackGains();
}

void IntruderFDNEngine::setPreDelayMs(float ms)
{
    const auto clampedMs = std::max(0.0f, ms);
    preDelaySamples = std::min((int) std::round(clampedMs * 0.001f * (float) sampleRateHz), maxPreDelaySamples - 1);
}

void IntruderFDNEngine::setTriggerFloorDb(float db)
{
    triggerFloorLinear = std::pow(10.0f, db / 20.0f);
    triggerReleaseFloorLinear = std::pow(10.0f, (db - triggerHysteresisGapDb) / 20.0f);
}

void IntruderFDNEngine::setTilt(float tiltDb)
{
    tiltDbParam = tiltDb;
    tiltL.setTiltDb(tiltDb);
    tiltR.setTiltDb(tiltDb);

    if (prepared)
        updateFeedbackGains(); // damping weight depends on tiltDbParam - see updateFeedbackGains()
}

void IntruderFDNEngine::setSpacingMultiplier(float multiplier)
{
    spacingMultiplier = std::clamp(multiplier, 0.1f, 1.0f);

    // Smoothing (named "Diffusion" until 2026-08-29, Adam's later naming call - same control,
    // same tighterParamID/spacingMultiplier underneath) also scales the input diffuser's allpass
    // coefficient (density/smoothness of the early buildup) - added the same day, Adam couldn't
    // hear a difference between 0% and 100% when this control only scaled the envelope's
    // hold-time (see setSpacingMultiplier()'s declaration comment - that alone shifts the
    // hold/decay knee by ~100ms, real but narrow and easy to miss by ear). A higher allpass
    // coefficient is the standard Schroeder-diffuser way to make an early field denser/smoother
    // rather than sparse/metallic, which is also a more directly audible reading of the one thing
    // actually measured on the real hardware (Tighter -> faster echo-density buildup,
    // findings.md) than hold-time alone was.
    //
    // 0.5 at spacingMultiplier==1.0 (Smoothing 0%) matches the coefficient this plugin always
    // used before this change, so Smoothing=0% still sounds like it always has. 0.7 at
    // spacingMultiplier==0.76 (Smoothing 100%, the measured tighterHalfRiseRatio endpoint -
    // IntruderParameterMap.h's, not imported here to keep the engine free of measured-data
    // dependencies by design) is comfortably inside the range Schroeder allpass diffusers
    // typically use (roughly 0.5-0.7) before density gains start trading off against audible
    // metallic ringing - not picked to hit a specific target curve, just a reasonable, tasteful
    // top end for "more diffuse."
    constexpr float baseDiffuserCoefficient = 0.5f;
    constexpr float maxDiffuserCoefficient = 0.7f;
    constexpr float spacingMultiplierAtMaxTighten = 0.76f;
    const auto tightenAmount = std::clamp((1.0f - spacingMultiplier) / (1.0f - spacingMultiplierAtMaxTighten), 0.0f, 1.0f);
    const auto diffuserCoefficient = baseDiffuserCoefficient + tightenAmount * (maxDiffuserCoefficient - baseDiffuserCoefficient);
    for (auto& a : allpassL) a.coefficient = diffuserCoefficient;
    for (auto& a : allpassR) a.coefficient = diffuserCoefficient;
}

void IntruderFDNEngine::updateLineLengths()
{
    for (int i = 0; i < numLines; ++i)
        lineDelaySamples[(size_t) i] = std::max(1, (int) std::round(baseLineLengthsMs[(size_t) i] * 0.001 * sampleRateHz));
}

void IntruderFDNEngine::updateFeedbackGains()
{
    // The FDN's OWN natural decay is deliberately kept much slower than decaySecondsParam - the
    // envelope shaper (processStereo()) is what actually shapes the audible hold+decay contour,
    // matching the real hardware's non-exponential envelope (findings.md). If the tank decayed on
    // the same timescale as the envelope, the two would compound multiplicatively into a decay
    // roughly twice as fast as either alone. Keeping the tank's own RT60 a fixed multiple longer
    // than the Decay control means it stays a dense, sustained diffuse field for the envelope to
    // shape, rather than an independent second decay fighting it - closer to how a real gated
    // reverb's algorithm works (a sustained tank gated by a separate envelope/VCA stage).
    constexpr float tankSustainMultiplier = 1.3f;
    const auto rt60Samples = std::max(1.0f, decaySecondsParam * tankSustainMultiplier * (float) sampleRateHz);

    for (int i = 0; i < numLines; ++i)
    {
        const auto roundTrips = rt60Samples / (float) lineDelaySamples[(size_t) i];
        feedbackGain[(size_t) i] = std::pow(10.0f, -3.0f / roundTrips);
    }

    // Damping weight compounds H's tilt over the tail (see baseDampingWeight/dampingPerNegativeTiltDb
    // comments) - more negative tilt darkens further with every round trip, not just once at input.
    const auto extraDamping = std::clamp(-tiltDbParam, 0.0f, 24.0f) * dampingPerNegativeTiltDb;
    const auto dampingWeight = std::clamp(baseDampingWeight + extraDamping, 0.0f, 0.95f);
    for (auto& f : dampingFilter)
        f.setWeight(dampingWeight);
}

void IntruderFDNEngine::processStereo(float* left, float* right, int numSamples)
{
    if (!prepared)
        return;

    // Hold fraction shrinks with spacingMultiplier, matching the observed earlier knee/faster
    // diffusion buildup under Tighter (findings.md).
    const auto holdSeconds = decaySecondsParam * baseHoldFraction * spacingMultiplier;
    const auto decayTailSeconds = std::max(0.01f, decaySecondsParam - holdSeconds);
    const auto holdSamples = (double) holdSeconds * sampleRateHz;
    // -60dB over decayTailSeconds, expressed as a per-sample multiplicative rate.
    const auto decayPerSample = std::pow(10.0f, -3.0f / ((float) decayTailSeconds * (float) sampleRateHz));

    // Release ramp (a genuinely-held note ending): a fixed fraction of Decay, not the full
    // decayTailSeconds - a "note released" fade should feel prompt, not linger for the whole
    // tail length the way the natural decay-to-floor portion does.
    const auto releaseTimeSeconds = std::clamp(decaySecondsParam * 0.2f, 0.15f, 1.0f);
    const auto releasePerSample = std::pow(10.0f, -3.0f / (releaseTimeSeconds * (float) sampleRateHz));

    for (int n = 0; n < numSamples; ++n)
    {
        const auto dryL = left[n];
        const auto dryR = right[n];

        // --- Trigger detection: standard one-pole peak follower with separate attack/release
        // weights (fast rise, slow fall) tracking the DRY input.
        const auto inAbs = std::max(std::abs(dryL), std::abs(dryR));
        const auto followerWeight = (inAbs > followerState) ? followerAttackWeight : followerReleaseWeight;
        followerState += followerWeight * (inAbs - followerState);

        // Hysteresis: while already above, must fall below the LOWER release floor to count as
        // "off" - while already below, must rise above the HIGHER trigger floor to count as a new
        // hit. See triggerFloorLinear's comment for why a single shared threshold chattered.
        const auto crossThreshold = aboveTriggerFloor ? triggerReleaseFloorLinear : triggerFloorLinear;
        const auto nowAbove = followerState > crossThreshold;
        if (nowAbove && !aboveTriggerFloor)
        {
            // Rising edge: a new hit, or the input resuming after a real gap - (re)start the
            // attack/hold/decay cycle from the top - the standard gated-reverb idiom. Cancels
            // any release in progress (a new hit always wins).
            envelopePhaseSamples = 0.0;
            inRelease = false;
        }
        else if (!nowAbove && aboveTriggerFloor)
        {
            // Falling edge: the input just stopped. Only arm a release if the decay had already
            // reached the sustain floor by this point - i.e. this was a genuinely-held note, not
            // a short transient whose own input naturally drops long before its decay gets
            // anywhere near the floor (see baseHoldFraction's comment for why that distinction
            // is what earlier attempts got wrong).
            const auto samplesIntoDecayNow = envelopePhaseSamples - holdSamples;
            const auto naturalGainNow = (envelopePhaseSamples >= holdSamples)
                ? std::pow(decayPerSample, (float) samplesIntoDecayNow) : 1.0f;
            if (naturalGainNow < sustainLevelLinear)
            {
                inRelease = true;
                releaseStartGain = envelopeGain; // the actual smoothed gain right now - continuous, no jump
                releasePhaseSamples = 0.0;
            }
        }
        aboveTriggerFloor = nowAbove;

        // --- Envelope: hold at unity, then decay down to a sustain floor and hold there
        // smoothly for as long as the input stays active (no retriggering - see
        // baseHoldFraction's comment), then release to true silence once release is armed above.
        float targetGain;
        // Flat-sustaining: the decay has already reached the floor and the input is still active,
        // so the envelope isn't going anywhere on its own right now - see wobbleRateHz's comment
        // for why this specifically is what needs a quieter Wobble. False in every branch except
        // the one that can actually produce that state.
        bool flatSustaining = false;
        if (inRelease)
        {
            targetGain = releaseStartGain * std::pow(releasePerSample, (float) releasePhaseSamples);
            releasePhaseSamples += 1.0;
        }
        else if (envelopePhaseSamples < holdSamples)
        {
            targetGain = 1.0f;
        }
        else
        {
            const auto samplesIntoDecay = envelopePhaseSamples - holdSamples;
            const auto naturalGain = std::pow(decayPerSample, (float) samplesIntoDecay);
            targetGain = aboveTriggerFloor ? std::max(naturalGain, sustainLevelLinear) : naturalGain;
            flatSustaining = aboveTriggerFloor && naturalGain <= sustainLevelLinear;
        }
        envelopePhaseSamples += 1.0;
        envelopeGain = envelopeSmoother.processSample(targetGain);

        // --- Pre-delay
        preDelayBufferL.write(dryL);
        preDelayBufferR.write(dryR);
        const auto preL = preDelayBufferL.read(preDelaySamples);
        const auto preR = preDelayBufferR.read(preDelaySamples);

        // --- Tilt (H), applied immediately after pre-delay so it colors EVERYTHING downstream,
        // matching findings.md's measurement that H's effect is present from the very first
        // energy in the signal, not just the tank's tail.
        const auto tiltedL = tiltL.processSample(preL);
        const auto tiltedR = tiltR.processSample(preR);

        // --- Input diffuser (series allpass) feeding the tank
        auto diffusedL = tiltedL;
        auto diffusedR = tiltedR;
        for (auto& a : allpassL) diffusedL = a.processSample(diffusedL);
        for (auto& a : allpassR) diffusedR = a.processSample(diffusedR);

        // --- FDN tank: read each line, mix via Hadamard, apply damping, inject diffused input,
        // write back. Even lines seeded from L, odd from R (same convention as shields-reverb) -
        // one shared network is cheaper than two independent ones and gives a more correlated,
        // natural stereo image. Each line's read position wobbles slightly (see the header's "On
        // diffusion" comment) - a fractional, interpolated read instead of an exact integer one.
        const auto wobbleDepthNow = flatSustaining ? wobbleDepthSamplesF * sustainWobbleDepthFactor : wobbleDepthSamplesF;

        std::array<float, numLines> lineOut {};
        for (int i = 0; i < numLines; ++i)
        {
            const auto wobbleOffset = wobbleDepthNow * std::sin(wobblePhase[(size_t) i]);
            wobblePhase[(size_t) i] += wobblePhaseStep[(size_t) i];
            lineOut[(size_t) i] = lineBuffers[(size_t) i].readInterpolated(
                (float) (lineDelaySamples[(size_t) i] - 1) + wobbleOffset);
        }

        std::array<float, numLines> mixed {};
        constexpr float hadamardNorm = 0.25f; // 1/sqrt(16)
        for (int i = 0; i < numLines; ++i)
        {
            float sum = 0.0f;
            for (int j = 0; j < numLines; ++j)
                sum += hadamard[(size_t) i][(size_t) j] * lineOut[(size_t) j];
            mixed[(size_t) i] = sum * hadamardNorm;
        }

        for (int i = 0; i < numLines; ++i)
        {
            const auto damped = dampingFilter[(size_t) i].processSample(mixed[(size_t) i]);
            const auto fedBack = damped * feedbackGain[(size_t) i];
            const auto injected = (i % 2 == 0) ? diffusedL : diffusedR;
            lineBuffers[(size_t) i].write(fedBack + injected);
        }

        float tankL = 0.0f, tankR = 0.0f;
        for (int i = 0; i < numLines; ++i)
        {
            if (i % 2 == 0) tankL += lineOut[(size_t) i];
            else tankR += lineOut[(size_t) i];
        }
        constexpr float tankNorm = 0.353553390593f; // 1/sqrt(8) - 8 lines summed per channel
        tankL *= tankNorm;
        tankR *= tankNorm;

        const auto wetL = tankL * envelopeGain;
        const auto wetR = tankR * envelopeGain;

        left[n] = wetL;
        right[n] = wetR;
    }
}
