#include "../KarplunkExcitation.h"
#include "../KarplunkLoopFilter.h"
#include "../KarplunkVoice.h"

#include <juce_core/juce_core.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <vector>

// End-to-end correctness/stability for the composed algorithm - the same style of assertion as
// shields-reverb's ShieldsFDNEngineTests (silence in/out, bounded/decaying output, no runaway
// growth) rather than exact-waveform matching, since a plucked-string's precise sample values
// aren't the point - staying stable and actually decaying is.
class KarplunkVoiceTests : public juce::UnitTest
{
public:
    using Voice = SingleLineKarplunkVoice<NoiseExcitation, TwoPointAverageLoopFilter, LinearInterpolator>;

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

                // Bound raised from 1.5 -> 2.5 with Position's addition: the output now always
                // includes an unconditional second string tap mixed in (see renderNextSample()'s
                // Position comment - there is no bypass value), measured to add up to ~50%
                // peak headroom in the worst case (short-string/high-note territory) - this is
                // expected, bounded behavior, not a regression, following the same precedent Bow
                // set when it first required raising this same bound.
                expect(std::abs(sample) <= 2.5f, "output should never blow up far beyond the excitation's own amplitude");

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
                    // Bound raised 1.5 -> 2.5, same Position-related reason as the single-note
                    // test above - the highest supported note (shortest delay) is measurably the
                    // worst case (up to ~2.2 peak, confirmed empirically), since Position's tap
                    // length there can be under a sample.
                    expect(std::abs(sample) <= 2.5f, "output should stay bounded across the whole supported note range");
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

        beginTest("A held bow note's steady-state loudness is comparable to a plucked note's peak, not ~30dB quieter");
        {
            // This test exists because of a real, shipped, empirically-measured bug: an earlier
            // continuousLevel choice (tuned only by reasoning about the loop's theoretical gain,
            // never by rendering and listening) made a held bow note ~30-40dB quieter than a
            // plucked note - audible as "the pluck just fades out" rather than a sustained bowed
            // tone. Guards against that regressing silently again.
            auto toDb = [](float linear) { return 20.0f * std::log10(std::max(linear, 1.0e-9f)); };

            for (float damping : { 0.1f, 0.6f, 0.9f })
            {
                Voice pluckVoice;
                pluckVoice.prepare(44100.0);
                pluckVoice.setDamping(damping);
                pluckVoice.setBowAmount(0.0f);
                pluckVoice.noteOn(60, 1.0f);
                float pluckPeak = 0.0f;
                for (int i = 0; i < 4410; ++i) // first 100ms
                    pluckPeak = std::max(pluckPeak, std::abs(pluckVoice.renderNextSample()));

                Voice bowVoice;
                bowVoice.prepare(44100.0);
                bowVoice.setDamping(damping);
                bowVoice.setBowAmount(1.0f);
                bowVoice.noteOn(60, 1.0f);
                for (int i = 0; i < 44100 * 2; ++i) // 2s to reach steady state
                    bowVoice.renderNextSample();
                double sumSquares = 0.0;
                constexpr int measureWindow = 4410;
                for (int i = 0; i < measureWindow; ++i)
                {
                    const auto s = bowVoice.renderNextSample();
                    sumSquares += (double) s * (double) s;
                }
                const auto bowRms = (float) std::sqrt(sumSquares / measureWindow);
                const auto deltaDb = toDb(pluckPeak) - toDb(bowRms);

                // Some residual damping-dependence is accepted (documented in README) - the bar
                // here is "not dramatically quieter", not "identical".
                expect(deltaDb < 10.0f, "a held bow note should not be dramatically quieter than a plucked note's peak");
            }
        }

