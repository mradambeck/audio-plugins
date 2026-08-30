#pragma once

#include <algorithm>
#include <cmath>

namespace
{
    constexpr float strikeLoopFilterTwoPi = 6.28318530717958647692f;
}

// The Loop Filter seam: "process one sample through the feedback path." Deliberately no JUCE
// include at all - see StrikeExcitation.h's header comment for why (framework-free standalone
// DSP classes, matching gradient-pitch's convention).
//
// Two concrete implementations exist, `TwoPointAverageLoopFilter` and `StrikeResonantLoopFilter`
// (see its own comment below), selected at RUNTIME via a live "Loop Filter Type" dropdown -
// StrikeStringLineChannel owns BOTH by value and branches on a plain int each sample, mirroring
// exactly how Waveshaper Type works (see StrikeWaveshaper.h's own comment for that general
// pattern and why it replaces virtual dispatch). This is a migration from this seam's original
// design (a compile-time template parameter, matching Excitation/Delay Tuning) to the runtime-
// selectable bucket Waveshaper/Ring Modulator/Feedback Topology already occupy - the user wants to
// A/B loop filter character live, the same reason every other seam made this same move.
//
// A new loop-filter variant still matches this same method set (prepare/reset/setDamping/
// processSample/getLoopGain) - adding a THIRD type means adding it as another concrete member of
// StrikeStringLineChannel and another branch, not a template-argument swap any more. A
// filter needing more internal state (e.g. a resonant filter's own delay tap) just adds more
// fixed-size members here, sized in prepare() - still real-time safe as long as nothing is sized
// in processSample(). getLoopGain() is part of the required method set alongside the other four:
// StrikeStringLineChannel's continuous (bow) excitation injection needs *some* notion of the
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

// A generic, reusable resonant LOWPASS primitive (RBJ Audio-EQ-Cookbook "LPF," direct-form-II-
// transposed) - deliberately has no notion of "Resonance," "Cutoff," or the filter envelope itself
// (those live in StrikeResonantLoopFilter below), matching how StrikeDispersionFilter itself
// has no notion of "Structure."
//
// A classic subtractive-synth cutoff+resonance lowpass, at the user's explicit request, replacing
// this file's original resonant-BANDPASS design (a fixed "Formant Frequency" peak crossfaded into
// the signal) once that shipped as an output-only tap (see StrikeResonantLoopFilter's own
// comment): a bandpass peak coloring is a genuinely different, more exotic instrument than the
// cutoff-sweep character most users expect from a synth's "filter" control, and a real lowpass is
// what an Envelope Amount control (also new - see StrikeResonantLoopFilter) actually sweeps in
// every subtractive synth this is modeled on.
//
// SAFETY is straightforward now, unlike the bandpass predecessor's careful closed-form proof: this
// filter is called ONLY from outputColor() (never recirculated into the string - see
// StrikeResonantLoopFilter's own comment), so there is no loop-gain-runaway risk to prove at all,
// only "is this filter itself stable for its own bounded input" - the RBJ lowpass biquad's poles
// stay strictly inside the unit circle for any Q>0 and any 0 < freqHz < sampleRateHz/2 (the
// standard, widely-implemented result this cookbook formula is built to guarantee), so a bounded
// input can only ever produce a bounded output through it, for any Cutoff/Resonance combination -
// verified by a dense numeric frequency sweep in the test suite, not just trusted from the
// citation, matching this project's own "measured, not just reasoned" convention.
class StrikeLowpassFilter
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
        const auto w0 = strikeLoopFilterTwoPi * freqHz / (float) sampleRateHz;
        const auto cosw0 = std::cos(w0);
        const auto alpha = std::sin(w0) / (2.0f * q);
        const auto a0 = 1.0f + alpha;
        const auto b1 = (1.0f - cosw0) / a0;
        const auto b0 = b1 * 0.5f;
        const auto b2 = b0;
        const auto a1 = (-2.0f * cosw0) / a0;
        const auto a2 = (1.0f - alpha) / a0;

        // Direct-form-II-transposed:
        //   y      = b0*x + z1
        //   z1_new = b1*x - a1*y + z2
        //   z2_new = b2*x - a2*y
        const auto y = b0 * x + z1;
        const auto z1New = b1 * x - a1 * y + z2;
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

