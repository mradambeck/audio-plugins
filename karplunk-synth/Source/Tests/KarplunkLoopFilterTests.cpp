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
    float measureLowpassMagnitude(float cutoffHz, float q, double sampleRate, float testFreqHz)
    {
        KarplunkLowpassFilter filter;
        filter.prepare(sampleRate);

        const auto testOmega = 2.0f * 3.14159265358979323846f * testFreqHz / (float) sampleRate;

        const auto w0 = 2.0 * 3.14159265358979323846 * (double) cutoffHz / sampleRate;
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
            const auto out = filter.process(in, cutoffHz, q);
            if (n >= discard)
            {
                inEnergy += (double) in * (double) in;
                outEnergy += (double) out * (double) out;
            }
        }
        return (float) std::sqrt(outEnergy / inEnergy);
    }
}

class KarplunkLowpassFilterTests : public juce::UnitTest
{
public:
    KarplunkLowpassFilterTests() : juce::UnitTest("KarplunkLowpassFilter", "Karplunk") {}

    void runTest() override
    {
        beginTest("DC (very low test frequency) passes through near-unchanged, for any Cutoff/Q/sample rate");
        {
            // A real lowpass's defining property: DC/very-low frequencies pass through
            // essentially untouched, unlike the old constant-0dB-peak bandpass design (which had
            // an exact DC zero instead).
            for (double sampleRate : { 44100.0, 48000.0, 96000.0 })
                for (float cutoffHz : { 200.0f, 1000.0f, 4000.0f, 8000.0f })
                    for (float q : { 0.7f, 2.0f, 5.0f, 18.0f })
                    {
                        // Scaled relative to cutoffHz, not a fixed 20Hz - a high-Q lowpass has
                        // real passband ripple approaching its own resonant peak, so "very low"
                        // needs to stay comfortably below that region at every tested Cutoff.
                        const auto ratio = measureLowpassMagnitude(cutoffHz, q, sampleRate, cutoffHz / 50.0f);
                        expectWithinAbsoluteError(ratio, 1.0f, 0.1f,
                                                   "a lowpass should pass a very-low test frequency through near-unchanged");
                    }
        }

        beginTest("Well above Cutoff, the signal is substantially attenuated (real stopband rolloff), at any Q/sample rate");
        {
            for (double sampleRate : { 44100.0, 96000.0 })
                for (float cutoffHz : { 200.0f, 1000.0f, 4000.0f })
                    for (float q : { 0.7f, 5.0f, 18.0f })
                    {
                        const auto ratio = measureLowpassMagnitude(cutoffHz, q, sampleRate, cutoffHz * 8.0f);
                        expect(ratio < 0.3f,
                               "well above Cutoff, the lowpass should attenuate substantially, not pass the signal through");
                    }
        }

        beginTest("Output stays finite and bounded across a dense frequency sweep, at any Cutoff/Q/sample rate - including the resonant peak near Cutoff itself");
        {
            // Unlike the old constant-0dB-peak bandpass, a resonant LOWPASS genuinely CAN exceed
            // unity gain near its own Cutoff at high Q (that's the real, expected "self-
            // oscillation-adjacent" character a synth filter's Resonance is supposed to have) -
            // this test only confirms the response stays FINITE and within a sane bound, not that
            // it never exceeds 1.0 (which would be the wrong property to assert for this design).
            for (double sampleRate : { 44100.0, 96000.0 })
                for (float cutoffHz : { 200.0f, 1000.0f, 4000.0f })
                    for (float q : { 0.7f, 5.0f, 18.0f }) // 18.0 = maxQ, see KarplunkLoopFilter.h
                        for (int step = 1; step <= 40; ++step)
                        {
                            const auto testFreqHz = (float) sampleRate * 0.49f * ((float) step / 40.0f);
                            const auto ratio = measureLowpassMagnitude(cutoffHz, q, sampleRate, testFreqHz);
                            expect(std::isfinite(ratio) && ratio <= 25.0f,
                                   "magnitude should stay finite and within a sane bound at any frequency/Cutoff/Q/sample rate");
                        }
        }

        beginTest("reset() clears history");
        {
            KarplunkLowpassFilter filter;
            filter.prepare(44100.0);
            for (int i = 0; i < 100; ++i)
                filter.process(1.0f, 1000.0f, 5.0f);
            filter.reset();
            const auto y = filter.process(0.0f, 1000.0f, 5.0f);
            expectWithinAbsoluteError(y, 0.0f, 1.0e-6f);
        }
    }
};

static KarplunkLowpassFilterTests karplunkLowpassFilterTests;

