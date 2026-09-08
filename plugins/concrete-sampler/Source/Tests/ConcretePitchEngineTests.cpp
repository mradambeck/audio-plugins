#include "../ConcretePitchEngine.h"

#include <juce_core/juce_core.h>

#include <cmath>
#include <vector>

// Structural/behavioral correctness of each mode - does Mode A genuinely do nearest-neighbor
// rather than blend samples, does Mode B genuinely tick at a fixed rate independent of pitch, is
// Mode C's output bounded and DC-accurate. The actual spectral claims (imaging frequency, in-band
// vs out-of-band noise floor, host-sample-rate independence) are verified empirically through the
// real ConcreteRenderIR tool and analysis/verify_phase2.py instead - FFT analysis has no natural
// home in a C++ UnitTest here, and this catalog's convention is Python for that (see
// concrete-sampler-plugin-plan.md's Ground rules).
namespace
{
    std::vector<float> makeSine(double freqHz, double sampleRate, int numSamples)
    {
        std::vector<float> data((size_t) numSamples);
        for (int i = 0; i < numSamples; ++i)
            data[(size_t) i] = (float) std::sin(2.0 * juce::MathConstants<double>::pi * freqHz * (double) i / sampleRate);
        return data;
    }
}

class ConcretePitchEngineTests : public juce::UnitTest
{
public:
    ConcretePitchEngineTests() : juce::UnitTest("ConcretePitchEngine", "Concrete") {}

