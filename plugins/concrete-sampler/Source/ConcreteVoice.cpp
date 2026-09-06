#include "ConcreteVoice.h"

#include <cmath>

void ConcreteVoice::prepare(double sampleRateIn) noexcept
{
    sampleRate = sampleRateIn;
    adsr.setSampleRate(sampleRate);
    for (auto& engine : pitchEngines)
        engine.prepare(sampleRate);
}

void ConcreteVoice::startNote(ConcreteSampleSet::Ptr set, int zoneIndex, int midiNote, float velocity01,
                                ConcretePitchEngine::Mode mode, double effectiveSourceRateHzIn,
                                int coarseTuneSemitones, float fineTuneCents) noexcept
{
    sampleSet = set;
    zone = &sampleSet->zones[(size_t) zoneIndex];
    currentMidiNote = midiNote;
    velocityGain = velocity01;
    pitchMode = mode;
    effectiveSourceRateHz = effectiveSourceRateHzIn;

    // A zone whose source is missing (Architecture #2's relocate case) still exists in the set
    // with no buffer - allocate the voice slot (so note-on/note-off bookkeeping stays correct)
    // but never render anything.
    if (zone->buffer == nullptr || zone->buffer->getNumSamples() <= 0)
    {
        active = false;
        return;
    }

    active = true;
    const auto bufferLength = (juce::int64) zone->buffer->getNumSamples();
    zoneEndSample = zone->end > zone->start ? juce::jmin(zone->end, bufferLength) : bufferLength;

    const auto totalSemitones = (double) (midiNote - zone->rootNote) + (double) zone->tuneSemitones
                               + (double) coarseTuneSemitones + (double) fineTuneCents / 100.0;
    pitchRatio = std::pow(2.0, totalSemitones / 12.0);

    for (auto& engine : pitchEngines)
        engine.start((double) zone->start);

    // Fixed, basic default envelope for Phase 1/2 - no APVTS knobs expose these yet (Phase 6 owns
    // per-voice envelope design, including velocity sensitivity beyond this flat linear gain).
    // Attack is short but non-zero purely to avoid a hard-edge click on a full-scale sample.
    adsr.setParameters({ 0.002f, 0.0f, 1.0f, 0.05f });
    adsr.noteOn();
}

void ConcreteVoice::stopNote(bool allowTailOff) noexcept
{
    if (allowTailOff)
    {
        adsr.noteOff();
    }
    else
    {
        adsr.reset();
        active = false;
    }
}

void ConcreteVoice::renderNextBlock(juce::AudioBuffer<float>& outputBuffer, int startSample, int numSamples) noexcept
{
    if (!active || zone == nullptr || zone->buffer == nullptr)
        return;

    const auto& buf = *zone->buffer;
    const auto zoneChannels = buf.getNumChannels();
    const auto outChannels = outputBuffer.getNumChannels();
    const auto bufferLength = buf.getNumSamples();

    for (int i = 0; i < numSamples; ++i)
    {
        if (!adsr.isActive() || pitchEngines[0].getSourcePhase() >= (double) zoneEndSample)
        {
            active = false;
            break;
        }

        const auto env = adsr.getNextSample() * velocityGain * zone->level;

        if (outChannels <= 1)
        {
            float mix = 0.0f;
            for (int srcCh = 0; srcCh < zoneChannels; ++srcCh)
                mix += pitchEngines[(size_t) srcCh].processSample(pitchMode, buf.getReadPointer(srcCh), bufferLength,
                                                                     pitchRatio, effectiveSourceRateHz);
            mix /= (float) zoneChannels;
            outputBuffer.addSample(0, startSample + i, mix * env);
        }
        else if (zoneChannels <= 1)
        {
            // Mono zone -> replicate to every output channel. The engine is stateful (Mode B/C
            // carry history - see ConcretePitchEngine.h), so it must be advanced exactly ONCE per
            // sample here, then the same value broadcast to every channel - calling it once per
            // OUTPUT channel (as the stereo-zone branch below does) would double-advance it.
            const auto sampleValue = pitchEngines[0].processSample(pitchMode, buf.getReadPointer(0), bufferLength,
                                                                     pitchRatio, effectiveSourceRateHz);
            for (int ch = 0; ch < outChannels; ++ch)
                outputBuffer.addSample(ch, startSample + i, sampleValue * env);
        }
        else
        {
            for (int ch = 0; ch < outChannels; ++ch)
            {
                const auto srcChannel = juce::jmin(ch, zoneChannels - 1);
                const auto sampleValue = pitchEngines[(size_t) srcChannel].processSample(
                    pitchMode, buf.getReadPointer(srcChannel), bufferLength, pitchRatio, effectiveSourceRateHz);
                outputBuffer.addSample(ch, startSample + i, sampleValue * env);
            }
        }
    }
}