        beginTest("Sustained loudness increases smoothly with bowAmount - no dead zone in the middle of the range");
        {
            // This test exists because of a real, shipped, empirically-measured bug: an earlier
            // envelope design interpolated the attack time linearly in time but the decay time
            // linearly in its own coefficient - two curves that don't move together, so through
            // most of the middle of the range the attack had already gotten slow while the decay
            // hadn't actually gotten any slower yet, producing a loud pluck, a loud full bow, and a
            // dramatic, audible volume drop in between (user report: "from about 2% - 90% of the
            // knob there's a huge volume drop"). The fix - an explicit, directly-interpolated
            // sustainLevel - should make settled loudness a smooth, monotonic function of
            // bowAmount with no such dip. Guards against that regressing silently again.
            constexpr float bowSteps[] = { 0.05f, 0.25f, 0.5f, 0.75f, 1.0f };
            float previousRms = 0.0f;

            for (float bow : bowSteps)
            {
                Voice voice;
                voice.prepare(44100.0);
                voice.setDamping(0.6f);
                voice.setBowAmount(bow);
                voice.noteOn(60, 1.0f);

                // 3s settle - long enough for both the ADSR envelope AND the output loudness
                // leveler's own slow envelope (up to 0.6s time constant - see KarplunkVoice.h) to
                // fully converge before measuring.
                for (int i = 0; i < 44100 * 3; ++i)
                    voice.renderNextSample();

                // A full second, not 100ms - averages out both the resonant loop's inherent
                // noise-driven RMS fluctuation (measured up to ~50% window-to-window even at a
                // fixed bowAmount, tamed but not eliminated by the leveler) and any residual
                // leveling-gain variance, so this test measures the real underlying loudness
                // trend rather than a single noisy sample of it.
                double sumSquares = 0.0;
                constexpr int measureWindow = 44100;
                for (int i = 0; i < measureWindow; ++i)
                {
                    const auto s = voice.renderNextSample();
                    sumSquares += (double) s * (double) s;
                }
                const auto rms = (float) std::sqrt(sumSquares / measureWindow);

                // A small tolerance (not strict increase) - the top of the range is expected to
                // nearly plateau (measured a few-percent-scale hump-then-slight-dip right at the
                // very top at some Decay settings, a real, physically-explained near-flattening,
                // not the dramatic order-of-magnitude dead zone this test guards against).
                expect(rms > previousRms * 0.95f, "settled loudness should increase as bowAmount rises - no dead zone");
                previousRms = rms;
            }
        }

        beginTest("bowAmount = 0 still decays to silence on its own, like a plucked note");
        {
            // Since the unified envelope redesign, bowAmount = 0 is a smooth exponential taper
            // rather than the original design's hard-cutoff rectangular burst (an intentional,
            // user-approved character change - see karplunk-synth/README.md) - so this no longer
            // asserts *exact* equivalence, just that the core plucked-note behaviour (decays on
            // its own, no noteOff() required) still holds.
            Voice voice;
            voice.prepare(44100.0);
            voice.setDamping(0.6f);
            voice.setBowAmount(0.0f);
            voice.noteOn(60, 1.0f);

            constexpr int maxSamples = 44100 * 5;
            int samples = 0;
            for (; samples < maxSamples && voice.isActive(); ++samples)
                voice.renderNextSample();

            expect(samples < maxSamples, "bowAmount = 0 should decay to silence just like the base scaffold, even without ever calling noteOff()");
        }

        beginTest("Abruptly changing bowAmount mid-note keeps output continuous, no discontinuity spike");
        {
            // Guards against the exact failure mode a closed-form/elapsed-sample-counter envelope
            // would have had: bowAmount is live (PluginProcessor updates it every sample), so an
            // abrupt step must not make the envelope itself jump - only its rate of movement
            // should change. The one-pole recurrence in NoiseExcitation is continuous by
            // construction; this test is the empirical check that it actually behaves that way
            // end-to-end, not just in isolation.
            Voice voice;
            voice.prepare(44100.0);
            voice.setDamping(0.6f);
            voice.setBowAmount(0.0f);
            voice.noteOn(60, 1.0f);

            for (int i = 0; i < 2000; ++i)
                voice.renderNextSample();

            auto previous = voice.renderNextSample();
            voice.setBowAmount(1.0f); // abrupt, unsmoothed step - PluginProcessor smooths this in
                                      // practice, but the DSP itself must not rely on that alone

            constexpr int numSamples = 2000;
            for (int i = 0; i < numSamples; ++i)
            {
                const auto sample = voice.renderNextSample();
                expect(std::isfinite(sample), "output must stay finite through an abrupt bowAmount step");
                expect(std::abs(sample - previous) <= 1.0f, "an abrupt bowAmount change should not produce a large sample-to-sample discontinuity");
                previous = sample;
            }
        }

