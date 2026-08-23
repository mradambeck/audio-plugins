#pragma once

#include <algorithm>
#include <cmath>

namespace
{
    constexpr float karplunkWaveshaperPi = 3.14159265358979323846f;
}

// The Waveshaper seam: "nonlinearly reshape one sample of the loop's own recirculating signal
// before it's written back." This is the "nonlinear waveshaping in the loop" area from
// README.md's swap-in table - deliberately its own seam, not folded into the Loop Filter, since a
// waveshaper's contract is genuinely different (stateless per-sample amount, not a setter-then-
// process split like setDamping()/processSample() - see below) and future variants may need their
// own internal state (e.g. a DC blocker for a different asymmetric curve) that has nothing to do
// with decay/brightness shaping.
//
// Unlike Excitation/Loop Filter/Delay Tuning (each a compile-time KarplunkStringLineChannel template
// parameter - pick one implementation, rebuild to change it), this seam is a RUNTIME choice: the
// user asked for a dropdown to swap character live, not a rebuild. KarplunkStringLineChannel owns
// BOTH concrete classes below by value and branches on a `waveshaperType` index each sample - no
// virtual dispatch or std::function, matching this project's zero-polymorphism convention, just an
// explicit selection between a small, fixed set of concrete types (see its own comment for the
// exact branch). Each class shares the same `process(x, amount01, driveCompensation)` contract so
// PluginProcessor/KarplunkVoice.h's call sites don't need to know which one is active.
//
// KarplunkWaveFolder: a classic triangle/sine wavefolder (the "West Coast synthesis" style
// - Buchla-style folding, not a clipper/saturator). Past a threshold, the signal reflects back on
// itself rather than compressing/flattening - this generates dense, often inharmonic-sounding
// overtones rather than the simpler harmonic series a soft clipper adds, which is what makes
// folding the more dramatic/exploratory choice of the three discussed (soft saturation, hard
// asymmetric clipping, folding) for pushing Karplunk past a physically-plausible plucked string.
//
// Implemented via `threshold * (2/pi) * asin(sin(driven * pi / (2*threshold)))` - a closed-form
// triangle wave of the (driven, pre-scaled) input, not an iterative "reflect until in range" loop.
// This matters for two reasons: it's real-time-safe by construction (fixed-cost trig calls, no
// data-dependent iteration count for pathologically large inputs), and the output is
// *unconditionally bounded* to [-threshold, threshold] for ANY input magnitude (asin/sin are both
// bounded functions) - the loop's own recirculating energy literally cannot blow up through this
// stage, regardless of how hard it's driven or how it interacts with loop gain. For small
// `driven` values (asin(sin(t)) ≈ t there), this is also very close to a transparent, linear pass-
// through - so as a note's own decay quiets it below the fold threshold, this stage naturally
// reverts to near-identity rather than gating/holding the signal up artificially.
//
// NOTE: an envelope-following drive normalization (tracking the note's own recent level and
// boosting quiet moments back up toward the fold threshold, so the fold stays engaged throughout
// a note's decay, not just its loud initial transient) was built and measured working correctly
// here, then explicitly reverted at the user's request - they found the plain, fixed-drive
// character (fold strongest at the loud initial hit, cleaner as the note settles) more musically
// usable than the "folds throughout the whole decay" character envelope-following produced. Not
// a bug - a deliberate creative call. If revisited, see git history for the working
// implementation (KarplunkWaveFolder::updateEnvelope(), called once per sample from
// KarplunkStringLineChannel::renderChannelSample()) rather than rebuilding it from scratch.
class KarplunkWaveFolder
{
public:
    void prepare(double) noexcept {}
    void reset() noexcept {}

