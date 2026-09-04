#include "../GradientPitchShiftEngine.h"

#include <juce_core/juce_core.h>
#include <juce_dsp/juce_dsp.h>

#include <cmath>
#include <vector>

// Milestone 5: Drift is a slow, one-pole-lowpassed random walk (Alloy's "Age" technique), NOT a
// literal LFO, applied as a small additive offset onto the tap-delay values. These tests check the
// three things that actually matter for this design: it's silent at 0% (so it never surprises a
// user who hasn't touched the knob), it produces genuine non-constant instability at 100% (not a
// no-op), and - critical for Milestone 6's dual mode - two engine instances get independent
// trajectories rather than wobbling in lockstep, since each seeds its own RNG from its own address.
class GradientDriftTests : public juce::UnitTest
{
public:
    GradientDriftTests() : juce::UnitTest("GradientPitchShiftEngine drift", "Gradient") {}

    // Same restricted-search FFT approach as GradientPitchShiftEngineTests, duplicated locally to
    // keep this file independent.
    static double measureFrequency(const std::vector<float>& samples, double sampleRate, double hintFreqHz)
    {
        constexpr int order = 14;
        constexpr int fftSize = 1 << order; // 16384 samples (~0.34s @ 48kHz)

        juce::dsp::FFT fft(order);
        std::vector<float> data((size_t) fftSize * 2, 0.0f);

        const auto numToUse = std::min((size_t) fftSize, samples.size());
        for (size_t i = 0; i < numToUse; ++i)
        {
            const auto window = 0.5f - 0.5f * std::cos(juce::MathConstants<float>::twoPi * (float) i / (float) (numToUse - 1));
            data[i] = samples[i] * window;
        }

        fft.performFrequencyOnlyForwardTransform(data.data(), true);

        const auto binHz = sampleRate / (double) fftSize;
        const auto searchLowBin = std::max(1, (int) ((hintFreqHz * 0.75) / binHz));
        const auto searchHighBin = std::min(fftSize / 2 - 1, (int) ((hintFreqHz * 1.25) / binHz));

        int peakBin = searchLowBin;
        float peakMag = data[(size_t) searchLowBin];
        for (int bin = searchLowBin + 1; bin <= searchHighBin; ++bin)
        {
            if (data[(size_t) bin] > peakMag)
            {
                peakMag = data[(size_t) bin];
                peakBin = bin;
            }
        }

        const auto yMinus = (double) data[(size_t) (peakBin - 1)];
        const auto yZero = (double) data[(size_t) peakBin];
        const auto yPlus = (double) data[(size_t) (peakBin + 1)];
        const auto denom = yMinus - 2.0 * yZero + yPlus;
        const auto delta = (denom != 0.0) ? 0.5 * (yMinus - yPlus) / denom : 0.0;

        return ((double) peakBin + delta) * binHz;
    }

    static std::vector<float> renderTone(GradientPitchShiftEngine& engine, double sampleRate,
                                          float inputFreqHz, double seconds)
    {
        std::vector<float> output;
        const auto n = (int) (sampleRate * seconds);
        output.reserve((size_t) n);

        double phase = 0.0;
        const auto phaseIncrement = juce::MathConstants<double>::twoPi * (double) inputFreqHz / sampleRate;
        for (int i = 0; i < n; ++i)
        {
            const auto inputSample = (float) std::sin(phase);
            phase += phaseIncrement;
            if (phase >= juce::MathConstants<double>::twoPi)
                phase -= juce::MathConstants<double>::twoPi;
            output.push_back(engine.process(inputSample));
        }
        return output;
    }