        beginTest("A fully-bowed, held note does not decay to silence on its own");
        {
            Voice voice;
            voice.prepare(44100.0);
            voice.setDamping(0.6f);
            voice.setBowAmount(1.0f);
            voice.noteOn(60, 1.0f);
            // Deliberately never calling noteOff() - the note stays "held" (bowed) throughout.

            constexpr int numSamples = 44100 * 5; // 5 seconds of continuous bowing
            float peakAbs = 0.0f;
            for (int i = 0; i < numSamples; ++i)
            {
                const auto sample = voice.renderNextSample();
                expect(std::isfinite(sample), "held bow injection must stay finite");
                peakAbs = std::max(peakAbs, std::abs(sample));
            }
            // Looser than a pure pluck's bound - a held bow note is a genuinely different,
            // sustained-energy regime, and raw noise driving a resonant loop has real peak-to-RMS
            // crest factor well above its own RMS. Bound raised 3.0 -> 5.5 with Position's
            // addition (measured up to ~4.9 in the worst case: highest note, most-correlated tap) -
            // this just guards against actual runaway (NaN/divergence), not against normal crest
            // factor or Position's own expected contribution.
            expect(peakAbs <= 5.5f, "held bow injection must stay bounded");

            expect(voice.isActive(), "a continuously bowed, held note should still be active after 5 seconds");
        }

        beginTest("Releasing a bowed note (noteOff) lets it decay to silence afterward");
        {
            Voice voice;
            voice.prepare(44100.0);
            voice.setDamping(0.6f);
            voice.setBowAmount(1.0f);
            voice.noteOn(60, 1.0f);

            // Bow for a while first, confirming it doesn't decay on its own.
            for (int i = 0; i < 44100; ++i)
                voice.renderNextSample();
            expect(voice.isActive(), "should still be active while held and bowed");

            voice.noteOff();

            constexpr int maxSamplesAfterRelease = 44100 * 10;
            int samplesAfterRelease = 0;
            for (; samplesAfterRelease < maxSamplesAfterRelease && voice.isActive(); ++samplesAfterRelease)
                voice.renderNextSample();

            expect(samplesAfterRelease < maxSamplesAfterRelease, "a released bow note should decay to silence within 10 seconds, same as a plucked note");
        }

        beginTest("8 simultaneously held, fully-bowed voices at max velocity stay bounded");
        {
            std::array<Voice, 8> voices;
            for (auto& v : voices)
            {
                v.prepare(44100.0);
                v.setDamping(0.9f); // near-max sustain - the highest-risk setting for this stress case
                v.setBowAmount(1.0f);
            }

            const int notes[8] = { 48, 52, 55, 60, 64, 67, 72, 76 }; // an 8-note bowed chord
            for (int i = 0; i < 8; ++i)
                voices[(size_t) i].noteOn(notes[i], 1.0f);

            constexpr int numSamples = 44100 * 3;
            for (int i = 0; i < numSamples; ++i)
            {
                float mixed = 0.0f;
                for (auto& v : voices)
                    mixed += v.renderNextSample();

                expect(std::isfinite(mixed), "summed 8-voice bowed chord must stay finite");
                // Same fixed headroom PluginProcessor applies (1/sqrt(8)) - the point of this test
                // is to confirm the RAW summed signal stays within a sane bound before that
                // headroom is even applied, i.e. that continuous bow injection alone (across 8
                // voices at once) doesn't blow up independently of the processor's own scaling.
                // Per-voice multiplier raised 1.5 -> 2.5, same Position-related reason as the
                // single-voice bow bound above.
                expect(std::abs(mixed) <= 8 * 2.5f, "summed 8-voice bowed chord should stay within a sane bound");
            }
        }

        beginTest("Structure = 0 is bit-identical to today's baseline (no dispersion applied)");
        {
            // structure defaults to 0.0f - this confirms renderNextSample()'s guard
            // (apDelay >= 4.0f && mainDelay >= 4.0f) takes the plain stringLine.read() path at
            // structure=0 for every supported note, not just algebraically via the allpass's own
            // D=0 passthrough identity.
            for (int note : { Voice::kLowestSupportedMidiNote, 60, Voice::kHighestSupportedMidiNote })
            {
                Voice withStructure;
                withStructure.prepare(44100.0);
                withStructure.setDamping(0.6f);
                withStructure.setStructure(0.0f);
                withStructure.noteOn(note, 1.0f);

                Voice withoutStructure;
                withoutStructure.prepare(44100.0);
                withoutStructure.setDamping(0.6f);
                withoutStructure.noteOn(note, 1.0f);

                for (int i = 0; i < 4410; ++i)
                    expectWithinAbsoluteError(withStructure.renderNextSample(), withoutStructure.renderNextSample(), 1.0e-6f);
            }
        }

