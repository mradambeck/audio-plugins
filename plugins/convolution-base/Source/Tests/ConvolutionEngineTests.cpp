#include "ConvolutionEngine.h"
#include "IRLibrary.h"
#include "IRShaper.h"

#include <algorithm>
#include <cmath>
#include <vector>

// The measurements no compile can prove: what latency the convolution actually reports, whether the
// wet path is genuinely transparent at its defaults, and whether bypassing or swapping an IR
// produces a discontinuity. Every plugin-visible claim in the plan that isn't a pure function is
// pinned down here.
namespace
{
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 256;

    // Index into variantConfig()'s IR table. The Dirac is the measurement tool: convolving with it
    // is an identity operation, so anything the output does that the input didn't is the engine's
    // doing.
    constexpr int diracIR = 3;
    constexpr int roomIR = 0;
    constexpr int hallIR = 1;

    juce::AudioBuffer<float> shapedIR(int index, wildjag::conv::IRShaper::Params params = {})
    {
        using namespace wildjag::conv;

        IRLibrary library(variantConfig());
        library.setTargetSampleRate(sampleRate);

        auto decoded = library.getDecodedIR(index);
        if (decoded == nullptr)
            return {};

        return IRShaper::shape(decoded->samples, sampleRate, params);
    }

    void fillSine(std::vector<float>& signal, float amplitude, float frequencyHz)
    {
        for (size_t i = 0; i < signal.size(); ++i)
            signal[i] = amplitude * std::sin(juce::MathConstants<float>::twoPi * (float) frequencyHz
                                             * (float) i / (float) sampleRate);
    }

    // Runs `input` through the engine in blocks, optionally invoking `atSample` once the given
    // sample index has been reached - that is how the bypass and IR-swap transitions are triggered
    // partway through a continuous signal rather than between two separate runs.
    std::vector<float> runEngine(wildjag::conv::ConvolutionEngine& engine,
                                 const std::vector<float>& input,
                                 int numChannels,
                                 int triggerSample = -1,
                                 std::function<void()> atSample = {})
    {
        std::vector<float> output(input.size(), 0.0f);
        juce::AudioBuffer<float> block(numChannels, blockSize);

        for (int position = 0; position < (int) input.size(); position += blockSize)
        {
            const auto n = std::min(blockSize, (int) input.size() - position);

            if (triggerSample >= 0 && atSample && position <= triggerSample && triggerSample < position + n)
                atSample();

            for (int channel = 0; channel < numChannels; ++channel)
                for (int i = 0; i < n; ++i)
                    block.setSample(channel, i, input[(size_t) (position + i)]);

            engine.process(block, n);

            for (int i = 0; i < n; ++i)
                output[(size_t) (position + i)] = block.getSample(0, i);
        }

        return output;
    }

    float maxAbsoluteStep(const std::vector<float>& signal, int from, int to)
    {
        auto worst = 0.0f;
        for (int i = std::max(from, 1); i < std::min(to, (int) signal.size()); ++i)
            worst = std::max(worst, std::abs(signal[(size_t) i] - signal[(size_t) (i - 1)]));
        return worst;
    }

    float rms(const std::vector<float>& signal, int from, int to)
    {
        double sum = 0.0;
        int count = 0;
        for (int i = std::max(from, 0); i < std::min(to, (int) signal.size()); ++i, ++count)
            sum += (double) signal[(size_t) i] * (double) signal[(size_t) i];
        return count > 0 ? (float) std::sqrt(sum / (double) count) : 0.0f;
    }

    // Sets the engine to "wet only, nothing coloured" and snaps every ramp to its target, so a
    // measurement is not contaminated by a 20 ms gain ramp it did not ask about.
    void settleWetOnly(wildjag::conv::ConvolutionEngine& engine)
    {
        engine.setDryGain(0.0f);
        engine.setWetGain(1.0f);
        engine.setPreDelayMs(0.0f);
        engine.setLowCutHz(wildjag::conv::ConvolutionEngine::lowCutNeutralHz);
        engine.setHighCutHz(wildjag::conv::ConvolutionEngine::highCutNeutralHz);
        engine.setBypassed(false);
        engine.reset();
    }
}

class ConvolutionEngineTests : public juce::UnitTest
{
public:
    ConvolutionEngineTests() : juce::UnitTest("ConvolutionEngine", "Convolution") {}

