#include "../ConcreteQuantizer.h"

#include <juce_core/juce_core.h>

#include <cmath>
#include <set>

// Structural/behavioral correctness only - does linear quantization actually produce a bounded,
// finite set of discrete levels, is it deterministic (no dither, ever - see the plan's Phase 3),
// does companding genuinely favor small signals over a linear reduction at the same bit depth.
// The empirical spectral claims (noise floor scaling ~6dB/bit, companded-vs-linear noise at full
// scale vs -30dB, harmonic vs white quantization noise) are verified through the real
// ConcreteRenderIR tool and analysis/verify_phase3.py instead, matching this catalog's convention
// (see concrete-sampler-plugin-plan.md's Ground rules and ConcretePitchEngineTests.cpp's own
// comment on the same split).
class ConcreteQuantizerTests : public juce::UnitTest
{
public:
    ConcreteQuantizerTests() : juce::UnitTest("ConcreteQuantizer", "Concrete") {}

    void runTest() override
    {
        beginTest("Linear quantization is deterministic - no dither, ever");
        {
            for (float sample : { -0.73f, -0.01f, 0.0f, 0.37f, 0.9999f })
            {
                const auto a = ConcreteQuantizer::process(sample, ConcreteQuantizer::Mode::linear, 8);
                const auto b = ConcreteQuantizer::process(sample, ConcreteQuantizer::Mode::linear, 8);
                expectEquals(a, b, "identical input must always produce identical output");
            }
        }

        beginTest("Linear quantization at N bits produces at most 2^(N-1)*2+1 distinct output levels");
        {
            constexpr int bits = 4; // deliberately coarse so a dense sweep can actually hit every level
            std::set<float> distinctValues;
            for (int i = 0; i <= 4000; ++i)
            {
                const auto sample = -1.0f + 2.0f * (float) i / 4000.0f;
                distinctValues.insert(ConcreteQuantizer::process(sample, ConcreteQuantizer::Mode::linear, bits));
            }
            const auto maxExpectedLevels = (size_t) (1 << (bits - 1)) * 2 + 1;
            expect(distinctValues.size() <= maxExpectedLevels,
                   "a dense sweep must not produce more distinct levels than the bit depth allows");
            expect(distinctValues.size() > 1, "a dense sweep across the full range must hit more than one level");
        }

        beginTest("Linear quantization at 16 bits is a close approximation of the input");
        {
            // 16-bit's step size is small enough that this is effectively "transparent" - matches
            // the plugin's own 16-bit default rationale (PluginProcessor.cpp's createParameterLayout()).
            const auto levels = (float) (1 << 15);
            const auto maxError = 1.0f / levels;
            for (float sample : { -0.9f, -0.42f, 0.13f, 0.876f })
            {
                const auto quantized = ConcreteQuantizer::process(sample, ConcreteQuantizer::Mode::linear, 16);
                expectWithinAbsoluteError(quantized, sample, maxError);
            }
        }

        beginTest("Linear quantization output is always within [-1, 1] even for out-of-range input");
        {
            for (float sample : { -3.0f, 3.0f })
            {
                const auto quantized = ConcreteQuantizer::process(sample, ConcreteQuantizer::Mode::linear, 8);
                expect(quantized >= -1.0f && quantized <= 1.0f, "output must be clamped to the valid range");
            }
        }

        beginTest("Linear quantization is monotonically non-decreasing");
        {
            float previous = -2.0f;
            for (int i = 0; i <= 200; ++i)
            {
                const auto sample = -1.0f + 2.0f * (float) i / 200.0f;
                const auto quantized = ConcreteQuantizer::process(sample, ConcreteQuantizer::Mode::linear, 8);
                expect(quantized >= previous, "increasing input must never produce a decreasing output");
                previous = quantized;
            }
        }

        beginTest("Companded quantization stays within [-1, 1], is deterministic, and preserves sign");
        {
            for (float sample : { -0.95f, -0.1f, 0.0f, 0.1f, 0.95f })
            {
                const auto a = ConcreteQuantizer::process(sample, ConcreteQuantizer::Mode::companded, 8);
                const auto b = ConcreteQuantizer::process(sample, ConcreteQuantizer::Mode::companded, 8);
                expectEquals(a, b, "identical input must always produce identical output");
                expect(a >= -1.0f && a <= 1.0f, "output must be within the valid range");
                if (sample > 0.05f)
                    expect(a > 0.0f, "a clearly positive input must not flip sign under companding");
                else if (sample < -0.05f)
                    expect(a < 0.0f, "a clearly negative input must not flip sign under companding");
            }
        }

        beginTest("Companded quantization is monotonically non-decreasing");
        {
            float previous = -2.0f;
            for (int i = 0; i <= 200; ++i)
            {
                const auto sample = -1.0f + 2.0f * (float) i / 200.0f;
                const auto quantized = ConcreteQuantizer::process(sample, ConcreteQuantizer::Mode::companded, 8);
                expect(quantized >= previous, "increasing input must never produce a decreasing output");
                previous = quantized;
            }
        }

        beginTest("Companding reduces quantization error near zero vs. linear at the same bit depth");
        {
            // This is the entire reason E-mu companded their storage (see the plan's Phase 3) -
            // a structural sanity check backing the dB-based empirical measurement in
            // analysis/verify_phase3.py.
            constexpr int bits = 8;
            for (float quiet : { 0.01f, -0.02f, 0.03f })
            {
                const auto linearError = std::abs(ConcreteQuantizer::process(quiet, ConcreteQuantizer::Mode::linear, bits) - quiet);
                const auto compandedError = std::abs(ConcreteQuantizer::process(quiet, ConcreteQuantizer::Mode::companded, bits) - quiet);
                expect(compandedError < linearError,
                       "companding must give a quiet signal more resolution than a linear reduction");
            }
        }

        beginTest("Companding and linear give comparably-sized error at full scale");
        {
            // The two schemes should NOT differ wildly right at the top of the range - companding's
            // advantage is specifically for quiet signals, not everywhere (see the plan's Phase 3
            // analysis: "if the two measure the same at -30dB, the companding is doing nothing" -
            // the converse also matters: they should roughly agree at full scale).
            constexpr int bits = 8;
            constexpr float loud = 0.95f;
            const auto linearError = std::abs(ConcreteQuantizer::process(loud, ConcreteQuantizer::Mode::linear, bits) - loud);
            const auto compandedError = std::abs(ConcreteQuantizer::process(loud, ConcreteQuantizer::Mode::companded, bits) - loud);
            expect(compandedError < linearError * 4.0f + 0.02f,
                   "companded error at full scale should be the same order of magnitude as linear, not wildly worse");
        }
    }
};

static ConcreteQuantizerTests concreteQuantizerTests;
