#include "ConvolutionProcessor.h"

#include <cmath>
#include <vector>

// Drives the real shared AudioProcessor: bus negotiation, the mono fan-out, state round-tripping,
// and the bypass-parameter override. None of this is reachable from the engine-only suite.
// TestCreateEditorStub.cpp keeps the editor, the LookAndFeel and the fonts out of this target.
namespace
{
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 256;

    void fillSine(juce::AudioBuffer<float>& buffer, float amplitude, float frequencyHz)
    {
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            for (int i = 0; i < buffer.getNumSamples(); ++i)
                buffer.setSample(channel, i, amplitude * std::sin(juce::MathConstants<float>::twoPi
                                                                  * frequencyHz * (float) i / (float) sampleRate));
    }

    // Sets a parameter from a real-world value (percent, ms, Hz) rather than a normalised 0-1 one.
    void setRaw(wildjag::conv::ConvolutionProcessor& processor, const char* paramID, float rawValue)
    {
        if (auto* param = processor.apvts.getParameter(paramID))
            param->setValueNotifyingHost(param->convertTo0to1(rawValue));
    }

    float rms(const juce::AudioBuffer<float>& buffer, int channel)
    {
        double sum = 0.0;
        const auto* samples = buffer.getReadPointer(channel);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            sum += (double) samples[i] * (double) samples[i];
        return (float) std::sqrt(sum / (double) buffer.getNumSamples());
    }
}

class ConvolutionProcessorTests : public juce::UnitTest
{
public:
    ConvolutionProcessorTests() : juce::UnitTest("ConvolutionProcessor", "Convolution") {}