    void runTest() override
    {
        using namespace wildjag::conv;

        beginTest("reports zero latency");
        {
            ConvolutionEngine engine;
            engine.prepare(sampleRate, blockSize, 2, shapedIR(diracIR));

            // NonUniform partitioning is chosen for CPU, not latency - this is the check that it
            // did not quietly buy that back as delay. If this ever fails, the fix is to report
            // getLatencySamples() honestly, not to hardcode zero.
            expectEquals(engine.getLatencySamples(), 0);
        }

        beginTest("an impulse through the Dirac IR comes out at sample 0");
        {
            ConvolutionEngine engine;
            engine.prepare(sampleRate, blockSize, 2, shapedIR(diracIR));
            settleWetOnly(engine);

            std::vector<float> input(4096, 0.0f);
            input[0] = 1.0f;

            const auto output = runEngine(engine, input, 2);

            auto peak = 0.0f;
            auto peakIndex = -1;
            for (int i = 0; i < (int) output.size(); ++i)
            {
                if (std::abs(output[(size_t) i]) > peak)
                {
                    peak = std::abs(output[(size_t) i]);
                    peakIndex = i;
                }
            }

            expectEquals(peakIndex, 0, "the convolved impulse must not be delayed");
            expectWithinAbsoluteError(peak, 1.0f, 0.01f);
        }

        beginTest("the wet path is transparent at its defaults");
        {
            // Dirac IR plus neutral filters and no pre-delay means the whole wet chain should be an
            // identity. Tolerance rather than bit-exactness only because the FFT convolution itself
            // rounds; the filters must contribute nothing at all.
            ConvolutionEngine engine;
            engine.prepare(sampleRate, blockSize, 1, shapedIR(diracIR));
            settleWetOnly(engine);

            std::vector<float> input(8192);
            fillSine(input, 0.5f, 440.0f);

            const auto output = runEngine(engine, input, 1);

            auto worstError = 0.0f;
            for (size_t i = 0; i < input.size(); ++i)
                worstError = std::max(worstError, std::abs(output[i] - input[i]));

            expect(worstError < 1.0e-4f, "wet path altered a defaults-only signal by " + juce::String(worstError));
        }

        beginTest("pre-delay shifts the onset by the requested time");
        {
            ConvolutionEngine engine;
            engine.prepare(sampleRate, blockSize, 1, shapedIR(diracIR));
            settleWetOnly(engine);
            engine.setPreDelayMs(10.0f);
            engine.reset();

            std::vector<float> input(8192, 0.0f);
            input[0] = 1.0f;

            const auto output = runEngine(engine, input, 1);

            auto peakIndex = 0;
            auto peak = 0.0f;
            for (int i = 0; i < (int) output.size(); ++i)
            {
                if (std::abs(output[(size_t) i]) > peak)
                {
                    peak = std::abs(output[(size_t) i]);
                    peakIndex = i;
                }
            }

            const auto expectedIndex = (int) std::lround(0.010 * sampleRate);
            expect(std::abs(peakIndex - expectedIndex) <= 2,
                   "onset at " + juce::String(peakIndex) + ", expected near " + juce::String(expectedIndex));
        }

        beginTest("low cut and high cut engage, and do nothing at their extremes");
        {
            auto measure = [](float lowCutHz, float highCutHz, float toneHz)
            {
                ConvolutionEngine engine;
                engine.prepare(sampleRate, blockSize, 1, shapedIR(diracIR));
                settleWetOnly(engine);
                engine.setLowCutHz(lowCutHz);
                engine.setHighCutHz(highCutHz);
                engine.reset();

                std::vector<float> input(16384);
                fillSine(input, 0.5f, toneHz);

                const auto output = runEngine(engine, input, 1);

                // Skip the first 4096 samples so filter settling is not measured as attenuation.
                return rms(output, 4096, (int) output.size());
            };

            const auto neutral = ConvolutionEngine::lowCutNeutralHz;
            const auto neutralTop = ConvolutionEngine::highCutNeutralHz;

            const auto lowToneFlat = measure(neutral, neutralTop, 80.0f);
            const auto lowToneCut = measure(800.0f, neutralTop, 80.0f);
            expect(lowToneCut < lowToneFlat * 0.3f,
                   "an 800 Hz low cut barely touched an 80 Hz tone: " + juce::String(lowToneCut)
                       + " vs " + juce::String(lowToneFlat));

            const auto highToneFlat = measure(neutral, neutralTop, 9000.0f);
            const auto highToneCut = measure(neutral, 1000.0f, 9000.0f);
            expect(highToneCut < highToneFlat * 0.3f,
                   "a 1 kHz high cut barely touched a 9 kHz tone: " + juce::String(highToneCut)
                       + " vs " + juce::String(highToneFlat));

            // And the extremes really are pass-through, not a near-transparent filter.
            expectWithinAbsoluteError(lowToneFlat, 0.5f / std::sqrt(2.0f), 0.01f);
            expectWithinAbsoluteError(highToneFlat, 0.5f / std::sqrt(2.0f), 0.01f);
        }

        beginTest("bypassing mid-tail does not produce a discontinuity");
        {
            const auto toggleSample = 24000;

            auto run = [&](bool shouldBypass)
            {
                ConvolutionEngine engine;
                engine.prepare(sampleRate, blockSize, 1, shapedIR(roomIR));
                engine.setDryGain(1.0f);
                engine.setWetGain(1.0f);
                engine.setPreDelayMs(0.0f);
                engine.setLowCutHz(ConvolutionEngine::lowCutNeutralHz);
                engine.setHighCutHz(ConvolutionEngine::highCutNeutralHz);
                engine.setBypassed(false);
                engine.reset();

                std::vector<float> input(72000);
                fillSine(input, 0.5f, 220.0f);

                return runEngine(engine, input, 1, toggleSample,
                                 [&engine, shouldBypass] { if (shouldBypass) engine.setBypassed(true); });
            };

            const auto control = run(false);
            const auto bypassed = run(true);

            // A window covering the whole ramp plus margin on either side.
            const auto from = toggleSample - 512;
            const auto to = toggleSample + (int) (0.05 * sampleRate);

            const auto controlStep = maxAbsoluteStep(control, from, to);
            const auto bypassedStep = maxAbsoluteStep(bypassed, from, to);

            expect(bypassedStep < controlStep * 3.0f,
                   "bypass introduced a step of " + juce::String(bypassedStep)
                       + " against a steady-state " + juce::String(controlStep));

            // And it actually did something - otherwise the assertion above passes trivially.
            expect(rms(bypassed, to, (int) bypassed.size()) < rms(control, to, (int) control.size()) * 0.95f,
                   "bypass did not change the output at all");
        }

        beginTest("swapping the IR mid-tail does not produce a discontinuity");
        {
            const auto swapSample = 24000;

            auto run = [&](bool shouldSwap)
            {
                ConvolutionEngine engine;
                engine.prepare(sampleRate, blockSize, 1, shapedIR(roomIR));
                settleWetOnly(engine);

                std::vector<float> input(96000);
                fillSine(input, 0.5f, 220.0f);

                return runEngine(engine, input, 1, swapSample, [&engine, shouldSwap]
                {
                    if (shouldSwap)
                        engine.loadIR(shapedIR(hallIR));
                });
            };

            const auto control = run(false);
            const auto swapped = run(true);

            // Wider than the bypass window: the load is asynchronous, so the crossfade begins
            // whenever the new engine is installed rather than exactly at the request.
            const auto from = swapSample - 512;
            const auto to = swapSample + (int) (0.5 * sampleRate);

            const auto controlStep = maxAbsoluteStep(control, from, to);
            const auto swappedStep = maxAbsoluteStep(swapped, from, to);

            expect(swappedStep < controlStep * 3.0f,
                   "IR swap introduced a step of " + juce::String(swappedStep)
                       + " against a steady-state " + juce::String(controlStep));
        }

        beginTest("mono IRs load without forcing a stereo buffer");
        {
            ConvolutionEngine engine;
            engine.prepare(sampleRate, blockSize, 1, shapedIR(2)); // the mono 96k plate
            settleWetOnly(engine);

            std::vector<float> input(8192);
            fillSine(input, 0.5f, 440.0f);

            const auto output = runEngine(engine, input, 1);
            expect(rms(output, 1024, (int) output.size()) > 0.0f, "a mono IR produced silence");
        }
    }
};

static ConvolutionEngineTests convolutionEngineTests;
