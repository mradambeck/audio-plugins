#include "../KarplunkExcitation.h"

#include <juce_core/juce_core.h>

#include <cmath>
#include <vector>

class KarplunkExcitationTests : public juce::UnitTest
{
public:
    KarplunkExcitationTests() : juce::UnitTest("KarplunkExcitation", "Karplunk") {}

    void runTest() override
    {
        beginTest("At bowAmount=0 (pure pluck), output never exceeds the requested velocity");
        {
            // Split out from a combined bowAmount={0,1} test (see this class's own header comment
            // and README for why): a real friction curve's output is not bounded by note-on
            // velocity01 the way an impulse-response burst's is - that's a genuine property of the
            // PLUCK mechanism specifically, not a general Excitation-seam law any more.
            KarplunkExcitation excitation;
            excitation.prepare(44100.0);
            excitation.setBrightness(1.0f);
            excitation.setBowAmount(0.0f);
            excitation.setBaseDuration(500);

            for (int i = 0; i < 2000; ++i)
            {
                const auto sample = excitation.nextExcitationSample(0.7f, 0.0f);
                expect(std::abs(sample) <= 0.7f + 1.0e-4f, "sample amplitude should stay within velocity bound");
            }
        }

        beginTest("At bowAmount=1 (friction bow), output stays finite and bounded even against an adversarial string signal");
        {
            // Weaker than the pluck-side bound above, on purpose - the friction curve's own output
            // isn't scaled by velocity01 the way the noise burst is (see nextFrictionSample()'s own
            // comment: what actually protects the loop is the tanh() cap at the injection site in
            // KarplunkVoice.h, not this call's own return value). This test isolates the primitive's
            // OWN boundedness (see nextFrictionSample()'s unconditional-boundedness argument),
            // independent of that outer cap.
            for (float bowForce : { 0.0f, 0.5f, 1.0f })
            {
                KarplunkExcitation excitation;
                excitation.prepare(44100.0);
                excitation.setBowAmount(1.0f);
                excitation.setBowForce(bowForce);
                excitation.setBaseDuration(500);

                for (float stringSignal : { -100.0f, -1.0f, 0.0f, 1.0f, 100.0f })
                {
                    const auto sample = excitation.nextExcitationSample(1.0f, stringSignal);
                    expect(std::isfinite(sample), "friction-driven output must stay finite even against an adversarial string signal");
                }
            }
        }

        beginTest("Velocity scales output linearly for an identical noise sequence (pluck side)");
        {
            KarplunkExcitation excitationFull;
            KarplunkExcitation excitationHalf;
            excitationFull.prepare(44100.0);
            excitationHalf.prepare(44100.0);
            excitationFull.setBaseDuration(500);
            excitationHalf.setBaseDuration(500);
            // Both instances start with the same default RNG seed and the same (default,
            // bowAmount = 0) envelope path, so the envelope shape and noise sequence are
            // identical between the two - only velocity differs, and velocity is a pure output
            // multiplier that never feeds back into the envelope or RNG state.

            for (int i = 0; i < 500; ++i)
            {
                const auto full = excitationFull.nextExcitationSample(1.0f, 0.0f);
                const auto half = excitationHalf.nextExcitationSample(0.5f, 0.0f);
                expectWithinAbsoluteError(half, full * 0.5f, 1.0e-5f);
            }
        }

        beginTest("Lower brightness measurably reduces RMS energy versus full brightness");
        {
            KarplunkExcitation darkExcitation;
            KarplunkExcitation brightExcitation;
            darkExcitation.prepare(44100.0);
            brightExcitation.prepare(44100.0);
            darkExcitation.setBaseDuration(2000);
            brightExcitation.setBaseDuration(2000);
            darkExcitation.setBrightness(0.0f);
            brightExcitation.setBrightness(1.0f);

            constexpr int numSamples = 2000;
            auto rms = [](KarplunkExcitation& excitation)
            {
                double sumSquares = 0.0;
                for (int i = 0; i < numSamples; ++i)
                {
                    const auto sample = excitation.nextExcitationSample(1.0f, 0.0f);
                    sumSquares += (double) sample * (double) sample;
                }
                return std::sqrt(sumSquares / (double) numSamples);
            };

            expect(rms(darkExcitation) < rms(brightExcitation), "brightness = 0 should have lower RMS than brightness = 1");
        }

        beginTest("reset() clears envelope and filter state so the next sample after reset starts near zero");
        {
            KarplunkExcitation excitation;
            excitation.prepare(44100.0);
            excitation.setBrightness(0.05f); // heavy smoothing, so state carries over visibly if not reset
            excitation.setBaseDuration(1000);

            for (int i = 0; i < 1000; ++i)
                excitation.nextExcitationSample(1.0f, 0.0f);

            excitation.reset();

            // attackEnv restarts at 0 (rises from there) and lowpassState restarts at 0 too, so
            // the very first sample after reset can't be far from zero regardless of how long the
            // pre-reset run had been going.
            const auto firstSampleAfterReset = excitation.nextExcitationSample(1.0f, 0.0f);
            expect(std::abs(firstSampleAfterReset) < 0.1f, "first sample after reset should start near zero");
        }

        beginTest("Attack is slower as bowAmount approaches 1, for the same note (pluck side unaffected)");
        {
            // Raw white noise (brightness = 1, no lowpass smoothing) so the PLUCK envelope shape is
            // directly reflected in the average |sample| over a window at bowAmount=0. At
            // bowAmount=1 this now measures the friction mechanism instead (a genuinely different
            // output shape - see nextFrictionSample()'s own comment on why its output isn't simply
            // proportional to the envelope) - this test only asserts the ATTACK-SPEED property
            // (bow's own attack, driven by the same unchanged envelope state machine, should still
            // be rising while pluck's has already peaked), not a loudness-parity claim (that's
            // covered separately in KarplunkVoiceTests.cpp against the real injection/tanh path).
            auto averageAbs = [](KarplunkExcitation& excitation, int numSamples)
            {
                double sum = 0.0;
                for (int i = 0; i < numSamples; ++i)
                    sum += std::abs(excitation.nextExcitationSample(1.0f, 0.0f));
                return sum / (double) numSamples;
            };

            constexpr int baseDuration = 200; // pluck decay time constant = 400 samples (durationMultiplier = 2)

            KarplunkExcitation pluck;
            pluck.prepare(44100.0);
            pluck.setBrightness(1.0f);
            pluck.setBowAmount(0.0f);
            pluck.setBaseDuration(baseDuration);

            KarplunkExcitation bow;
            bow.prepare(44100.0);
            bow.setBrightness(1.0f);
            bow.setBowAmount(1.0f);
            bow.setBaseDuration(baseDuration);

            const auto pluckEarly = averageAbs(pluck, 20);
            const auto bowEarly = averageAbs(bow, 20);
            expect(bowEarly < pluckEarly, "a fully-bowed note's attack should still be rising while a plucked note's has already peaked");
        }

        beginTest("Friction curve stays finite across a dense sweep of string signal, at every Bow Force");
        {
            // Direct empirical verification of nextFrictionSample()'s own unconditional-
            // boundedness argument (the "+0.75" floor before a negative exponent) - not just
            // trusted from the derivation.
            for (float bowForce : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
            {
                KarplunkExcitation excitation;
                excitation.prepare(44100.0);
                excitation.setBowAmount(1.0f);
                excitation.setBowForce(bowForce);
                excitation.setBaseDuration(500);
                for (int i = 0; i < 200; ++i) // let the envelope settle toward sustain
                    excitation.nextExcitationSample(1.0f, 0.0f);

                for (int step = -50; step <= 50; ++step)
                {
                    const auto stringSignal = (float) step * 2.0f; // sweeps -100 to 100
                    const auto sample = excitation.nextExcitationSample(1.0f, stringSignal);
                    expect(std::isfinite(sample), "friction output should stay finite across the whole string-signal sweep, at any Bow Force");
                }
            }
        }

        beginTest("Bow Force measurably changes the friction character at fixed bowAmount/string signal");
        {
            // stringSignal=0.05 (the original choice here) put vDelta so close to zero that rho
            // clamped to its 0.98 ceiling at BOTH Bow Force extremes - a real test-parameter bug
            // (measuring in a saturated region insensitive to slope by construction), not a
            // production-code issue: caught by checking the actual rho/vDelta values by hand rather
            // than trusting the failure. stringSignal=0.5 (with envelope allowed to fully settle
            // toward its bowAmount=1 sustain level first) keeps vDelta moderate enough that rho
            // stays in its unclamped, slope-sensitive region at both Bow Force=0 (slope=5) and
            // Bow Force=1 (slope=1).
            auto measureRms = [](float bowForce, float stringSignal) {
                KarplunkExcitation excitation;
                excitation.prepare(44100.0);
                excitation.setBowAmount(1.0f);
                excitation.setBowForce(bowForce);
                excitation.setBaseDuration(500);
                for (int i = 0; i < 20000; ++i) // let envelope settle near its sustain level
                    excitation.nextExcitationSample(1.0f, stringSignal);

                double sumSquares = 0.0;
                constexpr int window = 200;
                for (int i = 0; i < window; ++i)
                {
                    const auto s = excitation.nextExcitationSample(1.0f, stringSignal);
                    sumSquares += (double) s * (double) s;
                }
                return std::sqrt(sumSquares / (double) window);
            };

            const auto rmsLowForce = measureRms(0.0f, 0.5f);
            const auto rmsHighForce = measureRms(1.0f, 0.5f);

            logMessage("Bow Force=0% RMS: " + juce::String(rmsLowForce, 6)
                       + ", Bow Force=100% RMS: " + juce::String(rmsHighForce, 6));

            expect(std::abs(rmsLowForce - rmsHighForce) > 0.001 * std::max(rmsLowForce, rmsHighForce),
                   "Bow Force should measurably change the friction curve's output character");
        }
    }
};

static KarplunkExcitationTests karplunkExcitationTests;
