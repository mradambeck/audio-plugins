#include "../ConcreteCapturePass.h"

#include <juce_core/juce_core.h>

#include <cmath>
#include <vector>

// Black-box tests against ConcreteCapturePass::apply() (resample/applyDrive are private) -
// structural/behavioral correctness (bypass is a true no-op, resample changes length by the
// expected ratio, drive/quantization are genuinely wired in, output stays finite/bounded). The
// empirical spectral claims (alias energy vs. transpose, drive's HF rolloff, non-destructiveness
// against the real ConcreteAudioProcessor) are verified through ConcreteRenderIR and
// analysis/verify_phase4.py instead, matching this catalog's convention (see
// concrete-sampler-plugin-plan.md's Ground rules and ConcretePitchEngineTests.cpp's own comment on
// the same split).
namespace
{
    juce::AudioBuffer<float> makeSine(double freqHz, double sampleRate, int numSamples, float amplitude = 0.5f)
    {
        juce::AudioBuffer<float> buffer(1, numSamples);
        auto* data = buffer.getWritePointer(0);
        for (int i = 0; i < numSamples; ++i)
            data[i] = amplitude * (float) std::sin(2.0 * juce::MathConstants<double>::pi * freqHz * (double) i / sampleRate);
        return buffer;
    }

    float maxAbsDifference(const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
    {
        const auto n = juce::jmin(a.getNumSamples(), b.getNumSamples());
        float worst = 0.0f;
        for (int i = 0; i < n; ++i)
            worst = juce::jmax(worst, std::abs(a.getSample(0, i) - b.getSample(0, i)));
        return worst;
    }
}

class ConcreteCapturePassTests : public juce::UnitTest
{
public:
    ConcreteCapturePassTests() : juce::UnitTest("ConcreteCapturePass", "Concrete") {}