class KarplunkResonantLoopFilterTests : public juce::UnitTest
{
public:
    KarplunkResonantLoopFilterTests() : juce::UnitTest("KarplunkResonantLoopFilter", "Karplunk") {}

    void runTest() override
    {
        beginTest("processSample() is bit-identical to a standalone TwoPointAverageLoopFilter, at every Resonance/Damping - Resonance no longer touches the loop at all");
        {
            for (float damping : { 0.0f, 0.3f, 0.6f, 0.9f, 1.0f })
                for (float resonance : { 0.0f, 0.5f, 1.0f })
                {
                    KarplunkResonantLoopFilter resonant;
                    resonant.prepare(44100.0);
                    resonant.setDamping(damping);
                    resonant.setResonance(resonance);

                    TwoPointAverageLoopFilter plain;
                    plain.prepare(44100.0);
                    plain.setDamping(damping);

                    for (int i = 0; i < 2000; ++i)
                    {
                        const auto x = std::sin((float) i * 0.05f);
                        expectWithinAbsoluteError(resonant.processSample(x), plain.processSample(x), 1.0e-6f,
                                                   "the recirculating value should be bit-identical to the plain two-point average at any Resonance");
                    }
                }
        }

        beginTest("getLoopGain() matches TwoPointAverageLoopFilter's own gain exactly, at every Resonance - unaffected by Resonance now");
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

                    expectWithinAbsoluteError(filter.getLoopGain(), reference.getLoopGain(), 1.0e-6f);
                }
        }

        beginTest("outputColor(): a wide-open Cutoff (well above the test signal) is near-transparent, at any Resonance");
        {
            // Unlike the old bandpass design (a bit-exact bypass at Resonance=0), a real lowpass
            // has no natural "off" state tied to Resonance - Resonance is just the filter's own Q
            // now, same as any subtractive synth. The equivalent "basically off" state is Cutoff
            // wide open relative to the signal, matching real analog synth convention.
            for (float resonance : { 0.0f, 0.5f, 1.0f })
            {
                KarplunkResonantLoopFilter filter;
                filter.prepare(44100.0);
                filter.setResonance(resonance);
                filter.setCutoffFrequency(18000.0f);

                for (int i = 0; i < 2000; ++i)
                {
                    const auto x = std::sin((float) i * (2.0f * 3.14159265358979323846f * 200.0f / 44100.0f));
                    expectWithinAbsoluteError(filter.outputColor(x), x, 0.05f,
                                               "a Cutoff far above the test signal should be near-transparent, at any Resonance");
                }
            }
        }

        beginTest("outputColor(): a sustained/decaying loop signal stays finite and bounded across Resonance/Q/Cutoff/sample-rate");
        {
            // outputColor() is fed the loop's own decaying content and is never itself
            // recirculated (see this class's own header comment) - a much simpler property to
            // check now than the old in-loop crossfade needed: just finite/bounded for a bounded
            // input, not a magnitude-response sweep. Resonance=1.0 (maxQ - see
            // KarplunkLoopFilter.h) is the worst case for peak gain near Cutoff.
            for (double sampleRate : { 44100.0, 96000.0 })
                for (float resonance : { 0.5f, 1.0f })
                    for (float cutoffHz : { 200.0f, 1000.0f, 4000.0f })
                    {
                        KarplunkResonantLoopFilter filter;
                        filter.prepare(sampleRate);
                        filter.setResonance(resonance);
                        filter.setCutoffFrequency(cutoffHz);

                        for (int i = 0; i < 10000; ++i)
                        {
                            // A decaying sinusoid AT the filter's own cutoff - matches a real
                            // plucked note's own recirculating content landing right on Cutoff,
                            // the worst case for a resonant peak (see this test's own name).
                            const auto envelope = std::exp(-(float) i / 4000.0f);
                            const auto testOmega = 2.0f * 3.14159265358979323846f * cutoffHz / (float) sampleRate;
                            const auto x = envelope * std::sin((float) i * testOmega);
                            const auto y = filter.outputColor(x);
                            expect(std::isfinite(y), "outputColor() must stay finite at any Resonance/Q/Cutoff/sample-rate");
                            expect(std::abs(y) <= 25.0f, "outputColor() should stay within a sane bound for a bounded input");
                        }
                    }
        }

        beginTest("outputColor() with Resonance>0 measurably differs from its own input - the control actually adds color");
        {
            KarplunkResonantLoopFilter filter;
            filter.prepare(44100.0);
            filter.setResonance(1.0f);
            filter.setCutoffFrequency(1000.0f);

            bool sawDifference = false;
            for (int i = 0; i < 2000; ++i)
            {
                const auto x = std::sin((float) i * (2.0f * 3.14159265358979323846f * 1000.0f / 44100.0f));
                if (std::abs(filter.outputColor(x) - x) > 1.0e-4f)
                    sawDifference = true;
            }
            expect(sawDifference, "outputColor() should audibly color a signal driven at its own Cutoff frequency");
        }

        beginTest("Filter envelope: Envelope Amount=0 leaves Cutoff fixed, regardless of the still-running envelope");
        {
            KarplunkResonantLoopFilter filter;
            filter.prepare(44100.0);
            filter.setResonance(0.8f);
            filter.setCutoffFrequency(500.0f);
            filter.setEnvelopeAmount(0.0f);
            filter.setEnvelopeAttackSeconds(0.005f);
            filter.setEnvelopeDecaySeconds(0.05f);
            filter.reset(); // triggers the envelope's own Attack stage, like a fresh noteOn()

            // Same test signal, run well past the envelope's own lifetime, compared against a
            // second instance that never had reset() (and so never triggered any envelope motion
            // at all) - should filter identically either way if Envelope Amount is truly a no-op.
            const auto testOmega = 2.0f * 3.14159265358979323846f * 100.0f / 44100.0f;
            for (int i = 0; i < 10001; ++i)
                filter.outputColor(std::sin((float) i * testOmega));
            const auto late = filter.outputColor(std::sin(10001.0f * testOmega));

            KarplunkResonantLoopFilter reference;
            reference.prepare(44100.0);
            reference.setResonance(0.8f);
            reference.setCutoffFrequency(500.0f);
            for (int i = 0; i < 10001; ++i)
                reference.outputColor(std::sin((float) i * testOmega));
            const auto referenceLate = reference.outputColor(std::sin(10001.0f * testOmega));

            expectWithinAbsoluteError(late, referenceLate, 1.0e-4f,
                                       "Envelope Amount=0 should leave Cutoff exactly fixed, matching a filter with no envelope motion at all");
        }

        beginTest("Filter envelope: a positive Envelope Amount opens Cutoff right after trigger, then it settles back down");
        {
            // Cutoff well below the test tone, with a strongly positive Envelope Amount - right at
            // the trigger, the envelope should have opened Cutoff enough to let more of the test
            // tone through than once the envelope has fully decayed back to 0.
            KarplunkResonantLoopFilter filter;
            filter.prepare(44100.0);
            filter.setResonance(0.0f);
            filter.setCutoffFrequency(300.0f);
            filter.setEnvelopeAmount(1.0f);
            filter.setEnvelopeAttackSeconds(0.001f);
            filter.setEnvelopeDecaySeconds(0.05f);
            filter.reset();

            const auto testHz = 3000.0f; // well above the base Cutoff, comfortably below a 4-octave-open sweep
            const auto testOmega = 2.0f * 3.14159265358979323846f * testHz / 44100.0f;

            double earlySum = 0.0;
            for (int i = 0; i < 200; ++i)
            {
                const auto y = filter.outputColor(std::sin((float) i * testOmega));
                earlySum += (double) std::abs(y);
            }

            // Run well past the Decay time so the envelope has settled back to 0 (Cutoff back at
            // its base 300Hz), then measure a fresh window.
            for (int i = 200; i < 44100; ++i)
                filter.outputColor(std::sin((float) i * testOmega));

            double lateSum = 0.0;
            for (int i = 44100; i < 44300; ++i)
            {
                const auto y = filter.outputColor(std::sin((float) i * testOmega));
                lateSum += (double) std::abs(y);
            }

            expect(earlySum > lateSum * 2.0,
                   "right after trigger, a positive Envelope Amount should pass more of a high test tone through than once it has decayed back down");
        }

        beginTest("reset() clears both internal filters' history and retriggers the envelope's own Attack stage");
        {
            KarplunkResonantLoopFilter filter;
            filter.prepare(44100.0);
            filter.setDamping(1.0f);
            filter.setResonance(1.0f);
            for (int i = 0; i < 100; ++i)
                filter.outputColor(filter.processSample(1.0f));
            filter.reset();

            const auto loop = filter.processSample(0.0f);
            expectWithinAbsoluteError(loop, 0.0f, 1.0e-6f);
            const auto colored = filter.outputColor(0.0f);
            expectWithinAbsoluteError(colored, 0.0f, 1.0e-6f);
        }
    }
};

static KarplunkResonantLoopFilterTests karplunkResonantLoopFilterTests;