    void runTest() override
    {
        beginTest("Reference mode at unity ratio is a bit-exact passthrough (Phase 1's own behavior)");
        {
            const auto sine = makeSine(1000.0, 44100.0, 4410);
            ConcretePitchEngine engine;
            engine.prepare(44100.0);
            engine.start(0.0);

            for (int i = 0; i < 2000; ++i)
            {
                const auto out = engine.processSample(ConcretePitchEngine::Mode::reference,
                                                        sine.data(), (int) sine.size(), 1.0, 44100.0, 44100.0);
                expectWithinAbsoluteError(out, sine[(size_t) i], 1.0e-6f);
            }
        }

        beginTest("Mode A produces exact source sample values, never a blended/interpolated one");
        {
            // A ramp makes any interpolation blend trivially distinguishable from a real sample.
            std::vector<float> ramp(1000);
            for (size_t i = 0; i < ramp.size(); ++i)
                ramp[i] = (float) i;

            ConcretePitchEngine engine;
            engine.prepare(44100.0);
            engine.start(0.0);

            // A non-integer pitch ratio guarantees most reads land on a fractional source
            // position - if Mode A were secretly interpolating, these values would not be exact
            // integers matching one of ramp's own entries.
            const auto pitchRatio = std::pow(2.0, 3.7 / 12.0);
            for (int i = 0; i < 500; ++i)
            {
                const auto out = engine.processSample(ConcretePitchEngine::Mode::variableClockZeroOrderHold,
                                                        ramp.data(), (int) ramp.size(), pitchRatio, 44100.0, 44100.0);
                const auto rounded = std::round(out);
                expectWithinAbsoluteError(out, rounded, 1.0e-6f, "Mode A must read an exact sample, not an interpolated blend");
            }
        }

        beginTest("Mode A's hold-tick rate scales with baseRate*pitchRatio (fileRate matched to baseRate here, "
                  "isolating this from the fileRate/baseRate compensation - see the dedicated regression test below)");
        {
            // Mode A no longer advances sourcePhase by a fixed fraction every single sample (that
            // was the bug - see readModeA()'s own comment) - it now advances in discrete jumps,
            // only when a hold-tick fires. Over enough samples the total ticks fired converges on
            // baseRate*pitchRatio/hostRate per sample, but any single short window can be off by
            // up to one tick's worth of rounding - a large sample count keeps that negligible.
            std::vector<float> data(10000, 0.0f);
            ConcretePitchEngine engine;
            engine.prepare(48000.0);
            engine.start(0.0);

            const auto pitchRatio = 1.5;
            const auto baseRateHz = 30000.0;
            constexpr int numSamples = 100000;
            const auto expectedTickCount = (baseRateHz * pitchRatio / 48000.0) * numSamples;

            for (int i = 0; i < numSamples; ++i)
                engine.processSample(ConcretePitchEngine::Mode::variableClockZeroOrderHold, data.data(), (int) data.size(),
                                       pitchRatio, baseRateHz, baseRateHz);

            // fileRateHz == baseRateHz here, so each tick advances sourcePhase by exactly 1 -
            // sourcePhase itself IS the tick count.
            expectWithinAbsoluteError(engine.getSourcePhase(), expectedTickCount, 2.0,
                                       "total ticks over many samples should match baseRate*pitchRatio/hostRate, within rounding");
        }

        beginTest("Regression: Mode A's root-pitch playback speed tracks the file's real rate, not baseRateHz "
                  "(real reported bug: switching machines changed a loaded sample's pitch/tempo, not just its "
                  "character - a 1kHz tone measured as ~212Hz through the Casio SK-1 preset, baseRate 9.38kHz)");
        {
            std::vector<float> data(200000, 0.0f);
            constexpr double hostRate = 44100.0;
            constexpr double fileRateHz = 44100.0; // the loaded file's own real rate
            constexpr int numSamples = 100000;

            auto totalSourceAdvance = [&](double baseRateHz)
            {
                ConcretePitchEngine engine;
                engine.prepare(hostRate);
                engine.start(0.0);
                for (int i = 0; i < numSamples; ++i)
                    engine.processSample(ConcretePitchEngine::Mode::variableClockZeroOrderHold, data.data(), (int) data.size(),
                                           1.0, baseRateHz, fileRateHz); // pitchRatio 1.0 - an untransposed, root note
                return engine.getSourcePhase();
            };

            // At an untransposed root note, the total distance traveled through the source over N
            // samples must match the file's own real rate (fileRateHz/hostRate per sample)
            // REGARDLESS of baseRateHz - three wildly different machine base rates must all reach
            // essentially the same source position, since none of them transposed the note.
            const auto expected = (fileRateHz / hostRate) * numSamples;
            expectWithinAbsoluteError(totalSourceAdvance(9380.0), expected, 10.0,
                                       "the Casio SK-1's 9.38kHz base rate must not change root-pitch playback speed");
            expectWithinAbsoluteError(totalSourceAdvance(44100.0), expected, 10.0,
                                       "a matched base rate is the existing, already-correct baseline");
            expectWithinAbsoluteError(totalSourceAdvance(50000.0), expected, 10.0,
                                       "the Synclavier's 50kHz base rate must not change root-pitch playback speed");
        }

        beginTest("Mode B ticks at a fixed rate: the held value only changes every N host samples");
        {
            // baseRate = hostRate/4 -> a new tick (and therefore a possible value change) should
            // occur only once every 4 host samples, regardless of pitch.
            std::vector<float> ramp(1000);
            for (size_t i = 0; i < ramp.size(); ++i)
                ramp[i] = (float) i;

            ConcretePitchEngine engine;
            engine.prepare(44100.0);
            engine.start(0.0);

            const auto baseRateHz = 44100.0 / 4.0;
            int changeCount = 0;
            float previous = engine.processSample(ConcretePitchEngine::Mode::dropSampleDecimation, ramp.data(), (int) ramp.size(), 1.0, baseRateHz, baseRateHz);
            for (int i = 1; i < 40; ++i)
            {
                const auto current = engine.processSample(ConcretePitchEngine::Mode::dropSampleDecimation, ramp.data(), (int) ramp.size(), 1.0, baseRateHz, baseRateHz);
                if (current != previous)
                    ++changeCount;
                previous = current;
            }
            // 40 samples at one tick per 4 samples -> up to 10 ticks -> at most ~10 changes,
            // definitely not one every sample (39 possible).
            expect(changeCount <= 12, "the held value should change roughly once every 4 samples, not every sample");
            expect(changeCount >= 6, "ticks should still be occurring at roughly the expected rate");
        }

        beginTest("Mode B's tick rate is independent of pitch (only the source stepping changes)");
        {
            // NOT tested via counting observable value changes: at pitchRatio=0.5, sourcePhase
            // advances by only 0.5 per tick, so floor(sourcePhase) - and therefore the held value
            // - only actually changes on every OTHER tick (that's "repeating samples" to pitch
            // down, working as intended) - so a value-change count conflates tick rate with
            // pitch ratio instead of isolating it. The real, pitch-independent invariant is the
            // number of TICKS itself, which sourcePhase/pitchRatio recovers directly (each tick
            // advances sourcePhase by exactly pitchRatio, from a start position of 0.0).
            std::vector<float> data(2000, 0.0f);
            const auto baseRateHz = 44100.0 / 4.0;

            auto tickCountAfter400Samples = [&](double pitchRatio)
            {
                ConcretePitchEngine engine;
                engine.prepare(44100.0);
                engine.start(0.0);
                for (int i = 0; i < 400; ++i)
                    engine.processSample(ConcretePitchEngine::Mode::dropSampleDecimation, data.data(), (int) data.size(), pitchRatio, baseRateHz, baseRateHz);
                return engine.getSourcePhase() / pitchRatio;
            };

            const auto ticksAtUnity = tickCountAfter400Samples(1.0);
            const auto ticksUpAnOctave = tickCountAfter400Samples(2.0);
            const auto ticksDownAnOctave = tickCountAfter400Samples(0.5);

            // ~100 ticks over 400 samples at one tick per 4 samples - this count must not depend
            // on pitch, even though the actual held VALUES/how far they step through the source
            // differ because sourcePhase itself advances at pitchRatio-scaled speed per tick.
            expectWithinAbsoluteError(ticksAtUnity, ticksUpAnOctave, 1.0, "tick rate must be pitch-independent");
            expectWithinAbsoluteError(ticksAtUnity, ticksDownAnOctave, 1.0, "tick rate must be pitch-independent");
        }

        beginTest("Regression: Mode B's root-pitch playback speed tracks the file's real rate, not baseRateHz "
                  "(same real reported bug as Mode A's own regression test above)");
        {
            std::vector<float> data(200000, 0.0f);
            constexpr double hostRate = 44100.0;
            constexpr double fileRateHz = 44100.0;
            constexpr int numSamples = 100000;

            auto totalSourceAdvance = [&](double baseRateHz)
            {
                ConcretePitchEngine engine;
                engine.prepare(hostRate);
                engine.start(0.0);
                for (int i = 0; i < numSamples; ++i)
                    engine.processSample(ConcretePitchEngine::Mode::dropSampleDecimation, data.data(), (int) data.size(),
                                           1.0, baseRateHz, fileRateHz);
                return engine.getSourcePhase();
            };

            const auto expected = (fileRateHz / hostRate) * numSamples;
            expectWithinAbsoluteError(totalSourceAdvance(9380.0), expected, 10.0,
                                       "the Casio SK-1's 9.38kHz base rate must not change root-pitch playback speed");
            expectWithinAbsoluteError(totalSourceAdvance(26040.0), expected, 10.0,
                                       "the SP-1200's 26.04kHz base rate must not change root-pitch playback speed");
            expectWithinAbsoluteError(totalSourceAdvance(50000.0), expected, 10.0,
                                       "a base rate above the file's real rate must not change root-pitch playback speed either");
        }

        beginTest("Mode C's output is bounded and tracks a constant DC input");
        {
            std::vector<float> dc(2000, 0.6f);
            ConcretePitchEngine engine;
            engine.prepare(44100.0);
            engine.start(0.0);

            float lastValue = 0.0f;
            for (int i = 0; i < 2000; ++i)
            {
                lastValue = engine.processSample(ConcretePitchEngine::Mode::deltaSigma, dc.data(), (int) dc.size(), 1.0, 30000.0, 30000.0);
                expect(std::isfinite(lastValue), "delta-sigma output must never be NaN/inf");
                expect(std::abs(lastValue) <= 1.0f + 1.0e-3f, "delta-sigma output must stay within a reasonable bound");
            }
            expectWithinAbsoluteError(lastValue, 0.6f, 0.05f,
                                       "after settling, the decimated output should track the constant input");
        }

        beginTest("Regression: Mode C's root-pitch playback speed tracks the file's real rate, not baseRateHz "
                  "(same real reported bug as Modes A/B's own regression tests above)");
        {
            std::vector<float> data(2000000, 0.0f);
            constexpr double hostRate = 44100.0;
            constexpr double fileRateHz = 44100.0;
            constexpr int numSamples = 100000;

            auto totalSourceAdvance = [&](double baseRateHz)
            {
                ConcretePitchEngine engine;
                engine.prepare(hostRate);
                engine.start(0.0);
                for (int i = 0; i < numSamples; ++i)
                    engine.processSample(ConcretePitchEngine::Mode::deltaSigma, data.data(), (int) data.size(),
                                           1.0, baseRateHz, fileRateHz);
                return engine.getSourcePhase();
            };

            const auto expected = (fileRateHz / hostRate) * numSamples;
            // A wider tolerance than Modes A/B: at 64x oversampling, up to 64 oversample ticks can
            // be "in flight" (accumulated but not yet fired) at any host sample, versus at most 1
            // for A/B's own single-rate tick clocks - see readModeC()'s own oversampleTickAccumulator.
            expectWithinAbsoluteError(totalSourceAdvance(9380.0), expected, 128.0,
                                       "the Casio SK-1's 9.38kHz base rate must not change root-pitch playback speed");
            expectWithinAbsoluteError(totalSourceAdvance(30000.0), expected, 128.0,
                                       "the ASR-10's own ~30kHz base rate must not change root-pitch playback speed");
        }

        beginTest("All modes' getSourcePhase() reaches the same position given identical inputs (channel-lockstep assumption)");
        {
            // ConcreteVoice relies on two independent ConcretePitchEngine instances (one per
            // source channel of a stereo zone) staying numerically identical when fed identical
            // control parameters - this is what makes that safe.
            std::vector<float> dataA(1000, 0.1f);
            std::vector<float> dataB(1000, 0.9f); // different DATA, same length/parameters

            ConcretePitchEngine engineA, engineB;
            engineA.prepare(44100.0);
            engineB.prepare(44100.0);
            engineA.start(3.5);
            engineB.start(3.5);

            for (int i = 0; i < 500; ++i)
            {
                engineA.processSample(ConcretePitchEngine::Mode::dropSampleDecimation, dataA.data(), (int) dataA.size(), 1.3, 20000.0, 20000.0);
                engineB.processSample(ConcretePitchEngine::Mode::dropSampleDecimation, dataB.data(), (int) dataB.size(), 1.3, 20000.0, 20000.0);
            }

            expectWithinAbsoluteError(engineA.getSourcePhase(), engineB.getSourcePhase(), 1.0e-9);
        }
    }
};

static ConcretePitchEngineTests concretePitchEngineTests;
