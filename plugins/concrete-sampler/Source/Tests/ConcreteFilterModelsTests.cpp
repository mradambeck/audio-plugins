#include "../ConcreteFilterModels.h"

#include <juce_core/juce_core.h>

#include <cmath>
#include <vector>

// Structural/behavioral correctness of each filter mode - does bypass genuinely pass through
// unchanged, does the ladder actually attenuate above cutoff, does resonance genuinely add a peak/
// self-oscillation, does CEM-compensated actually hold passband level better than CEM-loss as
// resonance rises, does the one-pole ignore resonance entirely. The precise frequency-response
// slope (dB/octave), THD character, and per-voice independence claims are verified empirically
// through the real ConcreteRenderIR tool and analysis/verify_phase5.py instead, matching this
// catalog's convention (see concrete-sampler-plugin-plan.md's Ground rules).
namespace
{
    std::vector<float> makeSine(double freqHz, double sampleRate, int numSamples, float amplitude = 0.5f)
    {
        std::vector<float> data((size_t) numSamples);
        for (int i = 0; i < numSamples; ++i)
            data[(size_t) i] = amplitude * (float) std::sin(2.0 * juce::MathConstants<double>::pi * freqHz * (double) i / sampleRate);
        return data;
    }

    float rms(const std::vector<float>& signal, int startIndex)
    {
        double sum = 0.0;
        int count = 0;
        for (int i = startIndex; i < (int) signal.size(); ++i)
        {
            sum += (double) signal[(size_t) i] * (double) signal[(size_t) i];
            ++count;
        }
        return (float) std::sqrt(sum / juce::jmax(1, count));
    }
}

class ConcreteFilterModelsTests : public juce::UnitTest
{
public:
    ConcreteFilterModelsTests() : juce::UnitTest("ConcreteFilterModels", "Concrete") {}

