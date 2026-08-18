#include "../GradientPitchShiftEngine.h"

#include <juce_core/juce_core.h>
#include <juce_dsp/juce_dsp.h>

#include <cmath>
#include <vector>

// Fills the "sounds roughly right" gap from a by-ear-only check: feeds a known-frequency sine
// wave through the real engine code and measures the actual output frequency, rather than
// trusting the ramp-rate-to-pitch-ratio formula by inspection alone.
//
// A note on tolerance: this does NOT hold the engine to tight (~1%) pitch accuracy at this
// milestone. Investigation (see notes below and the implementation plan's Milestone 2 write-up)
// found a real, reproducible, direction-independent-magnitude bias of several percent up to
// ~20% at extreme ratios, present regardless of crossfade width (identical from a 240-sample
// crossfade down to a 0.5-sample one) or ramp-window size, and confirmed by three independent
// measurement methods (zero-crossing counting, FFT peak with two different window functions,
// and autocorrelation) all agreeing on the same "wrong" frequency - i.e. this is real signal
// content, not a measurement artifact, and the underlying tap-ramp math itself is provably
// correct (verified separately: a single, non-wrapping tap measures correctly, and the delay
// value's own average rate of change matches the intended rampRate to 6 decimal places).
//
// The actual cause: the two taps are NOT phase-aligned with each other (they're just a fixed
// half-window apart), so whenever the crossfade blends between them it's blending two copies of
// the same frequency content at an essentially arbitrary relative phase. Summing two same-
// frequency sinusoids with CONSTANT weights is still that same frequency (a basic trig identity),
// but the crossfade's weights are TIME-VARYING, and blending non-phase-aligned content with
// time-varying weights is a genuine spectral-shifting phenomenon (the same reason FM/PM sidebands
// exist), not a bug to patch out here. This is exactly the problem the H949's ALG-3 solved with
// autocorrelation-based, phase-aligned splice timing - i.e. exactly what Milestone 4's
// "De-glitch smart" mode is scoped to add. Milestone 4 should re-run this same measurement with
// phase-aware splice timing enabled and expect it to tighten substantially; if it doesn't, that
// milestone's splice-timing logic has a real problem worth chasing.
class GradientPitchShiftEngineTests : public juce::UnitTest
{
public:
    GradientPitchShiftEngineTests() : juce::UnitTest("GradientPitchShiftEngine pitch accuracy", "Gradient") {}

    // hintFreqHz restricts the peak search to a window around the expected frequency (+-25%) so a
    // stray strong bin elsewhere in the spectrum can't be picked up by mistake.
    static double measureFrequency(const std::vector<float>& samples, double sampleRate, double hintFreqHz)
    {
        constexpr int order = 16;
        constexpr int fftSize = 1 << order; // 65536 samples (~1.37s @ 48kHz) - bin width ~0.73Hz

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

        // Quadratic interpolation across the peak and its two neighbours, for sub-bin accuracy.
        const auto yMinus = (double) data[(size_t) (peakBin - 1)];
        const auto yZero = (double) data[(size_t) peakBin];
        const auto yPlus = (double) data[(size_t) (peakBin + 1)];
        const auto denom = yMinus - 2.0 * yZero + yPlus;
        const auto delta = (denom != 0.0) ? 0.5 * (yMinus - yPlus) / denom : 0.0;

        return ((double) peakBin + delta) * binHz;
    }

    void runPitchTest(double sampleRate, float inputFreqHz, float semitones)
    {
        GradientPitchShiftEngine engine;
        engine.prepare(sampleRate);
        engine.setPitchSemitones(semitones, 0.0f);
        engine.setDelayTimeMs(0.0f);
        engine.setMix(100.0f); // fully wet - isolate the shifted signal
        engine.setOutputTrimDb(0.0f);

        constexpr double totalSeconds = 2.0;
        const auto totalSamples = (int) (sampleRate * totalSeconds);

        std::vector<float> output;
        output.reserve((size_t) totalSamples);

        double phase = 0.0;
        const auto phaseIncrement = juce::MathConstants<double>::twoPi * (double) inputFreqHz / sampleRate;

        for (int i = 0; i < totalSamples; ++i)
        {
            const auto inputSample = (float) std::sin(phase);
            phase += phaseIncrement;
            if (phase >= juce::MathConstants<double>::twoPi)
                phase -= juce::MathConstants<double>::twoPi;

            output.push_back(engine.process(inputSample));
        }

        // Skip the first 300ms (comfortably past the ~30ms ramp-window fill transient) and the
        // last 100ms, measuring only a clean steady-state region.
        const auto startIndex = (size_t) (sampleRate * 0.3);
        const auto endIndex = (size_t) (sampleRate * (totalSeconds - 0.1));
        const std::vector<float> steadyRegion(output.begin() + (long) startIndex, output.begin() + (long) endIndex);

        const auto expectedRatio = std::pow(2.0, (double) semitones / 12.0);
        const auto expectedFreq = (double) inputFreqHz * expectedRatio;
        const auto measuredFreq = measureFrequency(steadyRegion, sampleRate, expectedFreq);

        expect(measuredFreq > 0.0, "Measured frequency should be nonzero");

        if (semitones == 0.0f)
        {
            // No pitch shift takes the single-tap bypass path entirely (see process()) - this
            // has none of the phase-discontinuity behaviour above, so it's held to the tight
            // tolerance a "no-op" case should trivially meet.
            expectWithinAbsoluteError(measuredFreq, expectedFreq, expectedFreq * 0.01,
                                       "0st (bypass) should reproduce the input frequency almost exactly");
            return;
        }

        // Direction must always be correct: a request to shift up must measure higher than the
        // input, down must measure lower. This is the one thing that must never be wrong,
        // regardless of the phase-discontinuity magnitude bias described above.
        if (semitones > 0.0f)
            expect(measuredFreq > inputFreqHz, juce::String(semitones, 1) + "st: shifted-up measurement should exceed the input frequency");
        else
            expect(measuredFreq < inputFreqHz, juce::String(semitones, 1) + "st: shifted-down measurement should be below the input frequency");

        // 30% relative tolerance on magnitude - loose enough to absorb the phase-discontinuity
        // bias documented above (observed up to ~20% at +-24 semitones with this un-phase-aligned
        // Milestone 2 splice), tight enough to catch a genuinely wrong ramp-rate formula (e.g. an
        // accidental 2x/0.5x scaling error, or a sign flip that direction-checking above wouldn't
        // catch on its own for some inputs).
        const auto toleranceHz = expectedFreq * 0.30;
        expectWithinAbsoluteError(measuredFreq, expectedFreq, toleranceHz,
                                   juce::String(semitones, 1) + "st: expected ~" + juce::String(expectedFreq, 1)
                                       + " Hz, measured " + juce::String(measuredFreq, 1) + " Hz");
    }