// StrikeResonantLoopFilter: the Loop Filter seam's second concrete implementation, selected at
// RUNTIME (a "Loop Filter Type" dropdown, live-switchable at the user's explicit request, mirroring
// exactly how Waveshaper Type works - see StrikeWaveshaper.h's own comment for the general
// pattern this follows: StrikeStringLineChannel owns BOTH concrete loop filter types by value and
// branches on a plain int each sample, no virtual dispatch).
//
// REDESIGNED TWICE. First, from an in-loop crossfade (H_mix(w) = (1-r) + r*H_peak(w), written back
// into the recirculating string every pass) that was measured crushing a note's own natural decay
// severely even at small Resonance amounts - a plucked note that should ring for ~600ms (Damping=
// 0.6) measured cut to under 200ms at Resonance=5%, since (1-r) attenuates every off-peak frequency
// EVERY loop pass, and that loss compounds across the thousands of passes a note's fundamental
// makes per second. Fixed by moving to a non-recirculating OUTPUT tap - cross-checked directly
// against Mutable Instruments Rings' own string.cc (already this codebase's reference for
// Structure/Position), which does the same thing for its own "extra resonance" trick.
//
// Second, from a resonant BANDPASS (a fixed "Formant Frequency" peak blended additively into the
// output) to a traditional subtractive-synth resonant LOWPASS with Cutoff, Resonance, and an
// Envelope Amount that sweeps Cutoff over each note's own Attack+Decay - at the user's explicit
// request, once the bandpass design was already safely output-only: a synth player expects
// "Resonance" to mean a lowpass's cutoff-adjacent peak, and expects a movable cutoff with its own
// envelope, not a fixed coloring frequency. See StrikeLowpassFilter's own comment for why this
// swap needed no new safety argument (the output-only architecture already proved that once).
//
// processSample() is ALWAYS baseFilter's own (un-filtered) output, for every Resonance/Cutoff/
// Envelope setting - none of them touch the recirculating signal or the loop's own gain/decay time
// AT ALL, matching TwoPointAverageLoopFilter's decay exactly regardless of any of this class's own
// controls. The lowpass, its envelope-swept cutoff, and Resonance (the lowpass's own Q) are all
// applied only by outputColor(), a genuinely non-recirculating post-processing tap called
// separately by StrikeStringLineChannel::applyOutputEffects() and folded only into the audible
// output copy, never written back into the string - see that call site's own comment.
//
// The envelope itself is a simple two-stage Attack->Decay generator (0 by default, no sustain/
// release stage tied to note-off) - the same one-pole-recurrence-per-stage shape
// StrikeExcitation's own ADSR-ish envelope uses (see its own header comment for why a one-pole
// recurrence, not a closed-form/elapsed-sample formula, stays continuous no matter how live the
// Attack/Decay TIME controls themselves move). Triggered by reset() (called at the top of every
// noteOn() - see StrikeStringLineChannel's own "always retriggers" convention), so a fresh note
// always starts its own cutoff sweep from the beginning, exactly like the excitation's own
// envelope. envelopeAmount is BIPOLAR (-1 to 1): positive sweeps Cutoff UP then back down as the
// note begins (the classic "pluck brightness" filter-opening character), negative sweeps it DOWN
// then back up.
//
// getLoopGain() is simply baseFilter's own gain - none of this class's own controls affect it,
// since none of them affect the loop at all.
class StrikeResonantLoopFilter
{
public:
    void prepare(double sampleRate) noexcept
    {
        sampleRateHz = sampleRate;
        baseFilter.prepare(sampleRate);
        lowpass.prepare(sampleRate);
        reset();
    }

