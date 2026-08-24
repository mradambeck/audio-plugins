#include "../KarplunkLoopFilter.h"

#include <juce_core/juce_core.h>

#include <cmath>

class KarplunkLoopFilterTests : public juce::UnitTest
{
public:
    KarplunkLoopFilterTests() : juce::UnitTest("TwoPointAverageLoopFilter", "Karplunk") {}

    void runTest() override
    {
        beginTest("Impulse response matches the classic two-point average, scaled by loop gain");
        {
            TwoPointAverageLoopFilter filter;
            filter.prepare(44100.0);
            filter.setDamping(1.0f); // maxLoopGain = 0.9995

            constexpr float expectedGain = 0.9995f;
            const auto y0 = filter.processSample(1.0f);
            const auto y1 = filter.processSample(0.0f);
            const auto y2 = filter.processSample(0.0f);

            // y[n] = g * 0.5 * (x[n] + x[n-1]): y0 = g*0.5*(1+0), y1 = g*0.5*(0+1), y2 = g*0.5*(0+0)
            expectWithinAbsoluteError(y0, expectedGain * 0.5f, 1.0e-5f);
            expectWithinAbsoluteError(y1, expectedGain * 0.5f, 1.0e-5f);
            expectWithinAbsoluteError(y2, 0.0f, 1.0e-5f);
        }

        beginTest("setDamping(0) and setDamping(1) hit the documented gain floor/ceiling");
        {
            TwoPointAverageLoopFilter minFilter;
            minFilter.prepare(44100.0);
            minFilter.setDamping(0.0f);
            const auto minY = minFilter.processSample(1.0f);
            expectWithinAbsoluteError(minY, 0.90f * 0.5f, 1.0e-5f);

            TwoPointAverageLoopFilter maxFilter;
            maxFilter.prepare(44100.0);
            maxFilter.setDamping(1.0f);
            const auto maxY = maxFilter.processSample(1.0f);
            expectWithinAbsoluteError(maxY, 0.9995f * 0.5f, 1.0e-5f);
        }

        beginTest("A sustained unit input settles to a stable output, never grows unbounded");
        {
            TwoPointAverageLoopFilter filter;
            filter.prepare(44100.0);
            filter.setDamping(1.0f); // longest sustain - the highest-risk setting for runaway growth

            float lastOutput = 0.0f;
            for (int i = 0; i < 10000; ++i)
            {
                lastOutput = filter.processSample(1.0f);
                expect(std::abs(lastOutput) <= 1.0f, "output should never exceed the input's own magnitude for a one-zero averaging filter with gain < 1");
            }
        }

        beginTest("reset() clears history");
        {
            TwoPointAverageLoopFilter filter;
            filter.prepare(44100.0);
            filter.setDamping(1.0f);
            filter.processSample(1.0f);
            filter.reset();
            const auto y = filter.processSample(0.0f);
            expectWithinAbsoluteError(y, 0.0f, 1.0e-6f);
        }
    }
};

static KarplunkLoopFilterTests karplunkLoopFilterTests;

namespace
{
    // Sine-in/settle/RMS-ratio magnitude measurement - the same technique
    // KarplunkVoiceTests.cpp's KarplunkDispersionFilter magnitude test already uses. Feeds a sine
    // at `testOmega` (radians/sample) through the filter (configured via `freqHz`/`q`, its own
    // design frequency in Hz and Q), discards a settling transient, then measures the output/input
    // RMS ratio over the remainder.
    //
    // The settling window is Q/freqHz-ADAPTIVE, not a fixed sample count - a high-Q, low-frequency
    // resonator's own pole sits very close to the unit circle (pole radius r -> 1 as Q grows or
    // freqHz shrinks), giving it a settling time constant of roughly 1/(1-r) samples. At Q=20,
    // freqHz=80Hz, 44.1kHz this is ~3500 samples - a fixed short window (first attempt: 4000 total,
    // 500 discarded) genuinely measured the filter mid-transient, not at steady state, and produced
    // several spurious "magnitude != 1.0" failures that were a TEST bug, not a real flaw (confirmed
    // independently in Python before changing this - the closed-form derivation in this class's own
    // header comment is exact, not approximate). Discarding 10 time constants and then measuring
    // for 5 more is generous margin for genuine settling, at any Q/frequency/sample-rate combination
    // this file exercises.
    float measurePeakFilterMagnitude(float freqHz, float q, double sampleRate, float testFreqHz)
    {
        KarplunkResonantPeakFilter filter;
        filter.prepare(sampleRate);

        const auto testOmega = 2.0f * 3.14159265358979323846f * testFreqHz / (float) sampleRate;

        const auto w0 = 2.0 * 3.14159265358979323846 * (double) freqHz / sampleRate;
        const auto alpha = std::sin(w0) / (2.0 * (double) q);
        const auto a2 = (1.0 - alpha) / (1.0 + alpha);
        const auto poleRadius = std::sqrt(std::max(a2, 0.0));
        const auto timeConstantSamples = 1.0 / std::max(1.0 - poleRadius, 1.0e-6);
        const auto discard = (int) std::min(2.0e6, 10.0 * timeConstantSamples);
        const auto measureWindow = (int) std::min(2.0e6, 5.0 * timeConstantSamples) + 500;
        const auto totalSamples = discard + measureWindow;

        double inEnergy = 0.0, outEnergy = 0.0;
        for (int n = 0; n < totalSamples; ++n)
        {
            const auto in = std::sin(testOmega * (float) n);
            const auto out = filter.process(in, freqHz, q);
            if (n >= discard)
            {
                inEnergy += (double) in * (double) in;
                outEnergy += (double) out * (double) out;
            }
        }
        return (float) std::sqrt(outEnergy / inEnergy);
    }
}