        beginTest("Abruptly changing structure mid-note keeps output continuous, no discontinuity spike");
        {
            Voice voice;
            voice.prepare(44100.0);
            voice.setDamping(0.6f);
            voice.setStructure(0.0f);
            voice.noteOn(60, 1.0f);

            for (int i = 0; i < 2000; ++i)
                voice.renderNextSample();

            auto previous = voice.renderNextSample();
            voice.setStructure(1.0f); // abrupt, unsmoothed step - PluginProcessor smooths this in
                                      // practice, but the DSP itself must not rely on that alone

            constexpr int numSamples = 2000;
            for (int i = 0; i < numSamples; ++i)
            {
                const auto sample = voice.renderNextSample();
                expect(std::isfinite(sample), "output must stay finite through an abrupt structure step");
                expect(std::abs(sample - previous) <= 1.0f, "an abrupt structure change should not produce a large sample-to-sample discontinuity");
                previous = sample;
            }
        }

        beginTest("Structure = 100% on the highest supported note stays finite and bounded, whichever branch triggers");
        {
            // The dispersion-active/fallback boundary (mainDelay >= 4.0f in renderNextSample())
            // now depends on numStages/maxDispersionGain rather than a fixed constant, so it
            // deserves its own explicit check rather than relying on the general note-range tests
            // to catch it incidentally - the highest supported note (C8, ~10.5-sample delay) at
            // maximum Structure is the closest this design comes to that boundary.
            Voice voice;
            voice.prepare(44100.0);
            voice.setDamping(0.6f);
            voice.setStructure(1.0f);
            voice.noteOn(Voice::kHighestSupportedMidiNote, 1.0f);

            for (int i = 0; i < 44100; ++i)
            {
                const auto sample = voice.renderNextSample();
                expect(std::isfinite(sample), "output must stay finite at the dispersion/fallback boundary");
                expect(std::abs(sample) <= 2.5f, "output should stay within the same bound as the other note-range tests");
            }
        }

        beginTest("Position sweep stays bounded across the full range, no clipping/runaway");
        {
            // NOT a monotonic-loudness assertion (unlike the Bow dead-zone test) - the dip in
            // energy near position ~= 0.5 is the correct, physical harmonic-node cancellation
            // effect (touching a string at its exact midpoint cancels the fundamental and odd
            // harmonics), not a bug. This only guards against actual runaway/instability.
            for (float pos : { 0.0f, 0.1f, 0.25f, 0.5f, 0.75f, 0.9f, 1.0f })
            {
                Voice voice;
                voice.prepare(44100.0);
                voice.setDamping(0.9f);
                voice.setPosition(pos);
                voice.noteOn(Voice::kHighestSupportedMidiNote, 1.0f); // measured worst case

                for (int i = 0; i < 44100; ++i)
                {
                    const auto sample = voice.renderNextSample();
                    expect(std::isfinite(sample), "output must stay finite across the position sweep");
                    expect(std::abs(sample) <= 2.5f, "output should stay within the same bound as the other note-range tests");
                }
            }
        }

        beginTest("A held bow note's loudness stays within a bounded range window-to-window (loudness leveling)");
        {
            // This test exists because of a real, shipped, empirically-measured bug: raw noise
            // driving a high-Q resonant loop naturally produces audible loudness fluctuation
            // ("noise through a narrow filter warbles") - measured as up to ~50% (~3.4dB)
            // window-to-window RMS swings at a FIXED bowAmount, with every parameter held
            // perfectly still (confirmed the fluctuation isn't something turning the Bow knob
            // itself introduces or worsens - a user reported "the volume goes up and down
            // drastically" while turning it, but the root cause is the resonant loop's own
            // energy dynamics). Tamed - not eliminated, since some natural "shimmer" is the
            // correct character for a noise-excited bowed string - by a fast/slow envelope-ratio
            // output leveler in SingleLineKarplunkVoice::renderNextSample(), down to ~1.2dB
            // measured. This guards against that fix regressing silently.
            for (float fixedBow : { 0.5f, 0.9f, 1.0f })
            {
                Voice voice;
                voice.prepare(44100.0);
                voice.setDamping(0.6f);
                voice.setBowAmount(fixedBow);
                voice.noteOn(60, 1.0f);

                for (int i = 0; i < 44100 * 3; ++i) // settle (long enough for the leveler itself to converge)
                    voice.renderNextSample();

                float minWindowRms = std::numeric_limits<float>::max();
                float maxWindowRms = 0.0f;
                constexpr int windowSamples = 2205; // 50ms windows
                for (int w = 0; w < 20; ++w)
                {
                    double sumSquares = 0.0;
                    for (int i = 0; i < windowSamples; ++i)
                    {
                        const auto s = voice.renderNextSample();
                        sumSquares += (double) s * (double) s;
                    }
                    const auto rms = (float) std::sqrt(sumSquares / windowSamples);
                    minWindowRms = std::min(minWindowRms, rms);
                    maxWindowRms = std::max(maxWindowRms, rms);
                }

                // Measured ratio is ~1.15-1.2x (~1.2-1.6dB) with leveling in place, vs. ~1.45-1.5x
                // (~3.2-3.5dB) without it - 1.5x gives comfortable margin while still catching a
                // real regression back toward the unleveled behavior.
                expect(maxWindowRms / minWindowRms < 1.5f, "held bow loudness should not swing drastically window-to-window");
            }
        }