    // Also triggers the filter envelope's own Attack stage from 0 - called at the top of every
    // noteOn() (see StrikeStringLineChannel's own "always retriggers, everything reset"
    // convention), so a fresh note always starts its own cutoff sweep from the beginning. Harmless
    // to call outside a note too (e.g. the initial prepare()/idle-voice case) - the envelope just
    // sweeps once against silence and settles at 0, inaudible either way.
    void reset() noexcept
    {
        baseFilter.reset();
        lowpass.reset();
        envelopeStage = EnvelopeStage::Attack;
        envelope = 0.0f;
    }

    void setDamping(float amount01) noexcept { baseFilter.setDamping(amount01); }

    void setResonance(float amount01) noexcept { resonanceAmount = amount01; }
    void setCutoffFrequency(float hz) noexcept { cutoffHz = hz; }

    // Bipolar - see this class's own header comment. 0 (default) is a bit-exact no-op: the
    // envelope stage machine still advances every outputColor() call (cheap, matching Waveshape/
    // Structure's own "keep state current regardless of whether it's audible" convention) but
    // multiplying by amount=0 always contributes exactly 0 octaves of sweep, so Cutoff stays fixed
    // at cutoffHz throughout the note, unaffected by the (still-running) envelope.
    void setEnvelopeAmount(float bipolarAmount) noexcept { envelopeAmount = bipolarAmount; }
    void setEnvelopeAttackSeconds(float seconds) noexcept { envelopeAttackSeconds = seconds; }
    void setEnvelopeDecaySeconds(float seconds) noexcept { envelopeDecaySeconds = seconds; }

    // Always baseFilter's own output, unaffected by any of this class's own controls - see this
    // class's own header comment for why (all of Cutoff/Resonance/Envelope moved to a
    // non-recirculating output tap, outputColor() below).
    float processSample(float x) noexcept
    {
        return baseFilter.processSample(x);
    }

    // Non-recirculating: runs `loopValue` (the loop filter's own already-computed recirculating
    // value, from processSample() above) through a resonant lowpass whose cutoff is swept by this
    // note's own Attack->Decay envelope, and returns the filtered result - never written back into
    // the string, called separately by StrikeStringLineChannel for its OUTPUT-only copy.
    float outputColor(float loopValue) noexcept
    {
        // Two-stage envelope (Attack rises to 1, Decay falls back to 0, then just stays there -
        // see this class's own header comment) - the exact same one-pole-per-stage shape/threshold
        // StrikeExcitation.cpp's own Attack stage uses.
        const auto attackCoeff = 1.0f - std::exp(-1.0f / std::max(1.0f, envelopeAttackSeconds * (float) sampleRateHz));
        const auto decayCoeff = 1.0f - std::exp(-1.0f / std::max(1.0f, envelopeDecaySeconds * (float) sampleRateHz));
        switch (envelopeStage)
        {
            case EnvelopeStage::Attack:
                envelope += attackCoeff * (1.0f - envelope);
                if (envelope > 0.999f)
                    envelopeStage = EnvelopeStage::Decay;
                break;
            case EnvelopeStage::Decay:
                envelope += decayCoeff * (0.0f - envelope);
                break;
        }

        // Exponential (octave-based) sweep - the musically standard way to move a filter cutoff,
        // and what keeps a fixed envelopeAmount sound like the same PROPORTIONAL sweep regardless
        // of where Cutoff itself is set. Clamped well clear of both DC and Nyquist so the lowpass
        // biquad's own coefficients (see StrikeLowpassFilter's own comment) never see a
        // degenerate w0.
        const auto sweptCutoffHz = cutoffHz * std::pow(2.0f, envelopeAmount * maxEnvelopeOctaves * envelope);
        const auto clampedCutoffHz = std::clamp(sweptCutoffHz, minCutoffHz, (float) sampleRateHz * 0.49f);

        const auto q = minQ + resonanceAmount * (maxQ - minQ);

        // Resonant-peak loudness compensation - a real, measured bug: unlike the old bandpass
        // design this replaced (which had a proven, unconditional "gain never exceeds 1.0
        // anywhere" property - see StrikeLowpassFilter's own comment for why that guarantee no
        // longer holds for a genuine resonant LOWPASS), a resonant lowpass's peak gain near
        // Cutoff scales roughly with Q, unbounded above 1.0 as Q rises - exactly the "screaming
        // resonance" character the redesign wanted, but with no headroom compensation at all, a
        // broadband transient (the excitation's own initial pluck burst) with real energy near
        // Cutoff measured producing peaks over 11x the input's own amplitude at high Resonance -
        // worst on the lowest supported notes, whose own excitation burst is broadband relative to
        // its very low fundamental. A flat scalar (not frequency-selective, so it does trade away
        // some of the filter's own overall loudness at high Resonance, not just the excess peak -
        // an accepted, simpler tradeoff over a full peak-tracking compensator) tuned by a dense
        // measured sweep across the whole supported note range, every Cutoff/Resonance/Damping/
        // Bow/Topology combination this codebase already tests - the worst measured peak with this
        // compensation in place is ~2.1, comfortably under this project's established <=2.5 bound.
        const auto resonancePeakCompensation = 1.0f / (1.0f + resonanceAmount * resonancePeakCompensationAmount);

        return lowpass.process(loopValue, clampedCutoffHz, q) * resonancePeakCompensation;
    }

