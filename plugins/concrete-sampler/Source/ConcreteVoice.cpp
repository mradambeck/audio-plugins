#include "ConcreteVoice.h"

#include <cmath>

void ConcreteVoice::prepare(double sampleRateIn) noexcept
{
    sampleRate = sampleRateIn;
    adsr.setSampleRate(sampleRate);
    contourEnvelope.setSampleRate(sampleRate);
    filterEnvelope.setSampleRate(sampleRate);
    for (auto& engine : pitchEngines)
        engine.prepare(sampleRate);
    for (auto& filter : filters)
        filter.prepare(sampleRate);
}

void ConcreteVoice::startNote(ConcreteSampleSet::Ptr set, int zoneIndex, int midiNote, float velocity01,
                                ConcretePitchEngine::Mode mode, double effectiveSourceRateHzIn,
                                int coarseTuneSemitones, float fineTuneCents, bool autoCompensate,
                                ConcreteFilterModel::Mode filterModeIn, float filterCutoffHz,
                                float filterResonance01In, float filterEnvAmountOctavesIn,
                                float filterKeyTrack01, ConcreteAmpEnvelopeMode ampEnvelopeModeIn) noexcept
{
    ampEnvelopeMode = ampEnvelopeModeIn;
    sampleSet = set;
    zone = &sampleSet->zones[(size_t) zoneIndex];
    currentMidiNote = midiNote;
    velocityGain = velocity01;
    pitchMode = mode;
    effectiveSourceRateHz = effectiveSourceRateHzIn;

    filterMode = filterModeIn;
    filterBaseCutoffHz = filterCutoffHz;
    filterResonance01 = filterResonance01In;
    filterEnvAmountOctaves = filterEnvAmountOctavesIn;
    // A fixed per-note octave offset, exactly like pitch's own semitone math - key tracking makes
    // the cutoff move with the played note the same way pitch does, scaled by filterKeyTrack01
    // (0 = no tracking, 1 = full 1:1 tracking), referenced against C3 (60) to match every other
    // "distance from root/reference" calculation in this plugin.
    filterKeyTrackOctaveOffset = (float) (midiNote - 60) / 12.0f * filterKeyTrack01;
    for (auto& filter : filters)
        filter.reset();

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

    const auto compensationSemitones = autoCompensate ? zone->captureTransposeSemitones : 0.0;
    const auto totalSemitones = (double) (midiNote - zone->rootNote) + (double) zone->tuneSemitones
                               + (double) coarseTuneSemitones + (double) fineTuneCents / 100.0
                               - compensationSemitones;
    pitchRatio = std::pow(2.0, totalSemitones / 12.0);

    for (auto& engine : pitchEngines)
        engine.start((double) zone->start);

    // Fixed, basic default ADSR shape - flat sustain, matching every earlier phase. Attack is
    // short but non-zero purely to avoid a hard-edge click on a full-scale sample. Velocity
    // sensitivity is the separate, flat linear velocityGain applied in renderNextBlock() below,
    // not an ADSR parameter - real per-voice velocity-to-envelope shaping (e.g. velocity affecting
    // attack/decay time, not just level) is a reasonable future refinement beyond Phase 6's scope.
    adsr.setParameters({ 0.002f, 0.0f, 1.0f, 0.05f });
    // Only one of these drives env below (see the isActive()/getNextSample() dispatch in
    // renderNextBlock()), but explicitly resetting the OTHER one too means a voice slot reused
    // across a mode change never carries stale isActive() state from its previous note.
    if (ampEnvelopeMode == ConcreteAmpEnvelopeMode::contoured)
    {
        adsr.reset();
        contourEnvelope.noteOn();
    }
    else
    {
        contourEnvelope.reset();
        adsr.noteOn();
    }

    // Fixed, basic filter envelope shape for Phase 5 - just like the amp envelope above, no APVTS
    // knobs expose ITS shape yet (only its overall depth, via filterEnvAmountOctaves - see Phase 6
    // for real per-voice envelope design). A more noticeable decay than the amp envelope's, since
    // this one exists specifically to produce an audible cutoff sweep (see the plan's Phase 5
    // "envelope-modulated cutoff" per-voice check).
    filterEnvelope.setParameters({ 0.01f, 0.3f, 0.3f, 0.2f });
    filterEnvelope.noteOn();
}