    void runTest() override
    {
        const double sampleRate = 44100.0;

        beginTest("Bypass is an exact, unmodified copy of the source, with zero baked transpose");
        {
            const auto source = makeSine(1000.0, sampleRate, 4410);
            ConcreteCapturePass::Settings settings;
            settings.bypass = true;
            settings.transposeSemitones = 12.0; // must be ignored while bypassed
            settings.bitDepthBits = 4;           // must be ignored while bypassed

            const auto result = ConcreteCapturePass::apply(source, sampleRate, settings);
            expectEquals(result.buffer->getNumSamples(), source.getNumSamples());
            expectWithinAbsoluteError(result.captureTransposeSemitones, 0.0, 1.0e-9);
            expectWithinAbsoluteError(maxAbsDifference(*result.buffer, source), 0.0f, 1.0e-9f,
                                       "bypass must not alter a single sample");
        }

        beginTest("Transpose 0 with drive/quantization at their transparent defaults leaves the buffer length "
                  "unchanged and values very close to the source");
        {
            const auto source = makeSine(1000.0, sampleRate, 4410);
            ConcreteCapturePass::Settings settings;
            settings.bypass = false;
            settings.transposeSemitones = 0.0;
            settings.inputDriveDb = 0.0;
            settings.quantizerMode = ConcreteQuantizer::Mode::linear;
            settings.bitDepthBits = 16;

            const auto result = ConcreteCapturePass::apply(source, sampleRate, settings);
            expectEquals(result.buffer->getNumSamples(), source.getNumSamples());
            expectWithinAbsoluteError(maxAbsDifference(*result.buffer, source), 0.0f, 1.0e-3f,
                                       "ratio-1 resample plus 16-bit quantization should stay very close to the source");
        }

        beginTest("A +12 semitone (one octave) capture transpose halves the buffer length");
        {
            const auto source = makeSine(1000.0, sampleRate, 4410);
            ConcreteCapturePass::Settings settings;
            settings.bypass = false;
            settings.transposeSemitones = 12.0;
            settings.iterations = 1;

            const auto result = ConcreteCapturePass::apply(source, sampleRate, settings);
            const auto expectedLength = (int) std::llround((double) source.getNumSamples() / 2.0);
            expect(std::abs(result.buffer->getNumSamples() - expectedLength) <= 1,
                   "length should shrink by very close to a factor of 2");
            expectWithinAbsoluteError(result.captureTransposeSemitones, 12.0, 1.0e-9);
        }

        beginTest("Iterations compound: captureTransposeSemitones scales with iteration count, and the "
                  "buffer shrinks further each time");
        {
            const auto source = makeSine(1000.0, sampleRate, 44100);
            ConcreteCapturePass::Settings settings;
            settings.bypass = false;
            settings.transposeSemitones = 12.0;
            settings.iterations = 3;

            const auto result = ConcreteCapturePass::apply(source, sampleRate, settings);
            expectWithinAbsoluteError(result.captureTransposeSemitones, 36.0, 1.0e-9,
                                       "3 iterations of +12 semitones should bake in +36 total");
            const auto expectedLength = (int) std::llround((double) source.getNumSamples() / 8.0); // 2^3
            expect(std::abs(result.buffer->getNumSamples() - expectedLength) <= 4,
                   "length should shrink by very close to a factor of 8 across 3 iterations");
        }

        beginTest("iterations is clamped to [1, 4] even if a caller passes something outside that range");
        {
            const auto source = makeSine(1000.0, sampleRate, 4410);
            ConcreteCapturePass::Settings tooMany;
            tooMany.bypass = false;
            tooMany.transposeSemitones = 12.0;
            tooMany.iterations = 99;

            ConcreteCapturePass::Settings clampedToFour = tooMany;
            clampedToFour.iterations = 4;

            const auto resultTooMany = ConcreteCapturePass::apply(source, sampleRate, tooMany);
            const auto resultClamped = ConcreteCapturePass::apply(source, sampleRate, clampedToFour);
            expectEquals(resultTooMany.buffer->getNumSamples(), resultClamped.buffer->getNumSamples());
            expectWithinAbsoluteError(resultTooMany.captureTransposeSemitones, resultClamped.captureTransposeSemitones, 1.0e-9);
        }

        beginTest("Input drive is genuinely wired in: driving hot measurably changes the output vs. 0dB drive");
        {
            const auto source = makeSine(200.0, sampleRate, 4410, 0.95f); // loud, low-frequency - drive should bite
            ConcreteCapturePass::Settings noDrive;
            noDrive.bypass = false;
            noDrive.transposeSemitones = 0.0;
            noDrive.inputDriveDb = 0.0;

            ConcreteCapturePass::Settings hotDrive = noDrive;
            hotDrive.inputDriveDb = 24.0;

            const auto resultNoDrive = ConcreteCapturePass::apply(source, sampleRate, noDrive);
            const auto resultHotDrive = ConcreteCapturePass::apply(source, sampleRate, hotDrive);
            expect(maxAbsDifference(*resultNoDrive.buffer, *resultHotDrive.buffer) > 0.05f,
                   "24dB of drive should audibly change a loud signal versus no drive at all");
        }

        beginTest("Quantization is genuinely wired in: every output sample lands on a valid step for the "
                  "configured bit depth");
        {
            const auto source = makeSine(1000.0, sampleRate, 4410);
            ConcreteCapturePass::Settings settings;
            settings.bypass = false;
            settings.transposeSemitones = 0.0;
            settings.quantizerMode = ConcreteQuantizer::Mode::linear;
            settings.bitDepthBits = 4; // coarse enough that "not quantized at all" would be obviously wrong

            const auto result = ConcreteCapturePass::apply(source, sampleRate, settings);
            const auto levels = (float) (1 << (settings.bitDepthBits - 1));
            for (int i = 0; i < result.buffer->getNumSamples(); ++i)
            {
                const auto sample = result.buffer->getSample(0, i);
                const auto nearestStep = std::round(sample * levels) / levels;
                expectWithinAbsoluteError(sample, nearestStep, 1.0e-5f,
                                           "every sample must land exactly on a 4-bit quantization step");
            }
        }

        beginTest("Output is always finite and bounded across bypass, transpose, drive, and quantization combinations");
        {
            const auto source = makeSine(300.0, sampleRate, 4410, 0.99f);
            for (bool bypass : { true, false })
            {
                for (double transpose : { 0.0, 5.0, 24.0 })
                {
                    for (double drive : { 0.0, 12.0, 24.0 })
                    {
                        ConcreteCapturePass::Settings settings;
                        settings.bypass = bypass;
                        settings.transposeSemitones = transpose;
                        settings.inputDriveDb = drive;
                        settings.bitDepthBits = 8;

                        const auto result = ConcreteCapturePass::apply(source, sampleRate, settings);
                        for (int i = 0; i < result.buffer->getNumSamples(); ++i)
                        {
                            const auto sample = result.buffer->getSample(0, i);
                            expect(std::isfinite(sample), "output must never be NaN/inf");
                            expect(std::abs(sample) <= 1.0f + 1.0e-3f, "output must stay within a reasonable bound");
                        }
                    }
                }
            }
        }

        beginTest("An empty source buffer is handled without crashing");
        {
            juce::AudioBuffer<float> empty(1, 0);
            ConcreteCapturePass::Settings settings;
            settings.bypass = false;
            settings.transposeSemitones = 5.0;

            const auto result = ConcreteCapturePass::apply(empty, sampleRate, settings);
            expect(result.buffer != nullptr);
        }

        beginTest("Multi-channel (stereo) source buffers are resampled independently per channel");
        {
            juce::AudioBuffer<float> stereo(2, 4410);
            for (int i = 0; i < stereo.getNumSamples(); ++i)
            {
                stereo.setSample(0, i, 0.8f);
                stereo.setSample(1, i, -0.8f);
            }
            ConcreteCapturePass::Settings settings;
            settings.bypass = false;
            settings.transposeSemitones = 5.0;
            settings.bitDepthBits = 16;

            const auto result = ConcreteCapturePass::apply(stereo, sampleRate, settings);
            expectEquals(result.buffer->getNumChannels(), 2);
            expect(result.buffer->getSample(0, result.buffer->getNumSamples() / 2) > 0.5f,
                   "left channel should stay positive, not be corrupted by the right channel's data");
            expect(result.buffer->getSample(1, result.buffer->getNumSamples() / 2) < -0.5f,
                   "right channel should stay negative, not be corrupted by the left channel's data");
        }
    }
};

static ConcreteCapturePassTests concreteCapturePassTests;
