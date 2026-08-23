#pragma once

// The Loop Filter seam: "process one sample through the feedback path." Deliberately no JUCE
// include at all - see KarplunkExcitation.h's header comment for why (framework-free standalone
// DSP classes, matching gradient-pitch's convention).
//
// To add a new loop-filter variant (one-pole lowpass, comb, resonant, asymmetric, etc.), write a
// new class matching this same method set (prepare/reset/setDamping/processSample/getLoopGain)
// and swap the template argument in KarplunkVoice.h's KarplunkStringLineChannel instantiation -
// nothing in KarplunkExcitation.h, KarplunkStringLine.h, or KarplunkVoice.h needs to change. A
// filter needing more internal state (e.g. a resonant filter's own delay tap) just adds more
// fixed-size members here, sized in prepare() - still real-time safe as long as nothing is sized
// in processSample(). getLoopGain() is part of the required method set alongside the other four:
// KarplunkStringLineChannel's continuous (bow) excitation injection needs *some* notion of the
// filter's own DC/loop gain to compensate injected loudness across the Decay range - see that
// class's renderChannelSample() for why.
class TwoPointAverageLoopFilter
{
public:
    void prepare(double sampleRate) noexcept;
    void reset() noexcept;

    // 0 = minimum loop gain (fastest decay), 1 = maximum loop gain (longest sustain). See the
    // .cpp for why decay time is inherently pitch-dependent even at a fixed damping value here -
    // that's real Karplus-Strong physics, not a bug, and decoupling it is a named future
    // loop-filter swap-in (Jaffe/Smith's stretched allpass), not something to fix in this class.
    void setDamping(float amount01) noexcept;

    // The classic Karplus-Strong loop filter: y[n] = g * 0.5f * (x[n] + x[n-1]). A one-zero
    // lowpass whose gain rolls off towards Nyquist, which is what gives higher harmonics a
    // shorter decay than the fundamental - the plucked-string "brightness fades first" character.
    float processSample(float x) noexcept;

    // The current loop gain g (in [minLoopGain, maxLoopGain]) - see the header comment above for
    // why this is part of this seam's required method set.
    float getLoopGain() const noexcept { return loopGain; }

private:
    float prevInput = 0.0f;
    float loopGain = 0.995f;

    static constexpr float minLoopGain = 0.90f;
    static constexpr float maxLoopGain = 0.9995f;
};