void ConcreteVoice::stopNote(bool allowTailOff, bool isForced) noexcept
{
    // An ordinary (non-forced) note-off on a one-shot zone is a no-op: the zone plays through to
    // its own end (see renderNextBlock()'s zoneEndSample check) regardless of when, or whether,
    // note-off ever arrives. zone can be null (a missing-source zone that was never actually
    // activated - see startNote()), in which case there's nothing playing to leave alone either way.
    if (!isForced && zone != nullptr && zone->oneShot)
        return;

    if (allowTailOff)
    {
        adsr.noteOff();
        contourEnvelope.noteOff();
        filterEnvelope.noteOff();
    }
    else
    {
        adsr.reset();
        contourEnvelope.reset();
        filterEnvelope.reset();
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

    const auto usingContourEnvelope = ampEnvelopeMode == ConcreteAmpEnvelopeMode::contoured;

    for (int i = 0; i < numSamples; ++i)
    {
        const auto ampEnvelopeActive = usingContourEnvelope ? contourEnvelope.isActive() : adsr.isActive();
        if (!ampEnvelopeActive)
        {
            active = false;
            break;
        }

        // Loop handling: zone->loopEnabled was carried on every zone since Architecture #1's
        // schema, but nothing ever read it before now - the DSP always just stopped at
        // zoneEndSample regardless, which is exactly what made the Loop button look like it did
        // nothing. loopEnd is clamped against zoneEndSample too, since a zone's own start/end trim
        // can be narrower than the loop points a file was originally saved with.
        const auto loopActive = zone->loopEnabled && zone->loopEnd > zone->loopStart;
        const auto playbackEndSample = loopActive ? (double) juce::jmin(zone->loopEnd, zoneEndSample)
                                                   : (double) zoneEndSample;
        if (pitchEngines[0].getSourcePhase() >= playbackEndSample)
        {
            if (!loopActive)
            {
                active = false;
                break;
            }

            const auto loopLength = playbackEndSample - (double) zone->loopStart;
            for (auto& engine : pitchEngines)
                engine.rewindSourcePhase(loopLength);
        }

        const auto ampEnvelopeValue = usingContourEnvelope ? contourEnvelope.getNextSample() : adsr.getNextSample();
        const auto env = ampEnvelopeValue * velocityGain * zone->level;

        // One filter-envelope/cutoff computation per sample, shared by every output channel's own
        // filter instance below - key tracking and envelope amount both modulate the SAME base
        // cutoff, combined as one octave offset (see startNote()'s own comment). Clamped well
        // short of Nyquist for stability across all three filter implementations, at any of the
        // host rates this plugin supports (44.1/48/96kHz).
        const auto filterEnvValue = filterEnvelope.getNextSample();
        const auto cutoffOctaveOffset = filterEnvAmountOctaves * filterEnvValue + filterKeyTrackOctaveOffset;
        const auto modulatedCutoffHz = juce::jlimit(20.0f, (float) (sampleRate * 0.45),
                                                      filterBaseCutoffHz * std::pow(2.0f, cutoffOctaveOffset));

        if (outChannels <= 1)
        {
            float mix = 0.0f;
            for (int srcCh = 0; srcCh < zoneChannels; ++srcCh)
                mix += pitchEngines[(size_t) srcCh].processSample(pitchMode, buf.getReadPointer(srcCh), bufferLength,
                                                                     pitchRatio, effectiveSourceRateHz, zone->sourceSampleRate);
            mix /= (float) zoneChannels;
            mix = filters[0].processSample(filterMode, mix, modulatedCutoffHz, filterResonance01);
            outputBuffer.addSample(0, startSample + i, mix * env);
        }
        else if (zoneChannels <= 1)
        {
            // Mono zone -> replicate to every output channel. The engine is stateful (Mode B/C
            // carry history - see ConcretePitchEngine.h), so it must be advanced exactly ONCE per
            // sample here, then the same value broadcast to every channel - calling it once per
            // OUTPUT channel (as the stereo-zone branch below does) would double-advance it. Each
            // output channel still gets its OWN filter instance, even though they're fed the same
            // input - matches the per-channel filtering the stereo branch below does.
            const auto sampleValue = pitchEngines[0].processSample(pitchMode, buf.getReadPointer(0), bufferLength,
                                                                     pitchRatio, effectiveSourceRateHz, zone->sourceSampleRate);
            for (int ch = 0; ch < outChannels; ++ch)
            {
                const auto filtered = filters[(size_t) ch].processSample(filterMode, sampleValue, modulatedCutoffHz, filterResonance01);
                outputBuffer.addSample(ch, startSample + i, filtered * env);
            }
        }
        else
        {
            for (int ch = 0; ch < outChannels; ++ch)
            {
                const auto srcChannel = juce::jmin(ch, zoneChannels - 1);
                auto sampleValue = pitchEngines[(size_t) srcChannel].processSample(
                    pitchMode, buf.getReadPointer(srcChannel), bufferLength, pitchRatio, effectiveSourceRateHz, zone->sourceSampleRate);
                sampleValue = filters[(size_t) ch].processSample(filterMode, sampleValue, modulatedCutoffHz, filterResonance01);
                outputBuffer.addSample(ch, startSample + i, sampleValue * env);
            }
        }
    }
}