        beginTest("Structure does not measurably detune the fundamental across the note range");
        {
            // This test exists because of a real, shipped, empirically-measured bug: the
            // original Structure implementation (a single large-delay Schroeder allpass, mirroring
            // Mutable Instruments Rings' own string.cc) detuned the fundamental by up to ~95 cents
            // (nearly a semitone), non-monotonically, because a large-delay allpass's group delay
            // oscillates unpredictably with frequency - "it contributes ~apDelay worth of delay"
            // only held by coincidence. Rebuilt as a cascade of small (D=1) allpass stages whose
            // group delay is smooth/monotonic and can be exactly compensated for - measured to
            // detune the fundamental by at most ~1.6 cents ADDITIONAL to whatever pre-existing,
            // Structure-unrelated baseline offset a note already has at Structure=0 (a separate,
            // known, accepted characteristic of the loop filter's own phase response - out of
            // scope here). Guards against the original bug regressing.
            //
            // Autocorrelation-based period estimate: find the lag near the expected delay that
            // maximizes normalized autocorrelation, to sub-sample precision via parabolic
            // interpolation of the correlation peak.
            auto estimatePeriodSamples = [](Voice& voice, float expectedDelaySamples) -> float
            {
                constexpr int windowSize = 4096;
                std::vector<float> buf(windowSize);
                for (auto& s : buf)
                    s = voice.renderNextSample();

                const int searchRadius = 15;
                const int centerLag = (int) std::lround(expectedDelaySamples);
                // Clamp the search range's low end to 1 - a negative or zero lag isn't a
                // meaningful "period" to search over, and would index the buffer out of bounds.
                const int lagLo = std::max(1, centerLag - searchRadius);
                const int lagHi = centerLag + searchRadius;
                int bestLag = centerLag;
                double bestCorr = -1e18;
                for (int lag = lagLo; lag <= lagHi; ++lag)
                {
                    double corr = 0.0;
                    for (int i = 0; i + lag < windowSize; ++i)
                        corr += (double) buf[(size_t) i] * (double) buf[(size_t) (i + lag)];
                    if (corr > bestCorr)
                    {
                        bestCorr = corr;
                        bestLag = lag;
                    }
                }

                auto corrAt = [&](int lag) -> double
                {
                    double corr = 0.0;
                    for (int i = 0; i + lag < windowSize; ++i)
                        corr += (double) buf[(size_t) i] * (double) buf[(size_t) (i + lag)];
                    return corr;
                };
                const auto cMinus = corrAt(bestLag - 1);
                const auto cCenter = corrAt(bestLag);
                const auto cPlus = corrAt(bestLag + 1);
                const auto denom = (cMinus - 2.0 * cCenter + cPlus);
                const auto refinement = std::abs(denom) > 1e-9 ? 0.5 * (cMinus - cPlus) / denom : 0.0;
                return (float) bestLag + (float) refinement;
            };

            // The highest supported note (C8, ~10.5-sample period) is deliberately excluded - its
            // period is too short for this autocorrelation-based estimator to reliably track
            // (confirmed a measurement-tool limitation, not a DSP one: the whole supported note
            // range already passes the general bounded/finite-output tests elsewhere).
            for (int note : { Voice::kLowestSupportedMidiNote, 48, 60, 72 })
            {
                const auto expectedDelay = 44100.0f / (440.0f * std::pow(2.0f, (note - 69) / 12.0f));

                auto measureCentsOff = [&](float structureAmount) -> float
                {
                    Voice voice;
                    voice.prepare(44100.0);
                    voice.setDamping(0.9f);
                    voice.setStructure(structureAmount);
                    voice.noteOn(note, 1.0f);

                    for (int i = 0; i < 4410; ++i) // let it settle past the attack
                        voice.renderNextSample();

                    const auto measuredPeriod = estimatePeriodSamples(voice, expectedDelay);
                    const auto measuredHz = 44100.0f / measuredPeriod;
                    const auto expectedHz = 44100.0f / expectedDelay;
                    return 1200.0f * std::log2(measuredHz / expectedHz);
                };

                // Baseline at Structure=0 - a real, pre-existing, Structure-unrelated offset (the
                // loop filter's own phase response) that this test isn't trying to fix, only to
                // confirm Structure doesn't meaningfully add to.
                const auto baselineCentsOff = measureCentsOff(0.0f);

                for (int step = 1; step <= 20; ++step)
                {
                    const float structureAmount = (float) step / 20.0f;
                    const auto centsOff = measureCentsOff(structureAmount);
                    const auto structureContribution = std::abs(centsOff - baselineCentsOff);

                    if (structureAmount <= 0.75f)
                    {
                        // Measured max ~12.5 cents, only at Structure > 90% on 3 of the 4 tested
                        // notes, at the tuned numStages=8/maxDispersionGain=0.5 (chosen for a
                        // strong, clearly audible stretch on the LOUD low-numbered harmonics - see
                        // KarplunkDispersionFilter's own comment for why this pushes closer to the
                        // edge of what stays in tune than earlier, more conservative tunings did) -
                        // 15 cents gives a small margin above that measured worst case while still
                        // catching a real regression back toward the old design's up-to-95-cent
                        // behavior. Only applies at/below 75%: the allpass-only mechanism this
                        // guards is unaffected by the noise-FM mechanism below (which is exactly 0
                        // in this range), so the same tight bound still holds here.
                        expect(structureContribution < 15.0f, "Structure should not add more than a small amount of detuning on top of the pre-existing baseline (allpass-only range)");
                    }
                    else
                    {
                        // Above 75%, real, DELIBERATE pitch instability is now expected - ported
                        // directly from Mutable Instruments Rings' string.cc, which FMs the delay
                        // length with lowpassed noise in exactly this range (see
                        // renderNextSample()'s own comment). This isn't a bug to bound tightly -
                        // it's the actual audible "unstable/breaking up" character real Rings uses
                        // to make high Structure perceptible, confirmed by rendering and listening
                        // (a pure allpass stretch alone measured as real but was tonally
                        // indistinguishable by ear). Measured up to ~17 cents of average-window
                        // contribution in this range (single long-window average - the real,
                        // moment-to-moment swing is a genuine time-varying wobble, not a static
                        // offset, see the dedicated wobble test below) - 40 cents gives generous
                        // margin above that while still catching a genuinely broken/runaway case
                        // (e.g. a sign error blowing the FM depth up far beyond Rings' own formula).
                        expect(structureContribution < 40.0f, "Structure's noise-FM should stay within a sane range above 75%, not diverge/runaway");
                    }
                }
            }
        }