    void runTest() override
    {
        constexpr double sampleRate = 44100.0;

        beginTest("Bypass passes every sample through completely unchanged");
        {
            ConcreteFilterModel filter;
            filter.prepare(sampleRate);
            for (float sample : { -0.9f, -0.1f, 0.0f, 0.3f, 0.87f })
                expectEquals(filter.processSample(ConcreteFilterModel::Mode::bypass, sample, 500.0f, 0.8f), sample);
        }

        beginTest("Each ladder mode measurably attenuates a tone well above cutoff (a real lowpass)");
        {
            for (auto mode : { ConcreteFilterModel::Mode::ssm, ConcreteFilterModel::Mode::cemResonanceLoss,
                                ConcreteFilterModel::Mode::cemResonanceCompensated })
            {
                ConcreteFilterModel filter;
                filter.prepare(sampleRate);
                const auto tone = makeSine(8000.0, sampleRate, 4410);
                std::vector<float> out(tone.size());
                for (size_t i = 0; i < tone.size(); ++i)
                    out[i] = filter.processSample(mode, tone[i], 500.0f, 0.0f);

                const auto inputLevel = rms(tone, 2000);
                const auto outputLevel = rms(out, 2000);
                expect(outputLevel < inputLevel * 0.5f,
                       "an 8kHz tone through a 500Hz lowpass must be substantially attenuated");
            }
        }

        beginTest("Each ladder mode passes a tone well below cutoff through close to unattenuated "
                  "(at zero resonance)");
        {
            for (auto mode : { ConcreteFilterModel::Mode::ssm, ConcreteFilterModel::Mode::cemResonanceLoss,
                                ConcreteFilterModel::Mode::cemResonanceCompensated })
            {
                ConcreteFilterModel filter;
                filter.prepare(sampleRate);
                const auto tone = makeSine(200.0, sampleRate, 4410);
                std::vector<float> out(tone.size());
                for (size_t i = 0; i < tone.size(); ++i)
                    out[i] = filter.processSample(mode, tone[i], 5000.0f, 0.0f);

                const auto inputLevel = rms(tone, 2000);
                const auto outputLevel = rms(out, 2000);
                expect(outputLevel > inputLevel * 0.8f,
                       "a 200Hz tone through a 5kHz lowpass at zero resonance should pass through mostly intact");
            }
        }

        beginTest("The ladder self-oscillates (sustains, rather than decaying, after a single nudge) at high resonance");
        {
            // A perfectly noise-free digital simulation starting from EXACTLY zero state and zero
            // input mathematically stays at exactly zero forever, no matter how high the feedback
            // gain - real analog self-oscillation is bootstrapped by circuit noise, which this
            // model doesn't have. A single-sample impulse stands in for that bootstrap; what's
            // under test is whether the resonant feedback loop SUSTAINS afterward rather than
            // decaying to silence like an ordinary (non-self-oscillating) resonant peak would.
            ConcreteFilterModel filter;
            filter.prepare(sampleRate);
            std::vector<float> impulseThenZeros(4410, 0.0f);
            impulseThenZeros[0] = 1.0f;
            std::vector<float> out(impulseThenZeros.size());
            for (size_t i = 0; i < impulseThenZeros.size(); ++i)
                out[i] = filter.processSample(ConcreteFilterModel::Mode::ssm, impulseThenZeros[i], 1000.0f, 1.0f);

            const auto tailLevel = rms(out, 2000);
            expect(tailLevel > 0.01f, "a resonance of 1.0, nudged once, should sustain into ongoing "
                                       "self-oscillation rather than decaying to silence");
        }

        beginTest("CEM-compensated holds passband level closer to unity as resonance rises than CEM-loss does");
        {
            // The defining difference between the two CEM variants (Fairlight/Linn's 3320 vs the
            // Mirage's compensated 3328) - see the plan's Phase 5 Analysis.
            const auto tone = makeSine(200.0, sampleRate, 4410);

            auto measureAtResonance = [&](ConcreteFilterModel::Mode mode, float resonance)
            {
                ConcreteFilterModel filter;
                filter.prepare(sampleRate);
                std::vector<float> out(tone.size());
                for (size_t i = 0; i < tone.size(); ++i)
                    out[i] = filter.processSample(mode, tone[i], 5000.0f, resonance);
                return rms(out, 2000);
            };

            const auto lossLow = measureAtResonance(ConcreteFilterModel::Mode::cemResonanceLoss, 0.0f);
            const auto lossHigh = measureAtResonance(ConcreteFilterModel::Mode::cemResonanceLoss, 0.9f);
            const auto compLow = measureAtResonance(ConcreteFilterModel::Mode::cemResonanceCompensated, 0.0f);
            const auto compHigh = measureAtResonance(ConcreteFilterModel::Mode::cemResonanceCompensated, 0.9f);

            const auto lossRatio = lossHigh / juce::jmax(1.0e-6f, lossLow);
            const auto compRatio = compHigh / juce::jmax(1.0e-6f, compLow);
            expect(std::abs(compRatio - 1.0f) < std::abs(lossRatio - 1.0f),
                   "compensated passband level must move less across the resonance sweep than uncompensated");
        }

        beginTest("The digital+VCA model filters distinctly from the ladder modes (a genuinely different topology)");
        {
            // A tone well ABOVE cutoff through both at moderate resonance mostly just measures
            // "both attenuate it a lot," which can coincidentally read as near-identical even
            // though the topologies differ - the two designs' RESONANT characters (what actually
            // differs) show up clearest with the tone driven right AT cutoff, where the ladder's
            // stronger, more ladder-typical resonant boost should diverge from the cleaner SVF's.
            const auto tone = makeSine(1000.0, sampleRate, 4410);

            ConcreteFilterModel digitalFilter;
            digitalFilter.prepare(sampleRate);
            ConcreteFilterModel ladderFilter;
            ladderFilter.prepare(sampleRate);

            std::vector<float> digitalOut(tone.size()), ladderOut(tone.size());
            for (size_t i = 0; i < tone.size(); ++i)
            {
                digitalOut[i] = digitalFilter.processSample(ConcreteFilterModel::Mode::digitalVca, tone[i], 1000.0f, 0.85f);
                ladderOut[i] = ladderFilter.processSample(ConcreteFilterModel::Mode::cemResonanceLoss, tone[i], 1000.0f, 0.85f);
            }

            const auto digitalLevel = rms(digitalOut, 2000);
            const auto ladderLevel = rms(ladderOut, 2000);
            expect(std::abs(digitalLevel - ladderLevel) > digitalLevel * 0.1f,
                   "digital+VCA's resonant peak at cutoff must differ measurably from the ladder's");
        }

        beginTest("One-pole ignores resonance entirely (mathematically cannot peak/self-oscillate)");
        {
            const auto tone = makeSine(200.0, sampleRate, 4410);

            auto measureAtResonance = [&](float resonance)
            {
                ConcreteFilterModel filter;
                filter.prepare(sampleRate);
                std::vector<float> out(tone.size());
                for (size_t i = 0; i < tone.size(); ++i)
                    out[i] = filter.processSample(ConcreteFilterModel::Mode::onePole, tone[i], 2000.0f, resonance);
                return out;
            };

            const auto atZero = measureAtResonance(0.0f);
            const auto atMax = measureAtResonance(1.0f);
            for (size_t i = 0; i < atZero.size(); ++i)
                expectWithinAbsoluteError(atZero[i], atMax[i], 1.0e-6f,
                                           "one-pole output must be identical regardless of the resonance parameter");
        }

        beginTest("Output stays finite and bounded across every mode at extreme settings");
        {
            const auto tone = makeSine(1000.0, sampleRate, 4410, 0.99f);
            for (auto mode : { ConcreteFilterModel::Mode::bypass, ConcreteFilterModel::Mode::ssm,
                                ConcreteFilterModel::Mode::cemResonanceLoss, ConcreteFilterModel::Mode::cemResonanceCompensated,
                                ConcreteFilterModel::Mode::digitalVca, ConcreteFilterModel::Mode::onePole })
            {
                for (float cutoff : { 20.0f, 1000.0f, 19000.0f })
                {
                    for (float resonance : { 0.0f, 0.5f, 1.0f })
                    {
                        ConcreteFilterModel filter;
                        filter.prepare(sampleRate);
                        for (float sample : tone)
                        {
                            const auto out = filter.processSample(mode, sample, cutoff, resonance);
                            expect(std::isfinite(out), "output must never be NaN/inf");
                            // Generous, not tight - a genuinely self-oscillating resonant filter at
                            // max resonance is SUPPOSED to get loud (the ladder's own feedback tap
                            // is tanh-bounded to (-1,1), so `fed` is bounded by roughly
                            // inputAmplitude + feedbackGain <= ~1 + 4.2 =~ 5.2; cemResonanceCompensated
                            // additionally applies up to a ~4.6x makeup gain on top of that at
                            // resonance=1, so ~24 is the real theoretical ceiling this bound needs
                            // to clear, not an arbitrary "small" number - see processLadder()'s own
                            // comment on why the feedback tap being bounded is what actually
                            // prevents unbounded (NaN/inf) growth, which is what this test's OTHER
                            // assertion above is really checking).
                            expect(std::abs(out) <= 30.0f, "output must stay within a reasonable bound");
                        }
                    }
                }
            }
        }
    }
};

static ConcreteFilterModelsTests concreteFilterModelsTests;
