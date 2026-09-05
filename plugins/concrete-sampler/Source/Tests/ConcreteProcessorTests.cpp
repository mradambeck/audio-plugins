#include "../PluginProcessor.h"

// Phase 0 has no DSP yet (no sample zones, no voices - see concrete-sampler-plugin-plan.md), so
// there's nothing framework-free for ConcreteTests to exercise. These tests instead drive the real
// ConcreteAudioProcessor - the exact class the plugin ships - through prepareToPlay()/
// processBlock()/getStateInformation(), the same path a host actually takes, and lock down the
// specific scaffold behaviors this phase is responsible for: silent output, the mono/stereo bus
// decision (Architecture #1), and a working state round-trip even with zero parameters. Every one
// of these must still hold once Phase 1 adds real DSP, so none of this is throwaway.
class ConcreteProcessorTests : public juce::UnitTest
{
public:
    ConcreteProcessorTests() : juce::UnitTest("ConcreteProcessor", "Concrete") {}

    void runTest() override
    {
        beginTest("Mono and stereo main output are both accepted");
        {
            ConcreteAudioProcessor processor;
            using Layout = juce::AudioProcessor::BusesLayout;

            Layout mono;
            mono.outputBuses.add(juce::AudioChannelSet::mono());
            expect(processor.isBusesLayoutSupported(mono), "mono main output should be supported");

            Layout stereo;
            stereo.outputBuses.add(juce::AudioChannelSet::stereo());
            expect(processor.isBusesLayoutSupported(stereo), "stereo main output should be supported");
        }

        beginTest("Other main output layouts are rejected (main stereo only - see Architecture #1)");
        {
            ConcreteAudioProcessor processor;
            using Layout = juce::AudioProcessor::BusesLayout;

            Layout quad;
            quad.outputBuses.add(juce::AudioChannelSet::quadraphonic());
            expect(! processor.isBusesLayoutSupported(quad), "quadraphonic main output should be rejected");

            Layout disabled;
            disabled.outputBuses.add(juce::AudioChannelSet::disabled());
            expect(! processor.isBusesLayoutSupported(disabled), "a disabled main output should be rejected");
        }

        beginTest("processBlock produces silence with no sample loaded, even with MIDI in");
        {
            ConcreteAudioProcessor processor;
            processor.prepareToPlay(44100.0, 512);

            juce::AudioBuffer<float> buffer(2, 512);
            buffer.clear();
            // Fill with non-zero content first so a real clear(), not a no-op, is what's verified.
            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                juce::FloatVectorOperations::fill(buffer.getWritePointer(ch), 1.0f, buffer.getNumSamples());

            juce::MidiBuffer midi;
            midi.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8) 100), 0);

            processor.processBlock(buffer, midi);

            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                for (int i = 0; i < buffer.getNumSamples(); ++i)
                    expectEquals(buffer.getSample(ch, i), 0.0f, "output must be exact silence in Phase 0");

            processor.releaseResources();
        }

        beginTest("getStateInformation()/setStateInformation() round-trip without corrupting state");
        {
            ConcreteAudioProcessor processor;

            juce::MemoryBlock block;
            processor.getStateInformation(block);
            expect(block.getSize() > 0, "state XML should be non-empty even with zero parameters");

            ConcreteAudioProcessor other;
            other.setStateInformation(block.getData(), (int) block.getSize());
            expect(other.apvts.state.getType() == processor.apvts.state.getType(),
                   "reloaded state should keep the same ValueTree type");
        }

        beginTest("No factory presets exist yet (Phase 7 defines the twelve machines)");
        {
            ConcreteAudioProcessor processor;
            expectEquals(processor.getNumPrograms(), 0);
        }
    }
};

static ConcreteProcessorTests concreteProcessorTests;