        beginTest("Position = 50% cancels the 2nd harmonic while preserving the fundamental and 3rd");
        {
            // This test exists because of a real, shipped, empirically-measured bug: the original
            // design summed the position tap directly into `filtered`, which measurably diluted
            // the effect almost to nothing (a periodic signal's phase-shifted read has identical
            // harmonic MAGNITUDES to the original by mathematical necessity - only combining two
            // such reads can create real cancellation, and the first attempt used the wrong sign,
            // cancelling ODD harmonics instead of the physically-correct EVEN ones). Guards
            // against either regressing silently: at Position = 50% (a real string excited/read at
            // its exact midpoint), the 2nd harmonic should be suppressed to near-nothing while the
            // fundamental and 3rd harmonic - which have no node at the midpoint - survive.
            auto goertzelMagnitude = [](const std::vector<float>& buf, float targetFreqBins) -> float
            {
                const auto omega = 2.0f * 3.14159265358979323846f * targetFreqBins / (float) buf.size();
                const auto coeff = 2.0f * std::cos(omega);
                float s0 = 0.0f, s1 = 0.0f, s2 = 0.0f;
                for (auto x : buf)
                {
                    s0 = x + coeff * s1 - s2;
                    s2 = s1;
                    s1 = s0;
                }
                const auto real = s1 - s2 * std::cos(omega);
                const auto imag = s2 * std::sin(omega);
                return std::sqrt(real * real + imag * imag) / (float) buf.size();
            };

            auto measureHarmonic = [&](float positionAmount, int harmonic) -> float
            {
                const auto note = 60;
                const auto expectedDelay = 44100.0f / (440.0f * std::pow(2.0f, (note - 69) / 12.0f));

                Voice voice;
                voice.prepare(44100.0);
                voice.setDamping(0.9f);
                voice.setStructure(0.0f);
                voice.setPosition(positionAmount);
                voice.noteOn(note, 1.0f);

                for (int i = 0; i < 4410; ++i)
                    voice.renderNextSample();

                constexpr int windowSize = 8192;
                std::vector<float> buf(windowSize);
                for (auto& s : buf)
                    s = voice.renderNextSample();

                const auto binsPerHarmonic = (float) windowSize / expectedDelay;
                return goertzelMagnitude(buf, binsPerHarmonic * (float) harmonic);
            };

            const auto h1AtMid = measureHarmonic(0.5f, 1);
            const auto h2AtMid = measureHarmonic(0.5f, 2);
            const auto h3AtMid = measureHarmonic(0.5f, 3);
            const auto h2Elsewhere = measureHarmonic(0.25f, 2);

            expect(h2AtMid < 0.1f * h2Elsewhere, "the 2nd harmonic should be almost entirely cancelled at Position = 50%");
            expect(h1AtMid > 5.0f * h2AtMid, "the fundamental should clearly survive relative to the cancelled 2nd harmonic");
            expect(h3AtMid > 5.0f * h2AtMid, "the 3rd harmonic should clearly survive relative to the cancelled 2nd harmonic (no node there)");
        }

