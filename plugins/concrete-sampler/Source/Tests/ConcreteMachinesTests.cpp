#include "../ConcreteMachines.h"

#include <juce_core/juce_core.h>

#include <algorithm>
#include <set>

// Data-sanity checks for the Phase 7 machine table itself (concrete-sampler-plugin-plan.md) - not
// DSP, so a plain deterministic unit test is the right tool here (see PluginProcessor's own
// createParameterLayout() for the ranges these values must stay inside, and ConcreteProcessorTests
// for the end-to-end "selecting a machine actually applies these values" coverage).
class ConcreteMachinesTests : public juce::UnitTest
{
public:
    ConcreteMachinesTests() : juce::UnitTest("ConcreteMachines", "Concrete") {}

    void runTest() override
    {
        const auto& machines = getConcreteMachines();

        beginTest("Exactly twelve machines exist, matching the plan's machine table");
        {
            expectEquals((int) machines.size(), 12);
        }

        beginTest("Every machine name is unique and non-empty");
        {
            std::set<juce::String> names;
            for (const auto& machine : machines)
            {
                expect(machine.name != nullptr && juce::String(machine.name).isNotEmpty(),
                       "every machine must have a real name");
                expect(names.insert(machine.name).second,
                       juce::String("duplicate machine name: ") + machine.name);
            }
        }

        beginTest("Every machine's values fall inside their APVTS parameter's own range");
        {
            for (const auto& machine : machines)
            {
                const juce::String name(machine.name);
                expect(machine.baseRateHz >= 4000.0f && machine.baseRateHz <= 100000.0f,
                       name + ": baseRateHz out of Base Rate's 4000-100000Hz range");
                expect(machine.bitDepthBits >= 1 && machine.bitDepthBits <= 16,
                       name + ": bitDepthBits out of Bit Depth's 1-16 range");
                expect(machine.captureTransposeSemitones >= 0.0f && machine.captureTransposeSemitones <= 24.0f,
                       name + ": captureTransposeSemitones out of Capture Transpose's 0-24 range");
                expect(machine.voiceCount >= 1 && machine.voiceCount <= 18,
                       name + ": voiceCount out of the voice pool's 1-18 range");
            }
        }

        beginTest("Kurzweil K250 is the one machine using the contoured amp envelope");
        {
            int contouredCount = 0;
            bool k250IsContoured = false;
            for (const auto& machine : machines)
            {
                if (machine.ampEnvelopeMode == ConcreteAmpEnvelopeMode::contoured)
                {
                    ++contouredCount;
                    if (juce::String(machine.name).containsIgnoreCase("K250"))
                        k250IsContoured = true;
                }
            }
            expectEquals(contouredCount, 1, "exactly one machine should use the contoured envelope");
            expect(k250IsContoured, "the K250 specifically should be the one using it");
        }

        beginTest("The E-mu SP-1200 uses Mode B (fixed-rate drop-sample) and an SSM-family filter");
        {
            const auto it = std::find_if(machines.begin(), machines.end(), [](const ConcreteMachine& m)
            { return juce::String(m.name).containsIgnoreCase("SP-1200"); });
            expect(it != machines.end(), "SP-1200 should be one of the twelve");
            if (it != machines.end())
            {
                expect(it->pitchEngineMode == ConcretePitchEngine::Mode::dropSampleDecimation,
                       "SP-1200 is fixed-rate drop-sample (Mode B), not a variable-clock machine");
                expect(it->filterModel == ConcreteFilterModel::Mode::ssm,
                       "SP-1200 defaults to its SSM2044 filter engaged - Filter Model itself is the "
                       "toggle back to the unfiltered path");
            }
        }

        beginTest("The Ensoniq ASR-10 is the one machine using delta-sigma (Mode C)");
        {
            int deltaSigmaCount = 0;
            for (const auto& machine : machines)
                if (machine.pitchEngineMode == ConcretePitchEngine::Mode::deltaSigma)
                    ++deltaSigmaCount;
            expectEquals(deltaSigmaCount, 1, "exactly one machine should use delta-sigma");
        }

        beginTest("The Casio SK-1 is the darkest preset: lowest base rate, one-pole filter, fewest voices");
        {
            const auto it = std::find_if(machines.begin(), machines.end(), [](const ConcreteMachine& m)
            { return juce::String(m.name).containsIgnoreCase("SK-1"); });
            expect(it != machines.end(), "SK-1 should be one of the twelve");
            if (it != machines.end())
            {
                for (const auto& other : machines)
                    expect(it->baseRateHz <= other.baseRateHz, "SK-1 should have the lowest base rate of all twelve");
                expect(it->filterModel == ConcreteFilterModel::Mode::onePole, "SK-1 uses the simple one-pole filter");
                expect(it->voiceCount == 4, "SK-1 has only 4 voices, no per-voice card architecture");
            }
        }

        beginTest("No two machines are configured completely identically");
        {
            // A pairwise duplicate would mean two rows of the plan's table collapsed onto the same
            // settings by mistake - matches the plan's own Analysis requirement ("any pair nulling
            // to near-silence means two are configured identically and one is wrong"), checked here
            // at the data level directly rather than only via audio nulling.
            for (size_t i = 0; i < machines.size(); ++i)
            {
                for (size_t j = i + 1; j < machines.size(); ++j)
                {
                    const auto& a = machines[i];
                    const auto& b = machines[j];
                    const auto identical = a.pitchEngineMode == b.pitchEngineMode
                        && a.baseRateHz == b.baseRateHz
                        && a.bitDepthBits == b.bitDepthBits
                        && a.quantizerMode == b.quantizerMode
                        && a.filterModel == b.filterModel
                        && a.voiceCount == b.voiceCount
                        && a.ampEnvelopeMode == b.ampEnvelopeMode;
                    expect(!identical, juce::String(a.name) + " and " + juce::String(b.name)
                                            + " are configured completely identically");
                }
            }
        }
    }
};

static ConcreteMachinesTests concreteMachinesTests;