    // amount01 scales DRIVE (pre-fold gain), not the fold threshold itself - a fixed
    // threshold (matching the +-1 signal convention used throughout this codebase) means
    // amount01 purely controls how many times a given signal reflects/folds, not where folding
    // starts. minDrive/maxDrive are a starting point chosen for a clearly audible, dense fold at
    // 100% without being absurd - to be confirmed (or retuned) by actually rendering and
    // listening, per this project's established practice, not treated as final on the numbers
    // alone.
    //
    // `driveCompensation` (0 = none, 1 = full) exists because "safe for the recirculating signal"
    // and "sounds right on the output" turned out to be genuinely different requirements, not the
    // same tuning problem twice - discovered only by measuring both together, not reasoned out in
    // advance:
    //   - driveCompensation=1 divides the folded result back down by the FULL drive. This is the
    //     safety-critical case: for any signal quiet enough to stay under the fold point even
    //     after being scaled by drive, asin(sin(t)) ~ t (small-angle), so the whole expression
    //     reduces to just `driven = x * drive` - an undisguised gain boost for anything that never
    //     actually folds. Sitting inside the feedback loop (this is the value written back via
    //     stringLine.write() - see KarplunkVoice.h), that extra gain compounds every pass -
    //     measured making the sustained loop ~4x louder at Waveshape=100% than at 0% before this
    //     existed. Dividing by the full drive restores near-unity gain for quiet/never-folded
    //     content (driven/drive = x) while genuinely loud/folding content - already bounded to
    //     +-threshold regardless of drive - ends up proportionally quieter the harder it's driven.
    //   - driveCompensation<1 divides by less, so genuinely-folded content stays louder/denser -
    //     correct for a signal that's only ever used for OUTPUT (never fed back), where there's no
    //     loop-gain-runaway risk to guard against, only "does this sound right," and full
    //     compensation at high maxDrive was measured crushing the audible effect almost to
    //     silence for anything that wasn't already loud. Regardless of driveCompensation, output
    //     is still unconditionally bounded to +-threshold (folded's own bound never changes) -
    //     this parameter only trades off how much of a genuinely-folding signal's loudness is
    //     given back, never safety.
    float process(float x, float amount01, float driveCompensation = 1.0f) const noexcept
    {
        const auto drive = minDrive + amount01 * (maxDrive - minDrive);
        const auto driven = x * drive;
        const auto folded = threshold * (2.0f / karplunkWaveshaperPi)
                           * std::asin(std::sin(driven * karplunkWaveshaperPi / (2.0f * threshold)));
        const auto divisor = 1.0f + driveCompensation * (drive - 1.0f);
        return folded / divisor;
    }

private:
    static constexpr float threshold = 1.0f;
    static constexpr float minDrive = 1.0f;
    static constexpr float maxDrive = 32.0f;
};

// KarplunkFuzz: heavy, asymmetric tanh clipping - a classic transistor-fuzz-pedal character
// (Fuzz Face/Big Muff family), the "hard/asymmetric clipping" option from the same three-way
// discussion that led to KarplunkWaveFolder (soft saturation, hard/asymmetric clipping, folding).
// Distinct in KIND from folding, not just degree: a clipper's output magnitude is monotonically
// non-decreasing then flat as input grows (it compresses/flattens toward a ceiling), where a
// folder's reflects back down past its own threshold - clipping stays harmonically related to a
// square/saw wave (dense odd harmonics, plus even harmonics from the asymmetry below), which
// reads as "fuzzy/buzzy/gated" rather than folding's more inharmonic, metallic character.
//
// tanh (not a literal hard clamp) for the same reason KarplunkWaveFolder uses a smooth closed-form
// curve rather than an iterative one: it's real-time-safe by construction and *unconditionally
// bounded* to +-1 for any input (tanh saturates smoothly, never exceeding its asymptote) - the
// loop's own recirculating energy cannot blow up through this stage regardless of drive, the same
// safety property KarplunkWaveFolder relies on. The asymmetry (a different effective drive for the
// negative half-cycle) is what actually gives this its "fuzz" character rather than a symmetric,
// comparatively polite tanh saturation - real transistor fuzz circuits clip asymmetrically because
// a single-ended transistor stage doesn't treat both signal polarities identically, and that
// asymmetry is exactly what introduces even harmonics on top of tanh's own odd-harmonic-rich
// saturation.
//
// Shares KarplunkWaveFolder's `driveCompensation` split (see its own comment for the full
// reasoning - discovered there first, applied here from the start rather than re-discovering the
// same bug): full compensation for what's written back into the loop (safety-critical, prevents a
// hidden gain boost on quiet/never-clipping content from compounding every pass), little to none
// for what's actually heard (never fed back, so no loop-gain risk, only "does it sound right").
//
// Post-clip lowpass, added after the user reported the raw clip as "very hissy" - heavy clipping
// on a noise-EXCITED string (Karplunk's excitation is filtered white noise, not a pure tone)
// generates dense harmonic/intermodulation energy all the way up toward Nyquist, with nothing in
// a plain tanh clip to tame it - real fuzz pedals almost universally follow their clip stage with
// a tone-shaping capacitor for exactly this reason, so this isn't a workaround, it's the missing
// other half of a standard fuzz circuit. A SINGLE one-pole stage was tried first and measured
// (independently, via a raw-WAV DFT read back in Python, not just reasoning about the coefficient)
// to still leave ~51% of the settled tail's spectral energy above 6kHz - a one-pole's -6dB/octave
// rolloff is too gentle against how much energy a hard clip dumps near Nyquist. Fixed by cascading
// TWO identical one-pole stages (-12dB/octave) at a lower cutoff - a real, measured fix, not a
// guessed number, since the single-stage attempt looked reasonable on paper (a "6kHz lowpass"
// sounds like it should obviously cut hiss) but measurably wasn't doing much.
//
// Filters the SHAPED (post-clip, pre-divide) value, not the raw input - dividing by a scalar
// afterward doesn't change frequency content, so the lowpass only needs to run ONCE per sample
// regardless of how many differently-scaled consumers (recirculating vs. output) read the result
// afterward, which is why updateFilter() is a separate call from process() - matching the same
// "shared per-sample state, called once, read via a separate scaled accessor" shape
// KarplunkWaveFolder's own (since-reverted) envelope-following used, for the same reason:
// process() is invoked twice per sample tick.
class KarplunkFuzz
{
public:
    void prepare(double sampleRate) noexcept
    {
        lowpassCoeff = 1.0f - std::exp(-2.0f * karplunkWaveshaperPi * cutoffHz / (float) sampleRate);
        reset();
    }

