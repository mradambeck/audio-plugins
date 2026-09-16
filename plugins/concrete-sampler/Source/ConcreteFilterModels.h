#pragma once

#include <juce_core/juce_core.h>

#include <algorithm>
#include <array>
#include <cmath>

// Phase 5: per-voice playback-side filter models (see concrete-sampler-plugin-plan.md's Phase 5).
// Filters are per-voice, never on the master bus - these machines had a physical filter chip per
// voice card, and ConcreteVoice owns one instance PER CHANNEL (see that class) so a stereo zone's
// L/R content doesn't share filter state.
//
// Four genuinely distinct machine behaviors share a common 4-pole transistor-ladder core - the
// SSM2044/CEM3320 family are architecturally similar OTA ladder designs (see the plan's own "model
// one topology with per-preset trim" guidance for the SSM family, extended here to the CEM
// variants since they're the same lineage of part):
//   - ssm: soft internal saturation (an OTA ladder's own driven character), no resonance
//     compensation, can self-oscillate at high resonance.
//   - cemResonanceLoss: cleaner (less saturating) internal stages, no resonance compensation - the
//     Fairlight/Linn 9000 "thinning out" as resonance rises.
//   - cemResonanceCompensated: cleaner internal stages, WITH a resonance-proportional makeup gain -
//     the Mirage's CEM3328 holding passband level roughly steady as resonance rises.
// digitalVca (Kurzweil K250) is a deliberately SEPARATE, non-ladder code path - a clean digital
// state-variable lowpass (its own resonance behavior, no saturation, no self-oscillation) followed
// by a fixed VCA-character saturation stage: "do not give the K250 an analog filter model," per
// the plan. onePole (Casio SK-1) is a literal single real pole - resonance has no effect on it,
// mathematically, since one pole alone cannot peak/self-oscillate. bypass (the SP-1200's actual
// path, which deliberately omitted a reconstruction filter) ignores every filter parameter.
class ConcreteFilterModel
{
public:
    enum class Mode
    {
        bypass,
        ssm,
        cemResonanceLoss,
        cemResonanceCompensated,
        digitalVca,
        onePole,
    };

    void prepare(double sampleRateIn) noexcept
    {
        sampleRate = sampleRateIn;
        reset();
    }

    void reset() noexcept
    {
        ladderStage.fill(0.0f);
        svfIc1eq = 0.0f;
        svfIc2eq = 0.0f;
        onePoleState = 0.0f;
    }

