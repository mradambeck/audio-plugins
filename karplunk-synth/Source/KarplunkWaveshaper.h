#pragma once

#include <cmath>

namespace
{
    constexpr float karplunkWaveshaperPi = 3.14159265358979323846f;
}

// The Waveshaper seam: "nonlinearly reshape one sample of the loop's own recirculating signal
// before it's written back." This is the "nonlinear waveshaping in the loop" area from
// README.md's swap-in table - deliberately its own seam (a 4th SingleLineKarplunkVoice template
// parameter), not folded into the Loop Filter, since a waveshaper's contract is genuinely
// different (stateless per-sample amount, not a setter-then-process split like
// setDamping()/processSample() - see below) and future variants (tanh saturation, asymmetric
// clipping) may need their own internal state (e.g. a DC blocker for asymmetric curves) that has
// nothing to do with decay/brightness shaping.
//
// To add a new waveshaper variant, write a new class matching this same method set
// (prepare/reset/process(x, amount01)) and swap the template argument in KarplunkVoice.h's
// SingleLineKarplunkVoice instantiation - nothing in KarplunkVoice.h itself needs to change
// (mirrors how KarplunkLoopFilter.h/KarplunkExcitation.h document their own swap points).
// process() takes `amount01` directly each call (matching KarplunkDispersionFilter's
// process(x, gain) shape, not a separate setAmount() setter) since Structure's live,
// every-sample-varying control is the closer precedent here than the Loop Filter's own
// set-once-per-block Damping.
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
// SingleLineKarplunkVoice::renderNextSample()) rather than rebuilding it from scratch.
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