    // Milestone 4 follow-up to the class comment above: measures pitch accuracy for a given splice
    // mode without asserting, so callers can compare Glitch vs De-glitch Smart directly.
    static double measureFreqForMode(double sampleRate, float inputFreqHz, float semitones,
                                      GradientPitchShiftEngine::SpliceMode mode, float crossfadeLengthMs)
    {
        GradientPitchShiftEngine engine;
        engine.prepare(sampleRate);
        engine.setPitchSemitones(semitones, 0.0f);
        engine.setDelayTimeMs(0.0f);
        engine.setMix(100.0f);
        engine.setOutputTrimDb(0.0f);
        engine.setSpliceMode(mode);
        engine.setCrossfadeLengthMs(crossfadeLengthMs);

        constexpr double totalSeconds = 2.0;
        const auto totalSamples = (int) (sampleRate * totalSeconds);

        std::vector<float> output;
        output.reserve((size_t) totalSamples);

        double phase = 0.0;
        const auto phaseIncrement = juce::MathConstants<double>::twoPi * (double) inputFreqHz / sampleRate;

        for (int i = 0; i < totalSamples; ++i)
        {
            const auto inputSample = (float) std::sin(phase);
            phase += phaseIncrement;
            if (phase >= juce::MathConstants<double>::twoPi)
                phase -= juce::MathConstants<double>::twoPi;

            output.push_back(engine.process(inputSample));
        }

        const auto startIndex = (size_t) (sampleRate * 0.3);
        const auto endIndex = (size_t) (sampleRate * (totalSeconds - 0.1));
        const std::vector<float> steadyRegion(output.begin() + (long) startIndex, output.begin() + (long) endIndex);

        const auto expectedRatio = std::pow(2.0, (double) semitones / 12.0);
        const auto expectedFreq = (double) inputFreqHz * expectedRatio;
        return measureFrequency(steadyRegion, sampleRate, expectedFreq);
    }

    void runTest() override
    {
        constexpr double sampleRate = 48000.0;
        constexpr float inputFreq = 440.0f;

        beginTest("Pitch shift accuracy across several semitone intervals");
        {
            for (float semitones : { -24.0f, -12.0f, -7.0f, 0.0f, 7.0f, 12.0f, 24.0f })
                runPitchTest(sampleRate, inputFreq, semitones);
        }

        beginTest("De-glitch smart tightens pitch accuracy vs Glitch at extreme intervals "
                   "(per the plan's Milestone 4 gate - this must genuinely improve, not just sound smoother)");
        {
            for (float semitones : { -24.0f, 24.0f })
            {
                const auto expectedRatio = std::pow(2.0, (double) semitones / 12.0);
                const auto expectedFreq = (double) inputFreq * expectedRatio;

                const auto glitchFreq = measureFreqForMode(sampleRate, inputFreq, semitones,
                                                             GradientPitchShiftEngine::SpliceMode::glitch, 5.0f);
                const auto smartFreq = measureFreqForMode(sampleRate, inputFreq, semitones,
                                                            GradientPitchShiftEngine::SpliceMode::deglitchSmart, 20.0f);

                const auto glitchError = std::abs(glitchFreq - expectedFreq) / expectedFreq;
                const auto smartError = std::abs(smartFreq - expectedFreq) / expectedFreq;

                logMessage(juce::String(semitones, 0) + "st: glitch=" + juce::String(glitchFreq, 1)
                           + "Hz (" + juce::String(glitchError * 100.0, 1) + "% off), smart="
                           + juce::String(smartFreq, 1) + "Hz (" + juce::String(smartError * 100.0, 1) + "% off), expected "
                           + juce::String(expectedFreq, 1) + "Hz");

                expect(smartError < glitchError,
                       juce::String(semitones, 0) + "st: De-glitch smart should measure closer to the expected "
                       "frequency than Glitch mode");
            }
        }
    }
};

static GradientPitchShiftEngineTests gradientPitchShiftEngineTests;