    // cutoffHz: the instantaneous cutoff (after envelope/key-tracking modulation has already been
    // applied by the caller - see ConcreteVoice). resonance01: 0-1; for the ladder modes, 1 is at
    // (or just past) self-oscillation. Has no audible effect for Mode::bypass, and no effect from
    // resonance01 specifically for Mode::onePole.
    float processSample(Mode mode, float input, float cutoffHz, float resonance01) noexcept
    {
        switch (mode)
        {
            case Mode::bypass:                    return input;
            case Mode::ssm:                        return processLadder(input, cutoffHz, resonance01, true, false);
            case Mode::cemResonanceLoss:           return processLadder(input, cutoffHz, resonance01, false, false);
            case Mode::cemResonanceCompensated:    return processLadder(input, cutoffHz, resonance01, false, true);
            case Mode::digitalVca:                 return processDigitalVca(input, cutoffHz, resonance01);
            case Mode::onePole:                    return processOnePole(input, cutoffHz);
        }
        return input;
    }

private:
    // Stilson/Smith-style simplified digital Moog ladder (4 cascaded one-pole stages plus a
    // feedback path from the last stage back to the first, which is what gives a ladder its
    // resonance and, past resonance01~=1, self-oscillation). saturateInternally adds the SSM's
    // OWN extra "soft asymmetric saturation when driven" on top of the shared safety clip below;
    // compensateResonanceLoss adds a resonance-proportional makeup gain (the CEM3328's defining
    // difference from the plain CEM3320).
    float processLadder(float input, float cutoffHz, float resonance01, bool saturateInternally,
                         bool compensateResonanceLoss) noexcept
    {
        const auto nyquist = (float) (sampleRate * 0.5);
        // cutoffHz is pre-scaled by an empirically-tuned ~2.3x before entering this simplified
        // recursion's own frequency mapping - without it, the resonant peak/self-oscillation
        // frequency this filter actually produces sits at roughly 0.4x the nominal cutoffHz over
        // most of the usable range (confirmed empirically: an impulse into this filter at
        // resonance=1.0 rang on at ~470Hz for a nominal 1200Hz cutoff before this correction),
        // which would make the Filter Cutoff knob badly mistrack what's actually heard. This
        // correction is itself only accurate over roughly the low-to-mid cutoff range (below
        // ~2kHz) - it under-corrects at very low cutoffs and over-corrects approaching Nyquist,
        // the same kind of range-dependent imprecision the "1.16"/"4.0"/"0.15" constants below
        // already carry from the reference algorithm; tune further by ear if a specific machine
        // preset (Phase 7) needs tighter tracking at its own particular range.
        // Clamped so f = fc*1.16 never reaches 1.0, not so fc itself stays under some round number -
        // a real, previously-present stability bug: at cutoffHz approaching Nyquist (fc up to 0.99),
        // f reached up to 1.148, past this recursion's stability boundary.
        const auto fc = std::clamp((cutoffHz * 2.3f) / nyquist, 0.0f, 0.98f / 1.16f);
        const auto f = fc * 1.16f;
        const auto fSquared = f * f;
        // Empirical resonance-vs-cutoff compensation (from the classic reference implementation)
        // so the self-oscillation threshold stays near resonance01=1 across the cutoff range
        // rather than drifting with fc. 4.0 is the ladder's own well-known self-oscillation
        // feedback gain.
        const auto feedbackGain = resonance01 * 4.0f * (1.0f - 0.15f * fSquared);

        // The feedback TAP is ALWAYS soft-clipped, unconditionally - this is what actually keeps
        // the recursion bounded at high feedbackGain (the classic reference algorithm has an
        // equivalent stabilizing nonlinearity in the same place; a real, previously-present bug
        // here dropped it for the "clean" variants, which then had NOTHING bounding their feedback
        // loop and genuinely diverged to NaN/inf at high cutoff+resonance - confirmed empirically
        // via ConcreteFilterModelsTests.cpp's extreme-settings sweep). This is a stability
        // requirement, not a tone choice, so it applies regardless of saturateInternally.
        const auto feedbackSample = std::tanh(ladderStage[3]);
        auto fed = input - feedbackGain * feedbackSample;
        if (saturateInternally)
        {
            // SSM's OWN additional driven character, on top of the safety clip above - a second,
            // stronger saturation stage the "clean" CEM variants don't get.
            constexpr auto driveAmount = 1.5f;
            fed = std::tanh(fed * driveAmount) / std::tanh(driveAmount);
        }

        ladderStage[0] += f * (fed - ladderStage[0]);
        ladderStage[1] += f * (ladderStage[0] - ladderStage[1]);
        ladderStage[2] += f * (ladderStage[1] - ladderStage[2]);
        ladderStage[3] += f * (ladderStage[2] - ladderStage[3]);

        auto output = ladderStage[3];
        if (compensateResonanceLoss)
        {
            // Tuned so passband level (well below cutoff) stays close to constant as resonance
            // rises from 0 to 1 - see analysis/verify_phase5.py's resonance-sweep check for the
            // measured result this was tuned against.
            output *= 1.0f + resonance01 * 3.6f;
        }
        return output;
    }

    // Vadim Zavalishin's topology-preserving-transform (TPT) state-variable lowpass - a standard,
    // unconditionally-stable digital filter with its own clean resonance/Q behavior, no saturation
    // or self-oscillation. Deliberately a different code path from the ladder models above (see
    // the class comment on why the K250 doesn't get an analog filter model), followed by a fixed,
    // always-on soft-saturation stage standing in for the K250's own CEM3335 VCA character - not a
    // user-adjustable amount, since the plan describes it as an intrinsic property of this model,
    // not a separate control.
    float processDigitalVca(float input, float cutoffHz, float resonance01) noexcept
    {
        const auto g = std::tan(juce::MathConstants<float>::pi * cutoffHz / (float) sampleRate);
        // resonance01=0 -> k=2 (Q=0.5, no peaking); resonance01=1 -> k=0.1 (high Q, close to but
        // clamped short of instability) - clean digital resonance, no ladder-style self-oscillation.
        const auto k = juce::jmax(0.1f, 2.0f - 1.9f * resonance01);
        const auto a1 = 1.0f / (1.0f + g * (g + k));
        const auto a2 = g * a1;
        const auto a3 = g * a2;

        const auto v3 = input - svfIc2eq;
        const auto v1 = a1 * svfIc1eq + a2 * v3;
        const auto v2 = svfIc2eq + a2 * svfIc1eq + a3 * v3;
        svfIc1eq = 2.0f * v1 - svfIc1eq;
        svfIc2eq = 2.0f * v2 - svfIc2eq;

        constexpr auto vcaDrive = 1.6f; // fixed, modest - "VCA character," not a distortion stage
        return std::tanh(v2 * vcaDrive) / std::tanh(vcaDrive);
    }

    // A literal single real pole - the SK-1's "simple one-pole lowpass." resonance01 is accepted
    // (matching the common processSample() signature) but has no effect: a single pole
    // mathematically cannot peak or self-oscillate.
    float processOnePole(float input, float cutoffHz) noexcept
    {
        const auto alpha = (float) (1.0 - std::exp(-2.0 * juce::MathConstants<double>::pi * cutoffHz / sampleRate));
        onePoleState += alpha * (input - onePoleState);
        return onePoleState;
    }

    double sampleRate = 44100.0;

    std::array<float, 4> ladderStage {};
    float svfIc1eq = 0.0f;
    float svfIc2eq = 0.0f;
    float onePoleState = 0.0f;
};