class KarplunkResonantPeakFilterTests : public juce::UnitTest
{
public:
    KarplunkResonantPeakFilterTests() : juce::UnitTest("KarplunkResonantPeakFilter", "Karplunk") {}

    void runTest() override
    {
        beginTest("Magnitude at the filter's own design frequency is exactly 1.0, for any Q/frequency/sample rate");
        {
            // Direct empirical verification of the class's own closed-form claim (derived, not
            // just trusted from the cookbook formula, given how load-bearing this is - see the
            // class's own header comment) - |H(e^jw0)| = 1 exactly, independent of Q or w0.
            for (double sampleRate : { 44100.0, 48000.0, 96000.0 })
                for (float freqHz : { 80.0f, 200.0f, 1000.0f, 4000.0f, 8000.0f })
                    for (float q : { 0.5f, 0.7f, 2.0f, 5.0f, 10.0f, 20.0f })
                    {
                        const auto ratio = measurePeakFilterMagnitude(freqHz, q, sampleRate, freqHz);
                        expectWithinAbsoluteError(ratio, 1.0f, 0.02f,
                                                   "magnitude at the filter's own design frequency should be exactly 1.0");
                    }
        }

        beginTest("Dense frequency sweep: magnitude never exceeds 1.0 anywhere, at any Q/design-frequency/sample rate");
        {
            // The direct numeric verification of "single resonant maximum of exactly 1 at w0,
            // nowhere higher" - not just trusted from the DC/Nyquist-zero derivation.
            for (double sampleRate : { 44100.0, 96000.0 })
                for (float freqHz : { 200.0f, 1000.0f, 4000.0f })
                    for (float q : { 0.7f, 5.0f, 10.0f })
                        for (int step = 1; step <= 40; ++step)
                        {
                            const auto testFreqHz = (float) sampleRate * 0.49f * ((float) step / 40.0f);
                            const auto ratio = measurePeakFilterMagnitude(freqHz, q, sampleRate, testFreqHz);
                            expect(ratio <= 1.0f + 0.02f,
                                   "magnitude should never exceed 1.0 at any frequency, for any Q/design-frequency/sample rate");
                        }
        }

        beginTest("reset() clears history");
        {
            KarplunkResonantPeakFilter filter;
            filter.prepare(44100.0);
            for (int i = 0; i < 100; ++i)
                filter.process(1.0f, 1000.0f, 5.0f);
            filter.reset();
            const auto y = filter.process(0.0f, 1000.0f, 5.0f);
            expectWithinAbsoluteError(y, 0.0f, 1.0e-6f);
        }
    }
};

static KarplunkResonantPeakFilterTests karplunkResonantPeakFilterTests;

class KarplunkResonantLoopFilterTests : public juce::UnitTest
{
public:
    KarplunkResonantLoopFilterTests() : juce::UnitTest("KarplunkResonantLoopFilter", "Karplunk") {}