    void runTest() override
    {
        using namespace wildjag::conv;

        beginTest("accepts mono->mono, mono->stereo and stereo->stereo");
        {
            ConvolutionProcessor processor(variantConfig());

            const auto mono = juce::AudioChannelSet::mono();
            const auto stereo = juce::AudioChannelSet::stereo();

            auto layout = [](const juce::AudioChannelSet& in, const juce::AudioChannelSet& out)
            {
                juce::AudioProcessor::BusesLayout l;
                l.inputBuses.add(in);
                l.outputBuses.add(out);
                return l;
            };

            expect(processor.isBusesLayoutSupported(layout(mono, mono)));
            expect(processor.isBusesLayoutSupported(layout(mono, stereo)));
            expect(processor.isBusesLayoutSupported(layout(stereo, stereo)));

            // Widening output beyond stereo is not supported - juce::dsp::Convolution handles mono
            // and stereo only, and silently dropping channels would be worse than refusing.
            expect(! processor.isBusesLayoutSupported(layout(stereo, mono)));
            expect(! processor.isBusesLayoutSupported(layout(stereo, juce::AudioChannelSet::create5point1())));
        }

        beginTest("exposes a real bypass parameter to the host");
        {
            ConvolutionProcessor processor(variantConfig());

            // Deliberately different from the rest of this catalog, which uses a plain parameter and
            // an early return. A convolution tail cannot survive that idiom.
            auto* bypass = processor.getBypassParameter();
            expect(bypass != nullptr, "the host must be given a bypass parameter to drive");
            expect(bypass == processor.apvts.getParameter(ConvolutionProcessor::bypassParamID));
        }

        beginTest("reports zero latency to the host");
        {
            ConvolutionProcessor processor(variantConfig());
            processor.setPlayConfigDetails(2, 2, sampleRate, blockSize);
            processor.prepareToPlay(sampleRate, blockSize);

            expectEquals(processor.getLatencySamples(), 0);
        }

        beginTest("the very first block is at full level, not ramping up");
        {
            // Regression test. The engine smooths its gains and its pre-delay, and prepareToPlay
            // used to leave those ramps at zero - so the wet signal faded in over the first 20 ms
            // of every transport start and an impulse escaped before Pre-Delay had moved at all.
            // Both unit suites missed it because they call engine.reset() by hand; it took
            // analysis/validate.py rendering actual audio to surface it. The fix is
            // applyParametersToEngine() + reset() in prepareToPlay.
            ConvolutionProcessor processor(variantConfig());

            // Dirac IR, wet only: convolution is then an identity, so the output should be the
            // input untouched from the very first sample.
            processor.apvts.getParameter(ConvolutionProcessor::irIndexParamID)
                ->setValueNotifyingHost(1.0f); // last entry in the table is the Dirac
            setRaw(processor, ConvolutionProcessor::dryParamID, 0.0f);
            setRaw(processor, ConvolutionProcessor::wetParamID, 100.0f);

            processor.setPlayConfigDetails(1, 1, sampleRate, blockSize);
            processor.prepareToPlay(sampleRate, blockSize);

            juce::AudioBuffer<float> buffer(1, blockSize);
            buffer.clear();
            buffer.setSample(0, 0, 1.0f);

            juce::MidiBuffer midi;
            processor.processBlock(buffer, midi);

            expectWithinAbsoluteError(buffer.getSample(0, 0), 1.0f, 0.01f,
                                      "the first sample was attenuated by an unsettled gain ramp");
        }

        beginTest("pre-delay applies from the first block");
        {
            ConvolutionProcessor processor(variantConfig());

            processor.apvts.getParameter(ConvolutionProcessor::irIndexParamID)->setValueNotifyingHost(1.0f);
            setRaw(processor, ConvolutionProcessor::dryParamID, 0.0f);
            setRaw(processor, ConvolutionProcessor::wetParamID, 100.0f);
            setRaw(processor, ConvolutionProcessor::preDelayMsParamID, 5.0f);

            processor.setPlayConfigDetails(1, 1, sampleRate, blockSize);
            processor.prepareToPlay(sampleRate, blockSize);

            // Rendered across two blocks and searched as one span, so the assertion does not
            // depend on which block the onset happens to land in.
            juce::MidiBuffer midi;
            std::vector<float> rendered;

            for (int block = 0; block < 2; ++block)
            {
                juce::AudioBuffer<float> buffer(1, blockSize);
                buffer.clear();
                if (block == 0)
                    buffer.setSample(0, 0, 1.0f);

                processor.processBlock(buffer, midi);

                for (int i = 0; i < blockSize; ++i)
                    rendered.push_back(buffer.getSample(0, i));
            }

            expect(std::abs(rendered[0]) < 0.01f, "the impulse escaped before pre-delay was applied");

            auto peakIndex = 0;
            auto peak = 0.0f;
            for (int i = 0; i < (int) rendered.size(); ++i)
            {
                if (std::abs(rendered[(size_t) i]) > peak)
                {
                    peak = std::abs(rendered[(size_t) i]);
                    peakIndex = i;
                }
            }

            const auto expectedIndex = (int) std::lround(0.005 * sampleRate);
            expect(std::abs(peakIndex - expectedIndex) <= 1,
                   "onset at sample " + juce::String(peakIndex) + ", expected " + juce::String(expectedIndex));
            expectWithinAbsoluteError(peak, 1.0f, 0.05f);
        }

        beginTest("produces stereo output from a mono input");
        {
            ConvolutionProcessor processor(variantConfig());
            processor.setPlayConfigDetails(1, 2, sampleRate, blockSize);
            processor.prepareToPlay(sampleRate, blockSize);

            juce::AudioBuffer<float> buffer(2, blockSize);
            buffer.clear();
            fillSine(buffer, 0.5f, 440.0f);
            buffer.clear(1, 0, blockSize); // only channel 0 carries the mono input

            juce::MidiBuffer midi;
            processor.processBlock(buffer, midi);

            expect(rms(buffer, 1) > 0.0f, "the right channel was left silent by a mono input");
        }

        beginTest("a full run at defaults passes signal and stays finite");
        {
            ConvolutionProcessor processor(variantConfig());
            processor.setPlayConfigDetails(2, 2, sampleRate, blockSize);
            processor.prepareToPlay(sampleRate, blockSize);

            juce::MidiBuffer midi;
            auto sawAnyOutput = false;

            for (int block = 0; block < 40; ++block)
            {
                juce::AudioBuffer<float> buffer(2, blockSize);
                fillSine(buffer, 0.5f, 220.0f);
                processor.processBlock(buffer, midi);

                for (int channel = 0; channel < 2; ++channel)
                {
                    const auto* samples = buffer.getReadPointer(channel);
                    for (int i = 0; i < blockSize; ++i)
                    {
                        expect(std::isfinite(samples[i]), "non-finite sample in block " + juce::String(block));
                        if (std::abs(samples[i]) > 1.0e-6f)
                            sawAnyOutput = true;
                    }
                }
            }

            expect(sawAnyOutput, "the processor produced silence at its defaults");
        }

        beginTest("tolerates a block larger than the one it was prepared for, and actually processes all of it");
        {
            // Logic's offline bounce does exactly this. The engine is sized with headroom (4x
            // samplesPerBlock - see blockSizeHeadroom) rather than reallocating on the audio
            // thread; the contract is that an oversized block is chunked through the engine, not
            // that everything past the first chunk is silently left dry-unmixed. Using 8x the
            // prepared block size here (not 2x, which fits inside the 4x headroom and so never
            // actually exercised the oversized path at all) so the buffer genuinely exceeds a
            // single chunk.
            ConvolutionProcessor processor(variantConfig());
            setRaw(processor, ConvolutionProcessor::dryParamID, 0.0f);
            setRaw(processor, ConvolutionProcessor::wetParamID, 100.0f);
            processor.setPlayConfigDetails(2, 2, sampleRate, blockSize);
            processor.prepareToPlay(sampleRate, blockSize);

            juce::AudioBuffer<float> buffer(2, blockSize * 8);
            fillSine(buffer, 0.5f, 220.0f);

            juce::MidiBuffer midi;
            processor.processBlock(buffer, midi);

            for (int i = 0; i < buffer.getNumSamples(); ++i)
                expect(std::isfinite(buffer.getSample(0, i)));

            // With Dry at 0%, any sample the engine never touched stays exactly 0 (that channel's
            // own dry contribution is zeroed out, and nothing else writes it) - so a non-silent
            // second half is direct evidence the engine actually ran on it, not just that nothing
            // crashed reading it.
            const auto secondHalfRms = rms(juce::AudioBuffer<float>(
                buffer.getArrayOfWritePointers(), 2, blockSize * 4, blockSize * 4), 0);
            expect(secondHalfRms > 0.0f,
                   "samples past the first chunk were left unprocessed by an oversized block");
        }

        beginTest("state round-trips through the host's save/restore");
        {
            ConvolutionProcessor saved(variantConfig());

            saved.apvts.getParameter(ConvolutionProcessor::preDelayMsParamID)->setValueNotifyingHost(0.4f);
            saved.apvts.getParameter(ConvolutionProcessor::lengthPercentParamID)->setValueNotifyingHost(0.55f);
            saved.apvts.getParameter(ConvolutionProcessor::wetParamID)->setValueNotifyingHost(0.7f);

            juce::MemoryBlock state;
            saved.getStateInformation(state);

            ConvolutionProcessor restored(variantConfig());
            restored.setStateInformation(state.getData(), (int) state.getSize());

            for (auto* id : { ConvolutionProcessor::preDelayMsParamID,
                              ConvolutionProcessor::lengthPercentParamID,
                              ConvolutionProcessor::wetParamID })
            {
                expectWithinAbsoluteError(restored.apvts.getRawParameterValue(id)->load(),
                                          saved.apvts.getRawParameterValue(id)->load(), 1.0e-4f);
            }
        }

        beginTest("the IR dropdown offers exactly the variant's IRs");
        {
            ConvolutionProcessor processor(variantConfig());

            auto* irParam = dynamic_cast<juce::AudioParameterChoice*>(
                processor.apvts.getParameter(ConvolutionProcessor::irIndexParamID));

            expect(irParam != nullptr);
            expectEquals(irParam->choices.size(), (int) variantConfig().irs.size());

            for (int i = 0; i < irParam->choices.size(); ++i)
                expectEquals(irParam->choices[i], juce::String(variantConfig().irs[(size_t) i].displayName));
        }

        beginTest("a shape request completed after the session's sample rate has moved on is discarded, not delivered");
        {
            // Exercises IRLoadWorker::setSessionSampleRate() directly: request a re-shape at the
            // OLD rate, then tell the worker the session has moved to a NEW rate before the
            // request's debounce period elapses (IRLoadWorker.cpp's debounceMs is 80ms) - the same
            // sequence a host re-prepare landing mid-shape produces. Without the fix, the worker has
            // no way to know the rate it was asked to shape for is now stale, and would deliver it
            // anyway; loadIR() stamps whatever it's handed as the CURRENT rate with no resampling.
            ConvolutionProcessor processor(variantConfig());
            processor.setPlayConfigDetails(2, 2, sampleRate, blockSize);
            processor.prepareToPlay(sampleRate, blockSize);

            auto& worker = processor.getIRLoadWorker();

            worker.requestShape(1, { 100.0f, 0.0f }, sampleRate); // old rate: 48000
            worker.setSessionSampleRate(sampleRate * 2.0); // session moves to 96000 before delivery

            juce::Thread::sleep(400); // clears the debounce plus the decode/resample/shape pass

            juce::AudioBuffer<float> delivered;
            expect(! worker.tryPopShapedIR(delivered),
                   "an IR shaped for a stale sample rate was delivered after the session moved on");

            // The worker is otherwise healthy - a fresh request at the NEW (current) rate still
            // delivers normally, so this isn't a case of the fix wedging the pipeline.
            worker.requestShape(1, { 100.0f, 0.0f }, sampleRate * 2.0);
            juce::Thread::sleep(400);
            expect(worker.tryPopShapedIR(delivered),
                   "a request matching the current session rate was not delivered");
        }
    }
};

static ConvolutionProcessorTests convolutionProcessorTests;