    void reset() noexcept
    {
        filterState1 = 0.0f;
        filterState2 = 0.0f;
    }

    // Call exactly once per sample tick, before any process() calls for that tick - see class
    // comment for why the filter state is shared rather than recomputed per call.
    void updateFilter(float x, float amount01) noexcept
    {
        const auto drive = minDrive + amount01 * (maxDrive - minDrive);
        const auto driven = x * drive;
        const auto shaped = driven >= 0.0f ? std::tanh(driven) : std::tanh(driven * negativeAsymmetry);
        filterState1 += lowpassCoeff * (shaped - filterState1);
        filterState2 += lowpassCoeff * (filterState1 - filterState2); // 2nd cascaded stage: -12dB/octave
    }

    float process(float amount01, float driveCompensation = 1.0f) const noexcept
    {
        const auto drive = minDrive + amount01 * (maxDrive - minDrive);
        const auto divisor = 1.0f + driveCompensation * (drive - 1.0f);
        return filterState2 / divisor;
    }

private:
    static constexpr float minDrive = 1.0f;
    static constexpr float maxDrive = 50.0f;
    static constexpr float negativeAsymmetry = 0.6f;

    // A single one-pole stage at 6kHz was measured leaving ~51% of spectral energy above 6kHz
    // (see class comment) - this cascaded/lower-cutoff combination was measured leaving
    // substantially less (see git history/PR discussion for the exact re-measured number).
    // Doesn't choke normal-register fundamentals (Karplunk's highest supported note, C8, is
    // ~4186Hz) but is deliberately more aggressive than the first attempt - to be confirmed (or
    // retuned) by actually listening, per this project's established practice.
    static constexpr float cutoffHz = 3000.0f;

    float lowpassCoeff = 0.0f;
    float filterState1 = 0.0f;
    float filterState2 = 0.0f;
};

// KarplunkSaturator: gentle, SYMMETRIC tanh soft saturation - the third and last option from the
// original three-way discussion that led to KarplunkWaveFolder and KarplunkFuzz (soft saturation,
// hard/asymmetric clipping, folding). Unlike KarplunkFuzz, both half-cycles get the identical
// curve (a real odd function, same as KarplunkWaveFolder) - no even harmonics, just tanh's own
// smooth rounding of a signal's peaks. `maxDrive` is deliberately much lower than KarplunkFuzz's
// 50 - the point of this class is a mild, "warm/rounded" character, not a heavy clip; pushing this
// curve as hard as Fuzz would just make it a slower, symmetric version of the same effect rather
// than a genuinely different, milder option on the dropdown. A starting value, to be confirmed (or
// retuned) by actually rendering and listening, matching every other waveshaper constant in this
// file.
//
// Shares the same real-time-safety property (unconditionally bounded to +-1) and the same
// `driveCompensation` split (see KarplunkWaveFolder's own comment for the full reasoning) as both
// siblings.
//
// Post-clip lowpass, added proactively (before any user listened to it) once measurement showed
// it was actually needed, not assumed just because Fuzz needed one: a first render at full drive
// (Bow engaged, the same condition that first surfaced Fuzz's hiss) measured only 29% of the
// UNSHAPED baseline's own settled-tail energy sitting above 6kHz (Bow's own continuous noise
// injection already has real high-frequency content, lowpass or not) - but Saturate=100% pushed
// that to 72%, a real, substantial, measured increase from the saturation stage itself, not just
// inherent to Bow. So despite being a much gentler curve than Fuzz's heavy asymmetric clip, this
// needed the identical fix: cascading two one-pole stages (-12dB/octave, see updateFilter()) at
// the same 3kHz cutoff Fuzz uses - re-measured afterward (see git history/PR discussion for the
// exact number) rather than assumed to transfer directly just because the mechanism looked the
// same. Shares KarplunkFuzz's exact "updateFilter() once per sample, read via process()" shape and
// the same reasoning for it (dividing by a scalar afterward doesn't change frequency content, so
// the filter only needs to run once regardless of how many differently-scaled consumers read the
// result afterward).
class KarplunkSaturator
{
public:
    void prepare(double sampleRate) noexcept
    {
        lowpassCoeff = 1.0f - std::exp(-2.0f * karplunkWaveshaperPi * cutoffHz / (float) sampleRate);
        reset();
    }