    void runTest() override
    {
        beginTest("Resonance=0 is bit-identical to a standalone TwoPointAverageLoopFilter, at several Damping values");
        {
            for (float damping : { 0.0f, 0.3f, 0.6f, 0.9f, 1.0f })
            {
                KarplunkResonantLoopFilter resonant;
                resonant.prepare(44100.0);
                resonant.setDamping(damping);
                resonant.setResonance(0.0f);

                TwoPointAverageLoopFilter plain;
                plain.prepare(44100.0);
                plain.setDamping(damping);

                for (int i = 0; i < 2000; ++i)
                {
                    const auto x = std::sin((float) i * 0.05f);
                    expectWithinAbsoluteError(resonant.processSample(x), plain.processSample(x), 1.0e-6f,
                                               "Resonance=0 should be a bit-exact bypass to the plain two-point average");
                }
            }
        }

        beginTest("getLoopGain() matches g*(1-resonanceAmount) exactly, across a grid of Damping/Resonance");
        {
            for (float damping : { 0.0f, 0.5f, 1.0f })
                for (float resonance : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
                {
                    KarplunkResonantLoopFilter filter;
                    filter.prepare(44100.0);
                    filter.setDamping(damping);
                    filter.setResonance(resonance);

                    TwoPointAverageLoopFilter reference;
                    reference.prepare(44100.0);
                    reference.setDamping(damping);

                    const auto expectedGain = reference.getLoopGain() * (1.0f - resonance);
                    expectWithinAbsoluteError(filter.getLoopGain(), expectedGain, 1.0e-6f);
                }
        }

        beginTest("Combined magnitude response never exceeds 0.9995 anywhere, across Damping/Resonance/Formant/sample-rate");
        {
            // The direct empirical validation of this class's own closed-form safety argument
            // (|H_total(w)| <= |H_TwoPoint(w)| * |H_mix(w)| <= 0.9995 for every frequency, every
            // control combination) - measured, not just trusted from the derivation.
            for (double sampleRate : { 44100.0, 96000.0 })
                for (float damping : { 0.5f, 1.0f })
                    for (float resonance : { 0.5f, 1.0f })
                        for (float formantHz : { 200.0f, 1000.0f, 4000.0f })
                        {
                            for (int step = 1; step <= 30; ++step)
                            {
                                const auto testFreqHz = (float) sampleRate * 0.49f * ((float) step / 30.0f);
                                const auto testOmega = 2.0f * 3.14159265358979323846f * testFreqHz / (float) sampleRate;

                                KarplunkResonantLoopFilter sweepFilter;
                                sweepFilter.prepare(sampleRate);
                                sweepFilter.setDamping(damping);
                                sweepFilter.setResonance(resonance);
                                sweepFilter.setFormantFrequency(formantHz);

                                constexpr int totalSamples = 4000;
                                constexpr int discard = 500;
                                double inEnergy = 0.0, outEnergy = 0.0;
                                for (int n = 0; n < totalSamples; ++n)
                                {
                                    const auto in = std::sin(testOmega * (float) n);
                                    const auto out = sweepFilter.processSample(in);
                                    if (n >= discard)
                                    {
                                        inEnergy += (double) in * (double) in;
                                        outEnergy += (double) out * (double) out;
                                    }
                                }
                                const auto ratio = (float) std::sqrt(outEnergy / inEnergy);
                                expect(ratio <= 0.9995f + 0.02f,
                                       "combined loop filter magnitude should never exceed the existing safe ceiling, at any frequency/control combination");
                            }
                        }
        }

        beginTest("A sustained unit input at max Damping/Resonance never grows unbounded");
        {
            KarplunkResonantLoopFilter filter;
            filter.prepare(44100.0);
            filter.setDamping(1.0f);
            filter.setResonance(1.0f);
            filter.setFormantFrequency(1000.0f);

            for (int i = 0; i < 10000; ++i)
            {
                const auto y = filter.processSample(1.0f);
                expect(std::isfinite(y) && std::abs(y) <= 1.0f,
                       "output should never exceed the input's own magnitude, even at max Resonance");
            }
        }

        beginTest("reset() clears both internal filters' history");
        {
            KarplunkResonantLoopFilter filter;
            filter.prepare(44100.0);
            filter.setDamping(1.0f);
            filter.setResonance(1.0f);
            for (int i = 0; i < 100; ++i)
                filter.processSample(1.0f);
            filter.reset();

            const auto y = filter.processSample(0.0f);
            expectWithinAbsoluteError(y, 0.0f, 1.0e-6f);
        }
    }
};

static KarplunkResonantLoopFilterTests karplunkResonantLoopFilterTests;