    // Simply baseFilter's own gain - none of this class's own controls affect the loop at all, see
    // this class's own header comment.
    float getLoopGain() const noexcept { return baseFilter.getLoopGain(); }

private:
    double sampleRateHz = 44100.0;
    TwoPointAverageLoopFilter baseFilter;
    StrikeLowpassFilter lowpass;
    float resonanceAmount = 0.0f;

    // 8000Hz - deliberately bright/near-open by default (not a "vowel-ish" mid frequency like the
    // old bandpass design's own formantHz default), so simply selecting Loop Filter Type=Resonant
    // doesn't unexpectedly darken a patch before the user has touched Cutoff at all - matches
    // real analog synth convention (cutoff fully open = filter reads as barely engaged). To be
    // confirmed (or retuned) by listening, same convention as every other new-feature constant in
    // this codebase.
    float cutoffHz = 8000.0f;

    // Hard floor well above DC, regardless of Cutoff/Envelope settings - avoids a degenerate w0 in
    // StrikeLowpassFilter's own coefficient formulas at the extreme low end of a downward
    // envelope sweep.
    static constexpr float minCutoffHz = 20.0f;

    // Resonance's own range: 0.7 (gentle bump) to 18.0 (screaming, near-self-oscillating) - stable
    // at any value now that this filter is output-only (no loop-recirculation-instability risk,
    // unlike the old in-loop design - see StrikeLowpassFilter's own comment), so raised well past
    // the old bandpass design's own 10.0 ceiling for a more dramatic synth-filter character.
    // Stability isn't the same thing as bounded LOUDNESS, though - see
    // resonancePeakCompensationAmount's own comment for the real, measured peak-gain issue high Q
    // still needed compensating for. Starting points pending listening, not safety-derived.
    static constexpr float minQ = 0.7f;
    static constexpr float maxQ = 18.0f;

    // See outputColor()'s own comment for the resonant-peak loudness bug this compensates for.
    // Tuned by a dense measured sweep (every supported note x Cutoff x Resonance x Damping x Bow x
    // Topology combination this codebase already tests) - 7.0 keeps the worst measured peak at
    // ~2.1, safely under this project's established <=2.5 bound.
    static constexpr float resonancePeakCompensationAmount = 7.0f;

    // Envelope Amount's own octave range at its bipolar extremes (+-1) - +-4 octaves is a
    // dramatic-but-still-musical sweep (matches typical hardware/software synth filter-envelope
    // ranges), covering Cutoff's own full displayed range from either polarity at its own default.
    // A starting point pending listening, not safety-derived (see this class's own header comment:
    // any sweep amount is safe, this only affects how far it travels).
    static constexpr float maxEnvelopeOctaves = 4.0f;

    float envelopeAmount = 0.0f;
    float envelopeAttackSeconds = 0.001f;
    float envelopeDecaySeconds = 0.2f;
    enum class EnvelopeStage { Attack, Decay };
    EnvelopeStage envelopeStage = EnvelopeStage::Attack;
    float envelope = 0.0f;
};