        beginTest("KarplunkDispersionFilter is a true allpass (preserves magnitude) across gains and frequencies");
        {
            // Isolates "is the primitive itself correct" from "does the whole voice stay in
            // tune" (covered end-to-end by the Structure pitch-stability test above, which is
            // the stronger, more direct check that the compensation formula actually works - this
            // test just guards the underlying assumption that this DSP building block still has
            // unity magnitude response, the property the loudness/Bow-compensation math elsewhere
            // in this file relies on it never disturbing).
            auto measureMagnitudeRatio = [](float gain, float omega) -> float
            {
                KarplunkDispersionFilter filter;
                filter.prepare();

                constexpr int totalSamples = 2000;
                constexpr int discard = 200; // let the cascade settle past its own transient
                double inEnergy = 0.0;
                double outEnergy = 0.0;
                for (int n = 0; n < totalSamples; ++n)
                {
                    const auto in = std::sin(omega * (float) n);
                    const auto out = filter.process(in, gain);
                    if (n >= discard)
                    {
                        inEnergy += (double) in * (double) in;
                        outEnergy += (double) out * (double) out;
                    }
                }
                return (float) std::sqrt(outEnergy / inEnergy);
            };

            for (float gain : { 0.1f, 0.2f, 0.3f, 0.4f, 0.5f }) // 0.5 is the current maxDispersionGain
            {
                for (float omega : { 0.02f, 0.05f, 0.1f, 0.3f, 0.5f, 0.8f, 1.2f, 1.5f, 2.0f, 2.5f, 3.0f })
                {
                    const auto ratio = measureMagnitudeRatio(gain, omega);
                    expectWithinAbsoluteError(ratio, 1.0f, 0.02f);
                }
            }
        }

        beginTest("Structure=100% measurably stretches upper partials sharp relative to the fundamental");
        {
            // This test exists because the user found the first (correctly-tuned) version of
            // Structure too subtle - the fundamental stayed in tune, but numStages/
            // maxDispersionGain were tuned conservatively (worst-case note comfortably clear of
            // the fallback threshold), which left barely any DIFFERENCE in group delay across the
            // harmonic series - i.e. almost no real dispersion, just a small uniform shift. Guards
            // against that regressing: confirms upper partials measurably diverge from exact
            // integer multiples of the fundamental (real, audible inharmonicity), not just that
            // the fundamental itself stays in tune (already covered by the test above).
            auto goertzelMagnitude = [](const std::vector<float>& buf, double omega) -> double
            {
                double real = 0.0, imag = 0.0;
                for (size_t n = 0; n < buf.size(); ++n)
                {
                    real += (double) buf[n] * std::cos(omega * (double) n);
                    imag -= (double) buf[n] * std::sin(omega * (double) n);
                }
                return std::sqrt(real * real + imag * imag);
            };

            const auto note = 60;
            const auto f0 = 440.0 * std::pow(2.0, (note - 69) / 12.0);
            const auto sampleRate = 44100.0;

            Voice voice;
            voice.prepare(sampleRate);
            voice.setDamping(0.9f);
            voice.setStructure(1.0f);
            voice.noteOn(note, 1.0f);

            for (int i = 0; i < 44100; ++i)
                voice.renderNextSample();

            constexpr int windowSize = 16384;
            std::vector<float> buf(windowSize);
            for (auto& s : buf)
                s = voice.renderNextSample();

            auto measureCentsStretch = [&](int harmonic) -> double
            {
                const auto expectedFreq = f0 * harmonic;
                const auto expectedOmega = 2.0 * 3.14159265358979323846 * expectedFreq / sampleRate;

                // search +-3% around the expected frequency for the true peak
                double bestOmega = expectedOmega;
                double bestMag = -1.0;
                for (int step = -60; step <= 60; ++step)
                {
                    const auto omega = expectedOmega * (1.0 + (double) step * 0.0005);
                    const auto mag = goertzelMagnitude(buf, omega);
                    if (mag > bestMag)
                    {
                        bestMag = mag;
                        bestOmega = omega;
                    }
                }
                const auto measuredFreq = bestOmega * sampleRate / (2.0 * 3.14159265358979323846);
                return 1200.0 * std::log2(measuredFreq / expectedFreq);
            };

            const auto h1Stretch = measureCentsStretch(1);
            const auto h9Stretch = measureCentsStretch(9);

            // Measured ~6 cents of spread between h1 and h9 at the tuned settings - well above
            // the ~1 cent (essentially flat/no dispersion) the original conservative tuning gave,
            // and far below anything that would suggest instability. This is a floor, not a target
            // - it just guards against the effect collapsing back to "basically nothing" again.
            expect(std::abs(h9Stretch - h1Stretch) > 3.0, "upper partials should measurably diverge from the fundamental at max Structure");
        }

