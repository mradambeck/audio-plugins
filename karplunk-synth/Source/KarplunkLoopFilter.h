#pragma once

#include <cmath>

namespace
{
    constexpr float karplunkLoopFilterTwoPi = 6.28318530717958647692f;
}

// The Loop Filter seam: "process one sample through the feedback path." Deliberately no JUCE
// include at all - see KarplunkExcitation.h's header comment for why (framework-free standalone
// DSP classes, matching gradient-pitch's convention).
//
// Two concrete implementations exist, `TwoPointAverageLoopFilter` and `KarplunkResonantLoopFilter`
// (see its own comment below), selected at RUNTIME via a live "Loop Filter Type" dropdown -
// KarplunkStringLineChannel owns BOTH by value and branches on a plain int each sample, mirroring
// exactly how Waveshaper Type works (see KarplunkWaveshaper.h's own comment for that general
// pattern and why it replaces virtual dispatch). This is a migration from this seam's original
// design (a compile-time template parameter, matching Excitation/Delay Tuning) to the runtime-
// selectable bucket Waveshaper/Ring Modulator/Feedback Topology already occupy - the user wants to
// A/B loop filter character live, the same reason every other seam made this same move.
//
// A new loop-filter variant still matches this same method set (prepare/reset/setDamping/
// processSample/getLoopGain) - adding a THIRD type means adding it as another concrete member of
// KarplunkStringLineChannel and another branch, not a template-argument swap any more. A
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

// A generic, reusable resonant bandpass primitive (RBJ Audio-EQ-Cookbook "BPF, constant 0dB peak
// gain," direct-form-II-transposed) - deliberately has no notion of "Resonance" or "Formant
// Frequency" itself (those live in KarplunkResonantLoopFilter below), matching how
// KarplunkDispersionFilter itself has no notion of "Structure."
//
// Chosen (over a second internal comb/allpass creating non-harmonic peaks) specifically because
// its safety proof is a simple, closed-form, control-independent one - see KarplunkResonantLoopFilter's
// own comment for the full argument. Researched directly from Jaffe & Smith 1983 ("Extensions of
// the Karplus-Strong Plucked-String Algorithm," Computer Music Journal 7(2)) and Julius O. Smith's
// PASP treatment of the Extended KS loop filter, which states the stability requirement plainly:
// |H_d(e^jwT)| <= 1. Every canonical EKS loop filter in that literature (the pick-direction one-pole,
// the dynamic-level filter) is a damping/lowpass filter, never a resonant boost - confirming a
// resonant stage placed INSIDE the loop needs its own explicit, provable bound, not an assumption
// borrowed from the reference designs. Also cross-checked against Mutable Instruments Rings'
// string.cc (already this codebase's reference for Structure/Position): Rings' own "extra
// resonance" trick (a detuned comb readout) is applied to a non-recirculating OUTPUT tap, not
// injected as gain inside the loop - independent confirmation that resonant colouring's risk
// surface is normally kept outside (or provably bounded within) the recirculating path.
//
// SAFETY: at its own design frequency w0, |H(e^jw0)| = 1 EXACTLY, for any Q>0 and any w0 in
// (0, pi) - derived directly (not trusted from the cookbook formula alone, given how load-bearing
// this is): evaluating the transposed-direct-form-II biquad at z=e^jw0 makes numerator and
// denominator reduce to the identical complex factor 2*alpha*sin(w0)*(sin(w0)+j*cos(w0)), so their
// ratio's magnitude is exactly 1. Also: b0+b1+b2=0 (DC) and b0-b1+b2=0 (Nyquist) are exact
// identities independent of w0/Q - the standard "zero at both band edges, single resonant maximum
// of exactly 1 at w0, nowhere higher" property of this exact filter family. Poles stay inside the
// unit circle unconditionally too: a2 = (1-alpha)/(1+alpha) is in (-1,1) for ANY alpha>0, i.e. any
// Q>0 - no upper/lower bound on Q is needed for safety, only for musical taste. Verified by a dense
// numeric frequency sweep in the test suite, not just trusted from this derivation - matching this
// project's "measured, not just reasoned" convention even for an already-provable property.
class KarplunkResonantPeakFilter
{
public:
    void prepare(double sampleRate) noexcept
    {
        sampleRateHz = sampleRate;
        reset();
    }

    void reset() noexcept
    {
        z1 = 0.0f;
        z2 = 0.0f;
    }

    // Recomputes coefficients every call (live-tweakable freqHz/q, same per-sample trig-call cost
    // category as Structure's own sin/cos/atan2 calls elsewhere in this codebase - not a new
    // expense tier). Direct-form-II-transposed, 2 floats of state, no allocation ever.
    float process(float x, float freqHz, float q) noexcept
    {
        const auto w0 = karplunkLoopFilterTwoPi * freqHz / (float) sampleRateHz;
        const auto alpha = std::sin(w0) / (2.0f * q);
        const auto a0 = 1.0f + alpha;
        const auto b0 = alpha / a0;
        const auto b2 = -alpha / a0;
        const auto a1 = (-2.0f * std::cos(w0)) / a0;
        const auto a2 = (1.0f - alpha) / a0;

        // Direct-form-II-transposed, with b1=0 folded out:
        //   y      = b0*x + z1
        //   z1_new = -a1*y + z2
        //   z2_new = b2*x - a2*y
        const auto y = b0 * x + z1;
        const auto z1New = -a1 * y + z2;
        const auto z2New = b2 * x - a2 * y;
        z1 = z1New;
        z2 = z2New;
        return y;
    }

private:
    double sampleRateHz = 44100.0;
    float z1 = 0.0f;
    float z2 = 0.0f;
};