    void reset() noexcept
    {
        filterState1 = 0.0f;
        filterState2 = 0.0f;
    }

    // Call exactly once per sample tick, before any process() calls for that tick.
    void updateFilter(float x, float amount01) noexcept
    {
        const auto drive = minDrive + amount01 * (maxDrive - minDrive);
        const auto shaped = std::tanh(x * drive);
        filterState1 += lowpassCoeff * (shaped - filterState1);
        filterState2 += lowpassCoeff * (filterState1 - filterState2); // 2nd cascaded stage: -12dB/octave
    }

    float process(float amount01, float driveCompensation = 1.0f) const noexcept
    {
        const auto drive = minDrive + amount01 * (maxDrive - minDrive);
        const auto divisor = 1.0f + driveCompensation * (drive - 1.0f);
        return filterState2 / divisor;
    }

private:
    static constexpr float minDrive = 1.0f;
    static constexpr float maxDrive = 10.0f;
    static constexpr float cutoffHz = 3000.0f;

    float lowpassCoeff = 0.0f;
    float filterState1 = 0.0f;
    float filterState2 = 0.0f;
};

// KarplunkBitCrush: sample-and-hold rate reduction + bit-depth quantization, combined under one
// `amount01` (the classic "lo-fi" pair, usually offered as a single "Crush" knob) - not one of the
// original three waveshaping-curve options, but a genuinely different KIND of degradation
// (quantization/decimation, not a continuous nonlinear curve) that fits the same runtime-selectable
// seam (see this file's own top comment and `KarplunkVoice.h`'s branch).
//
// Real-time-safety note, different from every other class in this file: quantization/sample-hold
// cannot itself amplify a signal the way a drive-scaled curve can (there's no loop-gain-runaway
// risk to guard against with a `driveCompensation` split - see KarplunkWaveFolder's own comment for
// why that split exists for the other three), so this class's `process()` takes no parameters at
// all rather than accepting an unused one just to look uniform. A held/quantized value is bounded
// to the input's own magnitude by construction, EXCEPT quantization itself can round an
// already-large input further from zero by up to half a step - explicitly clamped to +-1 in
// updateFilter() to preserve the same unconditional-boundedness guarantee every other waveshaper in
// this file provides, since this is the one place in this class an unbounded input could still
// produce an unbounded (if rarely, in practice) output.
//
// `updateFilter()` must be called exactly once per sample tick (same contract as KarplunkFuzz/
// KarplunkSaturator) - sample-and-hold has real per-sample state (`holdCounter`) that would
// misbehave if advanced more than once per tick. Bit depth and hold length are bundled into the
// same `amount01` rather than split into two separate knobs, since Karplunk currently only exposes
// one shared Waveshape amount per type - `minBits`/`maxHoldSamples` are starting points, not
// finalized, pending listening (same convention as every other constant in this file).
class KarplunkBitCrush
{
public:
    void prepare(double) noexcept { reset(); }
    void reset() noexcept
    {
        heldValue = 0.0f;
        holdCounter = 0;
    }

    // Call exactly once per sample tick, before any process() calls for that tick.
    void updateFilter(float x, float amount01) noexcept
    {
        if (holdCounter <= 0)
        {
            const auto bits = maxBits - amount01 * (maxBits - minBits);
            const auto levels = std::exp2(bits);
            const auto step = 2.0f / levels; // quantization step across the [-1, 1] signal range
            heldValue = std::clamp(std::round(x / step) * step, -1.0f, 1.0f);
            holdCounter = 1 + (int) (amount01 * (float) (maxHoldSamples - 1));
        }

        --holdCounter;
    }

    float process() const noexcept { return heldValue; }

private:
    static constexpr float minBits = 2.0f;   // most crushed: coarse, stair-stepped quantization
    static constexpr float maxBits = 16.0f;  // least crushed: near-transparent quantization
    static constexpr int maxHoldSamples = 40; // most crushed: ~1.1kHz effective rate at 44.1kHz

    float heldValue = 0.0f;
    int holdCounter = 0;
};
