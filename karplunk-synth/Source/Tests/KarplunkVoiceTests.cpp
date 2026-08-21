#include "../KarplunkExcitation.h"
#include "../KarplunkLoopFilter.h"
#include "../KarplunkVoice.h"

#include <juce_core/juce_core.h>

#include <cmath>

// End-to-end correctness/stability for the composed algorithm - the same style of assertion as
// shields-reverb's ShieldsFDNEngineTests (silence in/out, bounded/decaying output, no runaway
// growth) rather than exact-waveform matching, since a plucked-string's precise sample values
// aren't the point - staying stable and actually decaying is.
class KarplunkVoiceTests : public juce::UnitTest
{
public:
    using Voice = SingleLineKarplunkVoice<NoiseBurstExcitation, TwoPointAverageLoopFilter, LinearInterpolator>;

    KarplunkVoiceTests() : juce::UnitTest("SingleLineKarplunkVoice", "Karplunk") {}

    void runTest() override
    {
        beginTest("An unstruck voice renders silence and reports inactive");
        {
            Voice voice;
            voice.prepare(44100.0);

            expect(!voice.isActive());
            for (int i = 0; i < 1000; ++i)
                expectWithinAbsoluteError(voice.renderNextSample(), 0.0f, 1.0e-9f);
        }

        beginTest("A plucked note produces bounded, non-silent output that eventually decays to silence");
        {
            Voice voice;
            voice.prepare(44100.0);
            voice.setDamping(0.6f);
            voice.noteOn(60, 1.0f); // middle C

            expect(voice.isActive(), "voice should report active immediately after noteOn");

            bool everNonSilent = false;
            float peakAbs = 0.0f;
            constexpr int maxSamples = 44100 * 5; // 5 seconds - generous upper bound on decay time
            int samplesRendered = 0;

            for (; samplesRendered < maxSamples; ++samplesRendered)
            {
                const auto sample = voice.renderNextSample();
                peakAbs = juce::jmax(peakAbs, std::abs(sample));
                if (std::abs(sample) > 0.01f)
                    everNonSilent = true;

                expect(std::abs(sample) <= 1.5f, "output should never blow up far beyond the excitation's own amplitude");

                if (!voice.isActive())
                    break;
            }

            expect(everNonSilent, "a plucked note should produce audible output");
            expect(samplesRendered < maxSamples, "a plucked note should decay to silence within 5 seconds at moderate damping");
        }

        beginTest("Higher damping sustains measurably longer than lower damping, for the same note");
        {
            auto samplesUntilSilent = [](float damping) -> int
            {
                Voice voice;
                voice.prepare(44100.0);
                voice.setDamping(damping);
                voice.noteOn(60, 1.0f);

                int samples = 0;
                constexpr int maxSamples = 44100 * 10;
                while (voice.isActive() && samples < maxSamples)
                {
                    voice.renderNextSample();
                    ++samples;
                }
                return samples;
            };

            const auto shortSustain = samplesUntilSilent(0.1f);
            const auto longSustain = samplesUntilSilent(0.9f);
            expect(longSustain > shortSustain, "higher damping (more loop gain) should sustain longer");
        }

        beginTest("Notes across the supported range all produce bounded, decaying output");
        {
            for (int note : { Voice::kLowestSupportedMidiNote, 60, Voice::kHighestSupportedMidiNote })
            {
                Voice voice;
                voice.prepare(44100.0);
                voice.setDamping(0.6f);
                voice.noteOn(note, 1.0f);

                // 10s, not 5s: decay time is genuinely pitch-dependent at fixed damping (see
                // TwoPointAverageLoopFilter's own comment) - the lowest supported note (A0) takes
                // ~7s to fully cross the silence-hold threshold at this damping setting, confirmed
                // empirically. This isn't a bug to fix, just headroom the test needs to account for.
                constexpr int maxSamples = 44100 * 10;
                int samples = 0;
                for (; samples < maxSamples && voice.isActive(); ++samples)
                {
                    const auto sample = voice.renderNextSample();
                    expect(std::abs(sample) <= 1.5f, "output should stay bounded across the whole supported note range");
                }

                expect(samples < maxSamples, "every supported note should decay to silence within 10 seconds");
            }
        }

        beginTest("Retriggering a note (noteOn while already active) resets cleanly, no NaN/inf");
        {
            Voice voice;
            voice.prepare(44100.0);
            voice.setDamping(0.6f);
            voice.noteOn(60, 1.0f);

            for (int i = 0; i < 500; ++i)
                voice.renderNextSample();

            voice.noteOn(67, 0.8f); // retrigger on a different note before the first decayed

            for (int i = 0; i < 1000; ++i)
            {
                const auto sample = voice.renderNextSample();
                expect(std::isfinite(sample), "output must stay finite through a retrigger");
            }
        }
    }
};

static KarplunkVoiceTests karplunkVoiceTests;