// KarplunkResonantLoopFilter: the Loop Filter seam's second concrete implementation, selected at
// RUNTIME (a "Loop Filter Type" dropdown, live-switchable at the user's explicit request, mirroring
// exactly how Waveshaper Type works - see KarplunkWaveshaper.h's own comment for the general
// pattern this follows: KarplunkStringLineChannel owns BOTH concrete loop filter types by value and
// branches on a plain int each sample, no virtual dispatch).
//
// Cascades the existing TwoPointAverageLoopFilter (keeps the physically-correct "brightness fades
// as decay progresses" character exactly as before) with KarplunkResonantPeakFilter, mixed in by
// Resonance (0 = pure two-point-average, bit-exact bypass; 1 = full resonant peak blended in) -
// Resonance also drives Q (a single, intuitive "more resonance = narrower/ringier peak" knob; an
// independent Q/bandwidth control is a natural, equally-safe future addition).
//
// SAFETY (this is the load-bearing part - see this file's own top for why a resonant stage inside
// the loop needs a real proof, not a hope): the convex mix H_mix(w) = (1-r) + r*H_peak(w) is bounded
// by the triangle inequality - |H_mix(w)| <= (1-r)*1 + r*1 = 1 for EVERY frequency, EVERY r in
// [0,1], EVERY Q, EVERY Formant Frequency (the identical algebraic pattern already used and proven
// for Cross-Couple's convex combination and Couple Delay's triangle-inequality argument in
// KarplunkVoice.h). Combined with TwoPointAverageLoopFilter's own already-proven |H(w)| <= g <=
// 0.9995 for all w, the total per-pass gain |H_total(w)| = |H_TwoPoint(w)| * |H_mix(w)| <= 0.9995
// for EVERY frequency, EVERY Damping, EVERY Resonance, EVERY Formant Frequency - no tuned ceiling
// needed on Resonance or Formant Frequency for safety, the same "already at its provably-safe
// maximum" category Cross-Couple/Couple Delay occupy. This bound is note-independent by
// construction too (Formant Frequency only ever needs 0 < freqHz < sampleRateHz/2 - it never
// appears in the bound itself), unlike Structure's dispersion bound, which had to be re-checked
// per note - so Formant Frequency is a purely musical choice (absolute Hz, not pitch-tracking), not
// a stability one.
//
// getLoopGain() is redefined as this filter's own DC gain (H_total(0)), the same interpretation
// TwoPointAverageLoopFilter's own already has (its own g literally IS its DC value): since
// H_peak(0)=0 exactly (the DC-zero identity above) and H_TwoPoint(0)=g, H_mix(0)=(1-r), giving
// getLoopGain()=g*(1-r) - confirmed to keep Bow's sqrt(1-loopGain) compensation well-behaved at
// every Resonance/Formant/Damping combination: g*(1-r) is always in [0, 0.9995), so 1-g*(1-r) is
// always in (0.0005, 1], never zero/negative, so the sqrt() never NaNs.
class KarplunkResonantLoopFilter
{
public:
    void prepare(double sampleRate) noexcept
    {
        baseFilter.prepare(sampleRate);
        peakFilter.prepare(sampleRate);
        reset();
    }

    void reset() noexcept
    {
        baseFilter.reset();
        peakFilter.reset();
    }

    void setDamping(float amount01) noexcept { baseFilter.setDamping(amount01); }

    void setResonance(float amount01) noexcept { resonanceAmount = amount01; }
    void setFormantFrequency(float hz) noexcept { formantHz = hz; }

    // Resonance=0 is a bit-exact bypass - peakFilter's own state never even advances, same
    // convention as every other "amount01=0" no-op in this codebase (Waveshape, Structure, Ring Mod).
    float processSample(float x) noexcept
    {
        const auto baseline = baseFilter.processSample(x);
        if (resonanceAmount <= 0.0f)
            return baseline;

        const auto q = minQ + resonanceAmount * (maxQ - minQ);
        const auto peaked = peakFilter.process(baseline, formantHz, q);
        return (1.0f - resonanceAmount) * baseline + resonanceAmount * peaked;
    }

    // See this class's own header comment for the derivation: this filter's DC gain is
    // baseFilter's own g scaled by (1-resonanceAmount), since the resonant peak has an exact DC
    // zero regardless of Resonance/Formant/Q.
    float getLoopGain() const noexcept { return baseFilter.getLoopGain() * (1.0f - resonanceAmount); }

private:
    TwoPointAverageLoopFilter baseFilter;
    KarplunkResonantPeakFilter peakFilter;
    float resonanceAmount = 0.0f;

    // 1000Hz - a reasoned, vowel-ish starting default, not measured against Karplunk's own loop -
    // to be confirmed (or retuned) by listening, same convention as every other new-feature
    // constant in this codebase.
    float formantHz = 1000.0f;

    // Resonance's own range: 0.7 (gentle bump) to 10.0 (sharp, bell-like ring) - starting points
    // pending listening, not safety-derived (see this class's own comment: no Q value is unsafe).
    static constexpr float minQ = 0.7f;
    static constexpr float maxQ = 10.0f;
};
