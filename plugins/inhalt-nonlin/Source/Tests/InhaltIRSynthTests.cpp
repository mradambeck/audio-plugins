#include "../InhaltIRSynth.h"

#include <juce_core/juce_core.h>

#include <cmath>
#include <numeric>

namespace
{
    bool hasNaNOrInf(const std::vector<float>& v)
    {
        for (auto x : v)
            if (! std::isfinite(x))
                return true;
        return false;
    }

    double correlation(const std::vector<float>& a, const std::vector<float>& b)
    {
        const auto n = std::min(a.size(), b.size());
        double meanA = 0.0, meanB = 0.0;
        for (size_t i = 0; i < n; ++i) { meanA += a[i]; meanB += b[i]; }
        meanA /= (double) n; meanB /= (double) n;

        double num = 0.0, denomA = 0.0, denomB = 0.0;
        for (size_t i = 0; i < n; ++i)
        {
            const auto da = a[i] - meanA, db = b[i] - meanB;
            num += da * db;
            denomA += da * da;
            denomB += db * db;
        }
        const auto denom = std::sqrt(denomA * denomB);
        return denom > 0.0 ? num / denom : 0.0;
    }
}

class InhaltIRSynthTests : public juce::UnitTest
{
public:
    InhaltIRSynthTests() : juce::UnitTest("InhaltIRSynth", "Inhalt") {}