        // This test exists specifically because a single long measurement window (the test above)
        // can mask a slow vibrato by averaging over it - the actual point of the noise-FM
        // mechanism (see renderNextSample()'s comment) is a TIME-VARYING pitch wobble, not a
        // static offset, so that's the property this guards: several consecutive short windows
        // within one held note should show the estimated pitch actually MOVE at Structure=100%,
        // unlike the flat, static baseline at Structure=0 (confirmed by measurement: baseline held
        // at a constant -14.7 cents across all windows, Structure=100% swung between -4.5 and
        // -14.7 cents window-to-window - a real, audible wobble, not a rounding artifact).
        beginTest("Structure=100% produces a genuine time-varying pitch wobble, not just a static offset");
        {
            auto estimateShortPeriod = [](Voice& voice, float expectedDelaySamples, int windowSize) -> float
            {
                std::vector<float> buf((size_t) windowSize);
                for (auto& s : buf)
                    s = voice.renderNextSample();

                const int searchRadius = 15;
                const int centerLag = (int) std::lround(expectedDelaySamples);
                const int lagLo = std::max(1, centerLag - searchRadius);
                const int lagHi = centerLag + searchRadius;
                int bestLag = centerLag;
                double bestCorr = -1e18;
                for (int lag = lagLo; lag <= lagHi; ++lag)
                {
                    double corr = 0.0;
                    for (int i = 0; i + lag < windowSize; ++i)
                        corr += (double) buf[(size_t) i] * (double) buf[(size_t) (i + lag)];
                    if (corr > bestCorr) { bestCorr = corr; bestLag = lag; }
                }
                return (float) bestLag;
            };

            const auto note = 60;
            const auto expectedDelay = 44100.0f / (440.0f * std::pow(2.0f, (note - 69) / 12.0f));

            auto measurePitchRangeCents = [&](float structureAmount) -> float
            {
                Voice voice;
                voice.prepare(44100.0);
                voice.setDamping(0.9f);
                voice.setStructure(structureAmount);
                voice.noteOn(note, 1.0f);

                for (int i = 0; i < 4410; ++i)
                    voice.renderNextSample();

                float minCents = 1e18f, maxCents = -1e18f;
                for (int w = 0; w < 15; ++w)
                {
                    const auto period = estimateShortPeriod(voice, expectedDelay, 2048);
                    const auto hz = 44100.0f / period;
                    const auto expectedHz = 44100.0f / expectedDelay;
                    const auto cents = 1200.0f * std::log2(hz / expectedHz);
                    minCents = std::min(minCents, cents);
                    maxCents = std::max(maxCents, cents);
                }
                return maxCents - minCents;
            };

            const auto baselineRange = measurePitchRangeCents(0.0f);
            const auto structureRange = measurePitchRangeCents(1.0f);

            // Baseline should be essentially flat (the loop filter's own phase response is a
            // static offset, not time-varying); Structure=100% should show meaningfully more
            // movement - a direct guard on the property that actually matters here (does the
            // pitch move over time), not just "is detuning bounded" (already covered above).
            expect(baselineRange < 2.0f, "Structure=0 baseline pitch should be static, not wobbling");
            expect(structureRange > baselineRange + 5.0f, "Structure=100% should produce a measurably larger pitch range than the static baseline");
        }
    }
};

static KarplunkVoiceTests karplunkVoiceTests;