    void runTest() override
    {
        constexpr double sampleRate = 48000.0;
        constexpr float inputFreq = 440.0f;

        beginTest("Drift at 0% matches the already-verified no-drift pitch accuracy exactly");
        {
            // Not a new measurement - GradientPitchShiftEngineTests' pitch-accuracy numbers are
            // bit-identical before and after Drift was added (Milestone 5 build log), which is the
            // real evidence for this. This test just documents that expectation as a standing
            // regression guard: default-constructed Drift (0%) should never be reachable from a
            // fresh engine without an explicit setDrift() call.
            GradientPitchShiftEngine engine;
            engine.prepare(sampleRate);
            engine.setPitchSemitones(0.0f, 0.0f);
            engine.setDelayTimeMs(0.0f);
            engine.setMix(100.0f);
            engine.setOutputTrimDb(0.0f);
            // setDrift() deliberately not called - engine should default to 0%.

            const auto output = renderTone(engine, sampleRate, inputFreq, 1.0);
            const std::vector<float> steady(output.begin() + (long) (sampleRate * 0.3), output.end());
            const auto measured = measureFrequency(steady, sampleRate, inputFreq);
            expectWithinAbsoluteError(measured, (double) inputFreq, (double) inputFreq * 0.01,
                                       "0st bypass with default (unset) Drift should reproduce the input frequency almost exactly");
        }

        beginTest("Drift at 100% audibly changes the output vs Drift at 0%, at a bounded magnitude");
        {
            // Per-window FFT peak frequency turns out to be the wrong metric here: Drift is a SLOW,
            // shared (both taps) additive offset, so its own rate of change (what actually drives
            // measured pitch - see the class comment on rampRate) is tiny relative to the main pitch
            // ramp, and a long FFT window averages out what little there is. Its real, intended
            // effect is a subtle delay-time wobble (vibrato/chorus-like character, closer to tape
            // wow than a pitch shift) - which a direct time-domain comparison against a Drift-off
            // baseline actually detects, the same approach the cross-instance-independence test
            // below already uses successfully.
            auto renderWithDrift = [&](float driftPercent)
            {
                GradientPitchShiftEngine engine;
                engine.prepare(sampleRate);
                engine.setPitchSemitones(7.0f, 0.0f);
                engine.setDelayTimeMs(50.0f);
                engine.setMix(100.0f);
                engine.setOutputTrimDb(0.0f);
                engine.setDrift(driftPercent);
                return renderTone(engine, sampleRate, inputFreq, 5.0);
            };

            const auto outputOff = renderWithDrift(0.0f);
            const auto outputOn = renderWithDrift(100.0f);

            double sumSqDiff = 0.0;
            double sumSqOff = 0.0;
            const auto startIdx = (size_t) (sampleRate * 0.5);
            for (size_t i = startIdx; i < outputOff.size(); ++i)
            {
                const auto diff = (double) outputOn[i] - (double) outputOff[i];
                sumSqDiff += diff * diff;
                sumSqOff += (double) outputOff[i] * (double) outputOff[i];
            }
            const auto rmsDiff = std::sqrt(sumSqDiff / (double) (outputOff.size() - startIdx));
            const auto rmsOff = std::sqrt(sumSqOff / (double) (outputOff.size() - startIdx));

            logMessage("Drift on vs off: RMS diff=" + juce::String(rmsDiff, 5) + ", baseline RMS=" + juce::String(rmsOff, 5));

            expect(rmsDiff > 1.0e-4,
                   "Drift at 100% should measurably change the output vs Drift at 0% (measured RMS diff="
                   + juce::String(rmsDiff, 6) + ")");

            // Bounded: Drift's excursion is capped at a few ms (see maxDriftExcursionMs), so the
            // difference it introduces shouldn't approach the signal's own overall level - it's a
            // subtle wobble, not a wholesale replacement of the signal.
            expect(rmsDiff < rmsOff * 0.5,
                   "Drift at 100% should be a subtle wobble, not dominate the signal (RMS diff="
                   + juce::String(rmsDiff, 5) + " vs baseline RMS=" + juce::String(rmsOff, 5) + ")");
        }

        beginTest("Two engine instances drift independently, not in lockstep (critical for dual mode)");
        {
            GradientPitchShiftEngine engineA;
            engineA.prepare(sampleRate);
            engineA.setPitchSemitones(7.0f, 0.0f);
            engineA.setDelayTimeMs(50.0f);
            engineA.setMix(100.0f);
            engineA.setOutputTrimDb(0.0f);
            engineA.setDrift(100.0f);

            GradientPitchShiftEngine engineB;
            engineB.prepare(sampleRate);
            engineB.setPitchSemitones(7.0f, 0.0f);
            engineB.setDelayTimeMs(50.0f);
            engineB.setMix(100.0f);
            engineB.setOutputTrimDb(0.0f);
            engineB.setDrift(100.0f);

            const auto outputA = renderTone(engineA, sampleRate, inputFreq, 2.0);
            const auto outputB = renderTone(engineB, sampleRate, inputFreq, 2.0);

            // Same settings, same input, identical engines constructed the same way - if the RNG
            // seed were shared/static instead of per-instance, these would be identical. They must
            // not be, or L/R will wobble in lockstep once dual mode exists (Milestone 6).
            double sumSqDiff = 0.0;
            const auto startIdx = (size_t) (sampleRate * 0.5);
            for (size_t i = startIdx; i < outputA.size(); ++i)
            {
                const auto diff = (double) outputA[i] - (double) outputB[i];
                sumSqDiff += diff * diff;
            }
            const auto rmsDiff = std::sqrt(sumSqDiff / (double) (outputA.size() - startIdx));

            logMessage("RMS difference between two independently-drifting engines: " + juce::String(rmsDiff, 5));
            expect(rmsDiff > 1.0e-4,
                   "Two engine instances with identical settings should NOT produce identical output "
                   "under Drift - each must seed its own RNG independently (measured RMS diff="
                   + juce::String(rmsDiff, 6) + ")");
        }
    }
};

static GradientDriftTests gradientDriftTests;