    void runTest() override
    {
        constexpr double sampleRate = 44100.0;

        beginTest("A typical render is finite everywhere and produces audible energy");
        {
            inhalt::InhaltIRSynth::Params params;
            params.feedbackGain = 0.8f;
            params.dampingWeight = 0.4f;
            params.kneeTimeMs = 150.0f;
            params.fallRateLowDbPerSec = params.fallRateMidDbPerSec = params.fallRateHighDbPerSec = -250.0f;

            std::vector<float> left, right;
            inhalt::InhaltIRSynth::render(params, sampleRate, (int) (0.5 * sampleRate), left, right);

            expect(! hasNaNOrInf(left), "left channel must be finite everywhere");
            expect(! hasNaNOrInf(right), "right channel must be finite everywhere");

            const auto peakL = *std::max_element(left.begin(), left.end(),
                [](float a, float b) { return std::abs(a) < std::abs(b); });
            expect(std::abs(peakL) > 1.0e-4f, "should produce audible energy");
        }

        beginTest("Left and right channels are genuinely different (delay-set copy-paste guard)");
        {
            inhalt::InhaltIRSynth::Params params;
            params.feedbackGain = 0.8f;
            params.dampingWeight = 0.4f;

            std::vector<float> left, right;
            inhalt::InhaltIRSynth::render(params, sampleRate, (int) (0.3 * sampleRate), left, right);

            float maxDiff = 0.0f;
            for (size_t i = 0; i < left.size(); ++i)
                maxDiff = std::max(maxDiff, std::abs(left[i] - right[i]));
            expect(maxDiff > 1.0e-4f, "left and right must not be identical");
        }

        beginTest("Rendered channels are substantially decorrelated (disjoint delay-set design)");
        {
            inhalt::InhaltIRSynth::Params params;
            params.feedbackGain = 0.85f;
            params.dampingWeight = 0.4f;
            params.kneeTimeMs = 300.0f;
            // slow fall, so there's plenty of dense signal to correlate
            params.fallRateLowDbPerSec = params.fallRateMidDbPerSec = params.fallRateHighDbPerSec = -100.0f;

            std::vector<float> left, right;
            inhalt::InhaltIRSynth::render(params, sampleRate, (int) (0.5 * sampleRate), left, right);

            const auto corr = std::abs(correlation(left, right));
            expect(corr < 0.3, "L/R correlation should be well below what a shared/split tank would produce");
        }

        beginTest("The gate crushes energy toward the tail past the knee");
        {
            inhalt::InhaltIRSynth::Params params;
            params.feedbackGain = 0.85f;
            params.dampingWeight = 0.4f;
            params.buildUpMs = 3.0f;
            params.kneeTimeMs = 100.0f;
            params.fallRateLowDbPerSec = params.fallRateMidDbPerSec = params.fallRateHighDbPerSec = -400.0f;
            params.kneeSoftnessMs = 3.0f;

            std::vector<float> left, right;
            const auto numSamples = (int) (0.5 * sampleRate);
            inhalt::InhaltIRSynth::render(params, sampleRate, numSamples, left, right);

            auto peakInWindow = [&](double centerSeconds, double windowSeconds)
            {
                const auto center = (int) (centerSeconds * sampleRate);
                const auto half = (int) (windowSeconds * 0.5 * sampleRate);
                const auto lo = std::max(0, center - half);
                const auto hi = std::min((int) left.size(), center + half);
                float peak = 0.0f;
                for (int i = lo; i < hi; ++i)
                    peak = std::max(peak, std::abs(left[(size_t) i]));
                return peak;
            };

            const auto beforeKnee = peakInWindow(0.06, 0.03);
            const auto wellPastKnee = peakInWindow(0.45, 0.03);
            expect(beforeKnee > 0.0f, "should have energy before the knee");
            expect(wellPastKnee < beforeKnee * 0.1f,
                "energy well past the knee (with a steep fall rate) should be crushed far below the pre-knee level");
        }

        beginTest("earlyExcessDb only affects the render at/after the knee, and pulls the level down there");
        {
            // Fixes a real "let's go back to trying to get it to have the same decay and timing
            // as the convolution" complaint - real captures' post-knee fall is CURVED, not the
            // single constant dB/s rate fallRateDbPerSec alone can represent (see
            // InhaltIRSynth.h's own earlyExcessDb comment). Guards the two properties that make
            // this a safe, additive extension rather than a regression risk: (1) it must be a
            // pure no-op before the knee (attack/plateau region untouched), and (2) a negative
            // value must measurably lower the level shortly after the knee.
            inhalt::InhaltIRSynth::Params paramsNeutral;
            paramsNeutral.feedbackGain = 0.85f;
            paramsNeutral.dampingWeight = 0.4f;
            paramsNeutral.buildUpMs = 3.0f;
            paramsNeutral.kneeTimeMs = 100.0f;
            paramsNeutral.fallRateLowDbPerSec = paramsNeutral.fallRateMidDbPerSec
                = paramsNeutral.fallRateHighDbPerSec = -150.0f;
            paramsNeutral.kneeSoftnessMs = 3.0f;
            paramsNeutral.earlyExcessDb = 0.0f;

            auto paramsExcess = paramsNeutral;
            paramsExcess.earlyExcessDb = -10.0f;
            paramsExcess.earlyExcessTauMs = 8.0f;

            const auto numSamples = (int) (0.5 * sampleRate);
            std::vector<float> leftNeutral, rightNeutral, leftExcess, rightExcess;
            inhalt::InhaltIRSynth::render(paramsNeutral, sampleRate, numSamples, leftNeutral, rightNeutral);
            inhalt::InhaltIRSynth::render(paramsExcess, sampleRate, numSamples, leftExcess, rightExcess);

            const auto beforeKneeSamples = (int) (0.08 * sampleRate); // well before the 100ms knee
            bool identicalBeforeKnee = true;
            for (int i = 0; i < beforeKneeSamples; ++i)
            {
                if (std::abs(leftNeutral[(size_t) i] - leftExcess[(size_t) i]) > 1.0e-7f)
                {
                    identicalBeforeKnee = false;
                    break;
                }
            }
            expect(identicalBeforeKnee,
                "earlyExcessDb must be a pure no-op before the knee - it should not touch the attack/plateau region");

            auto peakInWindow = [&](const std::vector<float>& buf, double centerSeconds, double windowSeconds)
            {
                const auto center = (int) (centerSeconds * sampleRate);
                const auto half = (int) (windowSeconds * 0.5 * sampleRate);
                const auto lo = std::max(0, center - half);
                const auto hi = std::min((int) buf.size(), center + half);
                float peak = 0.0f;
                for (int i = lo; i < hi; ++i)
                    peak = std::max(peak, std::abs(buf[(size_t) i]));
                return peak;
            };
            // Shortly after the knee (within a couple of earlyExcessTauMs), the -10dB excess
            // render should be measurably quieter than the neutral one.
            const auto neutralShortlyAfterKnee = peakInWindow(leftNeutral, 0.115, 0.01);
            const auto excessShortlyAfterKnee = peakInWindow(leftExcess, 0.115, 0.01);
            expect(neutralShortlyAfterKnee > 0.0f, "should have energy shortly after the knee");
            expect(excessShortlyAfterKnee < neutralShortlyAfterKnee * 0.5f,
                "a -10dB earlyExcessDb should measurably lower the level shortly after the knee");
        }

        beginTest("Direct/early tap measurably raises energy in the first ~1ms vs. the tank alone");
        {
            // Guards the actual reason the direct tap exists: a real, ear-caught complaint that
            // this plugin's render "retains the pluck/attack/envelope of the synth" where the
            // real hardware's convolution "sounds almost like a bow across strings" - traced to
            // the tank alone being EXACTLY ZERO until its shortest delay line's first round trip
            // (~10ms), a true silence gap the real captures don't have (they measure 13-16% of
            // eventual peak within the first sample or two). Checked over a short WINDOW (first
            // ~50 samples/~1.1ms), not literally sample 0 - the gate's own build-up envelope
            // (gateEnvelopeDb's attack term) forces sample 0 itself to a ~-120dB floor by
            // construction regardless of the direct tap, then ramps up rapidly over the next
            // several dozen samples; a window-based RMS check is what the real calibration (see
            // InhaltParameterMap.cpp's own directGainConstant comment) actually measured.
            inhalt::InhaltIRSynth::Params params;
            params.feedbackGain = 0.85f;
            params.dampingWeight = 0.4f;
            params.diffuserGain = 0.6f;

            auto windowRms = [](const std::vector<float>& buf, int length)
            {
                double sumSq = 0.0;
                for (int i = 0; i < length && i < (int) buf.size(); ++i)
                    sumSq += (double) buf[(size_t) i] * (double) buf[(size_t) i];
                return std::sqrt(sumSq / (double) length);
            };

            params.directGain = 0.0f;
            std::vector<float> leftNoDirect, rightNoDirect;
            inhalt::InhaltIRSynth::render(params, sampleRate, (int) (0.02 * sampleRate), leftNoDirect, rightNoDirect);
            const auto rmsNoDirect = windowRms(leftNoDirect, 50);

            params.directGain = 0.79f;
            std::vector<float> leftWithDirect, rightWithDirect;
            inhalt::InhaltIRSynth::render(params, sampleRate, (int) (0.02 * sampleRate), leftWithDirect, rightWithDirect);
            const auto rmsWithDirect = windowRms(leftWithDirect, 50);

            expect(rmsWithDirect > rmsNoDirect * 5.0f,
                "the direct tap should measurably raise energy in the first ~1ms, not just add noise");
            // Each allpass stage's own impulse response is a decaying comb (nonzero only at
            // multiples of its own delay), so a SINGLE sample index can coincidentally match
            // between the two differently-delayed diffusers even though their overall responses
            // differ - checked as a max-abs-difference over a window instead of one index.
            float maxDiff = 0.0f;
            for (int i = 0; i < 300 && i < (int) leftWithDirect.size(); ++i)
                maxDiff = std::max(maxDiff, std::abs(leftWithDirect[(size_t) i] - rightWithDirect[(size_t) i]));
            expect(maxDiff > 1.0e-4f,
                "left and right channels must diverge once their differently-delayed diffusers "
                "have each round-tripped (independent per-channel diffusers, not a shared mono tap)");
        }

        beginTest("Input diffuser measurably increases onset density vs. no diffuser");
        {
            // Guards the actual reason the diffuser chain exists: a real, ear-caught gap where
            // this plugin's first render had a measurably thinner/sparser initial attack than the
            // real hardware captures (see InhaltIRSynth.h's own comment and
            // ml-toolkit/core.fit.onset_density_loss). Without a diffuser, a tank line's very
            // first arrival is a single, isolated spike at exactly its own delay time; with the
            // diffuser, that single spike is smeared into several arrivals scattered across the
            // next few ms - this counts above-threshold samples in that window as a density proxy.
            //
            // Uses an ABSOLUTE threshold, not each case's own local peak (an earlier version did,
            // and a later crossover change - splitting the low band's own gate into subLow/low -
            // flipped it from passing to failing by coincidence): even at diffuserGain=0 the
            // diffuser is a pure 3-stage serial delay chain (~5.6ms total, see
            // leftDiffuserDelaysMs's own sum), so the tank's true first arrival lands at
            // ~5.6ms + its shortest line (~10.9ms) = ~16.5ms, well AFTER this window ends - the
            // no-diffuser case is genuine floating-point-noise-level silence here (~1e-10), not a
            // real signal, so a peak-relative threshold on it is measuring noise shape, not signal.
            // An absolute threshold well above float noise and well below a real diffuser arrival
            // (~1e-2 to 1e-1, see the with-diffuser case) makes the comparison architecture-
            // independent: no-diffuser must count exactly zero real samples here, with-diffuser
            // must count many, from the diffuser's OWN allpass feedback recirculating almost
            // immediately (unlike the diffuserGain=0 case's simple one-shot delay).
            auto countActiveSamples = [](const std::vector<float>& buf, int start, int length, float absoluteThreshold)
            {
                int count = 0;
                for (int i = start; i < start + length && i < (int) buf.size(); ++i)
                    if (std::abs(buf[(size_t) i]) > absoluteThreshold)
                        ++count;
                return count;
            };

            inhalt::InhaltIRSynth::Params params;
            params.feedbackGain = 0.85f;
            params.dampingWeight = 0.4f;
            params.kneeTimeMs = 300.0f;
            // slow fall - plenty of onset density to observe
            params.fallRateSubLowDbPerSec = params.fallRateLowDbPerSec = params.fallRateMidDbPerSec = params.fallRateHighDbPerSec = -50.0f;

            params.diffuserGain = 0.0f;
            std::vector<float> leftNoDiffuser, rightNoDiffuser;
            inhalt::InhaltIRSynth::render(params, sampleRate, (int) (0.05 * sampleRate), leftNoDiffuser, rightNoDiffuser);

            params.diffuserGain = 0.7f;
            std::vector<float> leftWithDiffuser, rightWithDiffuser;
            inhalt::InhaltIRSynth::render(params, sampleRate, (int) (0.05 * sampleRate), leftWithDiffuser, rightWithDiffuser);

            // Window chosen to sit BEFORE the no-diffuser case's true first arrival (~16.5ms, see
            // above) - the window covers 10.1-15.1ms, genuine silence with no diffuser.
            const auto windowStart = (int) (0.0101 * sampleRate);
            const auto windowLength = (int) (0.005 * sampleRate);
            const auto countNoDiffuser = countActiveSamples(leftNoDiffuser, windowStart, windowLength, 1e-4f);
            const auto countWithDiffuser = countActiveSamples(leftWithDiffuser, windowStart, windowLength, 1e-4f);

            expect(countWithDiffuser > countNoDiffuser,
                "a diffused impulse should produce measurably MORE above-threshold samples in the "
                "onset window than a bare impulse");
        }

        beginTest("Diffuser gain is clamped safely even if given out-of-range input");
        {
            inhalt::InhaltIRSynth::Params params;
            params.feedbackGain = 0.8f;
            params.dampingWeight = 0.4f;
            params.diffuserGain = 5.0f; // well past the documented 0.9 ceiling

            std::vector<float> left, right;
            inhalt::InhaltIRSynth::render(params, sampleRate, (int) (0.2 * sampleRate), left, right);

            expect(! hasNaNOrInf(left), "an out-of-range diffuser gain should still produce a finite, stable render");
            expect(! hasNaNOrInf(right), "an out-of-range diffuser gain should still produce a finite, stable render");
        }

        beginTest("No inter-sample discontinuity beyond a small tolerance, across the parameter range");
        {
            for (float feedbackGain : { 0.1f, 0.5f, inhalt::InhaltIRSynth::maxFeedbackGain })
            {
                for (float fallRate : { -20.0f, -500.0f, -2000.0f })
                {
                    inhalt::InhaltIRSynth::Params params;
                    params.feedbackGain = feedbackGain;
                    params.dampingWeight = 0.4f;
                    params.fallRateLowDbPerSec = params.fallRateMidDbPerSec = params.fallRateHighDbPerSec = fallRate;
                    params.kneeTimeMs = 100.0f;

                    std::vector<float> left, right;
                    inhalt::InhaltIRSynth::render(params, sampleRate, (int) (0.3 * sampleRate), left, right);

                    float maxStep = 0.0f;
                    for (size_t i = 1; i < left.size(); ++i)
                        maxStep = std::max(maxStep, std::abs(left[i] - left[i - 1]));
                    // A generous bound - this guards against a genuine discontinuity bug (a NaN
                    // sanitisation glitch, an off-by-one in the gate formula), not against the
                    // FDN's own normal sample-to-sample variation, which at these delay lengths
                    // and gains can legitimately be large right at the impulse's first arrivals.
                    expect(maxStep < 2.0f, "no single-sample jump should exceed a generous bound");
                }
            }
        }

        beginTest("feedbackGain/dampingWeight are clamped to their documented ceilings even if given out-of-range input");
        {
            inhalt::InhaltIRSynth::Params params;
            params.feedbackGain = 5.0f;   // well past maxFeedbackGain
            params.dampingWeight = 5.0f;  // well past maxDampingWeight

            std::vector<float> left, right;
            inhalt::InhaltIRSynth::render(params, sampleRate, (int) (0.2 * sampleRate), left, right);

            expect(! hasNaNOrInf(left), "clamped feedback gain should still produce a finite, stable render");
            expect(! hasNaNOrInf(right), "clamped feedback gain should still produce a finite, stable render");
        }

        beginTest("Rendering zero samples is a safe no-op");
        {
            inhalt::InhaltIRSynth::Params params;
            std::vector<float> left, right;
            inhalt::InhaltIRSynth::render(params, sampleRate, 0, left, right);
            expect(left.empty() && right.empty(), "zero-length render should produce empty buffers");
        }
    }
};

static InhaltIRSynthTests inhaltIRSynthTests;
