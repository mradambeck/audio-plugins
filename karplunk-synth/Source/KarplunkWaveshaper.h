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
// KarplunkFuzz (asymmetric tanh clipping) and KarplunkSaturator (symmetric tanh soft saturation)
// used to live here too, as the other two options from a three-way soft-saturation/hard-clipping/
// folding discussion - removed at the user's request (Fold and BitCrush covered the musically
// useful range on their own); see git history if either needs to come back.
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
// `updateFilter()` must be called exactly once per sample tick - sample-and-hold has real
// per-sample state (`holdCounter`) that would misbehave if advanced more than once per tick.
// Bit depth and hold length are bundled into the
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
