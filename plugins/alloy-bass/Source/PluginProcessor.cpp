#include "PluginProcessor.h"

#include <algorithm>
#include <cmath>

namespace
{
    // Choice index 0 is the highest octave in both lists (+2 / -1) - the dropdowns list values
    // top-to-bottom from highest to lowest, so index and octave shift move in opposite directions.
    constexpr int analogOctaveChoiceCount = 5;   // +2 .. -2
    constexpr int analogOctaveDefaultIndex = 3;  // "-1", matching the reference sound
    constexpr int subOctaveChoiceCount = 4;      // 0 .. -3
    constexpr int subOctaveDefaultIndex = 2;     // "-2", matching the reference sound
    constexpr int unisonDefaultIndex = 1;        // "2" voices

    // Max detune spread (each unison voice offset by up to +/- half of this, in cents) at
    // Detune = 100%.
    constexpr float maxDetuneCents = 25.0f;

    // How far the filter envelope/velocity can each push the cutoff above its base position,
    // in octaves - additive/one-directional (opening the filter), not bipolar, matching how an
    // analog bass filter envelope is normally used.
    constexpr float filterEnvMaxOctaves = 5.0f;
    constexpr float filterVelocityMaxOctaves = 3.0f;

    constexpr float maxGlideSeconds = 1.0f;

    // Note subdivisions offered for arp tempo sync, same convention as the Sync/Division control
    // already established in Flux, just driving a step rate instead of an LFO.
    struct Subdivision
    {
        const char* label;
        float quarterNoteMultiple;
    };

    constexpr Subdivision arpSubdivisions[] = {
        { "1/1", 4.0f },
        { "1/2.", 3.0f },
        { "1/2", 2.0f },
        { "1/2T", 4.0f / 3.0f },
        { "1/4.", 1.5f },
        { "1/4", 1.0f },
        { "1/4T", 2.0f / 3.0f },
        { "1/8.", 0.75f },
        { "1/8", 0.5f },
        { "1/8T", 1.0f / 3.0f },
        { "1/16.", 0.375f },
        { "1/16", 0.25f },
        { "1/16T", 1.0f / 6.0f },
        { "1/32", 0.125f },
    };

    constexpr int arpDefaultDivisionIndex = 11; // "1/16" - a typical arp step rate

    enum class ArpPattern
    {
        up,
        down,
        upDown,
        random,
        asPlayed
    };

    float noteToHz(float semitoneNote) noexcept
    {
        return 440.0f * std::pow(2.0f, (semitoneNote - 69.0f) / 12.0f);
    }

    // getSubWaveformChoices() is {"Square","Sine","Triangle"} (0,1,2) - a different order from
    // AlloyOscWaveform's own enumerators, so the combo index can't just be cast directly.
    AlloyOscWaveform subWaveformForChoiceIndex(int index) noexcept
    {
        switch (index)
        {
            case 1:  return AlloyOscWaveform::sine;
            case 2:  return AlloyOscWaveform::triangle;
            default: return AlloyOscWaveform::square;
        }
    }

    // getFmWaveformChoices() is {"Sine","Triangle","Square"} (0,1,2) - again a different order
    // from AlloyOscWaveform's own enumerators.
    AlloyOscWaveform fmWaveformForChoiceIndex(int index) noexcept
    {
        switch (index)
        {
            case 1:  return AlloyOscWaveform::triangle;
            case 2:  return AlloyOscWaveform::square;
            default: return AlloyOscWaveform::sine;
        }
    }

    // Ceiling for the modulator's phase-offset contribution to the carrier at 100% Modulator
    // Volume, in cycles - Modulator Volume doubles as the FM depth/index here (per the user's
    // original control spec: no separate hidden "FM amount" knob). Deliberately wide: EBM/
    // industrial FM basses lean on harsh, aliasy, "metallic" tones, not clean/subtle FM.
    constexpr float fmMaxModulationIndex = 6.0f;

    // One-pole smoothing time for the Modulator envelope before it scales phase-modulation depth
    // - short enough to preserve a fast Decay's punch, long enough to remove the audible click a
    // sharp envelope-slope kink would otherwise put into the carrier's instantaneous frequency.
    constexpr float fmModulatorEnvSmoothingSeconds = 0.003f;

    // Max multiplicative boost at Velocity-To-* = 100% and velocity = 127 - e.g. 1.5 means the
    // loudest notes can reach up to 2.5x (+8dB) the knob-set Carrier Volume / Modulator Volume
    // (Modulator Volume doubles as FM depth, so boosting it is what "brighter" means here - more
    // depth means more sidebands/harmonic content).
    constexpr float velToFmMaxBoost = 1.5f;

    // FM Modulator Brightness: a waveshaper on the raw Modulator oscillator itself, independent
    // of Modulator Volume (overall depth) and Velocity To Brightness (velocity's contribution to
    // depth) - this instead reshapes the waveform's own harmonic content before either of those
    // apply, so it brightens/enriches the modulator's timbre at a fixed depth rather than just
    // making the FM effect stronger. Same tanh(k*x)/k + makeup pattern as Mix Drive, for the same
    // reason (keeps x=0 an exact passthrough while still gaining perceived loudness back as
    // saturation increases).
    constexpr float fmBrightnessMaxK = 8.0f;
    constexpr float fmBrightnessMakeupRange = 4.0f;

    // Mix bus: Drive ceiling and its makeup gain, same tanh(k*x)/k + separate makeup-gain
    // pattern (and the same reason for it) as Flux's Grit knob - tanh(k*x)/k alone keeps slope
    // 1 at x=0 (so Drive=0% is an exact passthrough) but its *saturated* ceiling shrinks as k
    // grows, which without a separate makeup gain made cranking the knob sound weaker instead
    // of more aggressive.
    constexpr float mixDriveMaxK = 10.0f;
    constexpr float mixDriveMakeupRange = 5.0f;

    constexpr float mixToneMinHz = 200.0f;
    constexpr float mixToneMaxHz = 20000.0f;

    // Fixed peaking cut layered on top of the Tone low-pass, deepening as Tone moves toward 0%
    // (left) - 0dB at Tone=100%, mixToneEqMaxCutDb at Tone=0%.
    constexpr float mixToneEqHz = 210.0f;
    constexpr float mixToneEqQ = 1.20f;
    constexpr float mixToneEqMaxCutDb = -2.5f;

    // Fixed final-stage safety soft-clip, not user-controllable - Drive + a hot FM layer +
    // resonance can stack up past 0dBFS, and this is what keeps that from turning into harsh
    // digital clipping rather than a further-saturated but still-musical peak.
    constexpr float mixSafetyDrive = 1.3f;

    // Fixed master trim, not tied to the Output knob - the plugin was sitting too hot overall,
    // so this pulls everything down 5dB regardless of where Output is set, rather than just
    // changing Output's default (which a user could still turn back up to the old level anyway).
    constexpr float masterOutputTrimDb = -5.0f;

    // Fixed high shelf tied to the Sub oscillator, engaged only while its Waveform is Square -
    // 0dB for Sine/Triangle, subShelfGainDb when Square.
    constexpr float subShelfHz = 280.0f;
    constexpr float subShelfQ = 0.40f;
    constexpr float subShelfGainDb = -4.0f;

    // Age (Mix section): two one-pole-lowpassed noise components applied to the Analog VCO's
    // pitch - a slow "drift" and a faster "warble" - each scaled by its own max depth (in cents)
    // at Age=100%, then by the live Age amount.
    constexpr float ageDriftCutoffHz = 0.15f;
    constexpr float ageWarbleCutoffHz = 4.0f;
    // Ceilings are deliberately extreme (a fully-aged VCO should sound clearly broken/unstable,
    // not just faintly chorused) - a squared response curve on the knob (see ageAmount in
    // processBlock) keeps the low end subtle despite the high ceiling.
    constexpr float ageMaxDriftCents = 35.0f;
    constexpr float ageMaxWarbleCents = 20.0f;

    // Factory presets: raw parameter values (the same values setValueNotifyingHost() takes after
    // normalising, not display percentages) applied in one shot when the preset is selected.
    const std::vector<wildjag::FactoryPreset>& getFactoryPresets()
    {
        static const std::vector<wildjag::FactoryPreset> presets = {
            { "Chant", {
                { AlloyAudioProcessor::analogAmpAttackParamID, 0.01100000087171793f },
                { AlloyAudioProcessor::analogAmpDecayParamID, 0.2800000011920929f },
                { AlloyAudioProcessor::analogAmpReleaseParamID, 0.0390000008046627f },
                { AlloyAudioProcessor::analogAmpSustainParamID, 27.0f },
                { AlloyAudioProcessor::analogDetuneParamID, 11.69999980926514f },
                { AlloyAudioProcessor::analogFilterAttackParamID, 0.004999999888241291f },
                { AlloyAudioProcessor::analogFilterCutoffParamID, 355.0f },
                { AlloyAudioProcessor::analogFilterDecayParamID, 0.0650000050663948f },
                { AlloyAudioProcessor::analogFilterEnvAmountParamID, 55.5f },
                { AlloyAudioProcessor::analogFilterReleaseParamID, 0.0430000014603138f },
                { AlloyAudioProcessor::analogFilterResonanceParamID, 10.69999980926514f },
                { AlloyAudioProcessor::analogFilterSustainParamID, 35.70000076293945f },
                { AlloyAudioProcessor::analogGlideTimeParamID, 0.0f },
                { AlloyAudioProcessor::analogOctaveParamID, 3.0f },
                { AlloyAudioProcessor::analogUnisonParamID, 1.0f },
                { AlloyAudioProcessor::analogVelocityToFilterParamID, 72.30000305175781f },
                { AlloyAudioProcessor::analogVolumeParamID, 36.29999923706055f },
                { AlloyAudioProcessor::analogWaveformParamID, 0.0f },
                { AlloyAudioProcessor::arpDivisionParamID, 11.0f },
                { AlloyAudioProcessor::arpEnabledParamID, 0.0f },
                { AlloyAudioProcessor::arpGateParamID, 50.29999923706055f },
                { AlloyAudioProcessor::arpHoldParamID, 0.0f },
                { AlloyAudioProcessor::arpOctaveRangeParamID, 1.0f },
                { AlloyAudioProcessor::arpPatternParamID, 0.0f },
                { AlloyAudioProcessor::arpRateParamID, 5.889999866485596f },
                { AlloyAudioProcessor::arpSyncParamID, 0.0f },
                { AlloyAudioProcessor::fmCarrierAttackParamID, 0.001000000047497451f },
                { AlloyAudioProcessor::fmCarrierDecayParamID, 0.02800000086426735f },
                { AlloyAudioProcessor::fmCarrierOctaveParamID, 2.0f },
                { AlloyAudioProcessor::fmCarrierReleaseParamID, 0.08800000697374344f },
                { AlloyAudioProcessor::fmCarrierSustainParamID, 18.20000076293945f },
                { AlloyAudioProcessor::fmCarrierVolumeParamID, 20.60000038146973f },
                { AlloyAudioProcessor::fmCarrierWaveformParamID, 1.0f },
                { AlloyAudioProcessor::fmModulatorAttackParamID, 0.006000000052154064f },
                { AlloyAudioProcessor::fmModulatorDecayParamID, 0.1790000051259995f },
                { AlloyAudioProcessor::fmModulatorOctaveParamID, 3.0f },
                { AlloyAudioProcessor::fmModulatorReleaseParamID, 0.8080000281333923f },
                { AlloyAudioProcessor::fmModulatorSustainParamID, 22.70000076293945f },
                { AlloyAudioProcessor::fmModulatorVolumeParamID, 38.40000152587891f },
                { AlloyAudioProcessor::fmModulatorWaveformParamID, 1.0f },
                { AlloyAudioProcessor::mixDriveParamID, 17.70000076293945f },
                { AlloyAudioProcessor::mixOutputParamID, -4.099999904632568f },
                { AlloyAudioProcessor::mixToneParamID, 62.70000076293945f },
                { AlloyAudioProcessor::subEnabledParamID, 1.0f },
                { AlloyAudioProcessor::subOctaveParamID, 1.0f },
                { AlloyAudioProcessor::subVolumeParamID, 11.30000019073486f },
                { AlloyAudioProcessor::subWaveformParamID, 2.0f },
                { AlloyAudioProcessor::mixAgeParamID, 18.60000038146973f },
                { AlloyAudioProcessor::fmVelocityToBrightnessParamID, 43.5f },
                { AlloyAudioProcessor::fmVelocityToCarrierParamID, 66.0999984741211f },
                { AlloyAudioProcessor::fmModulatorBrightnessParamID, 11.69999980926514f },
            } },
            { "I Fight for the Users", {
                { AlloyAudioProcessor::analogAmpAttackParamID, 0.001000000047497451f },
                { AlloyAudioProcessor::analogAmpDecayParamID, 1.943000078201294f },
                { AlloyAudioProcessor::analogAmpReleaseParamID, 0.08000000566244125f },
                { AlloyAudioProcessor::analogAmpSustainParamID, 68.0999984741211f },
                { AlloyAudioProcessor::analogDetuneParamID, 56.90000152587891f },
                { AlloyAudioProcessor::analogFilterAttackParamID, 0.001000000047497451f },
                { AlloyAudioProcessor::analogFilterCutoffParamID, 281.0f },
                { AlloyAudioProcessor::analogFilterDecayParamID, 1.617000102996826f },
                { AlloyAudioProcessor::analogFilterEnvAmountParamID, 72.70000457763672f },
                { AlloyAudioProcessor::analogFilterReleaseParamID, 0.08000000566244125f },
                { AlloyAudioProcessor::analogFilterResonanceParamID, 33.40000152587891f },
                { AlloyAudioProcessor::analogFilterSustainParamID, 31.89999961853027f },
                { AlloyAudioProcessor::analogGlideTimeParamID, 0.0390000008046627f },
                { AlloyAudioProcessor::analogOctaveParamID, 4.0f },
                { AlloyAudioProcessor::analogUnisonParamID, 1.0f },
                { AlloyAudioProcessor::analogVelocityToFilterParamID, 52.0f },
                { AlloyAudioProcessor::analogVolumeParamID, 64.0f },
                { AlloyAudioProcessor::analogWaveformParamID, 0.0f },
                { AlloyAudioProcessor::arpDivisionParamID, 11.0f },
                { AlloyAudioProcessor::arpEnabledParamID, 1.0f },
                { AlloyAudioProcessor::arpGateParamID, 93.0f },
                { AlloyAudioProcessor::arpHoldParamID, 0.0f },
                { AlloyAudioProcessor::arpOctaveRangeParamID, 1.0f },
                { AlloyAudioProcessor::arpPatternParamID, 3.0f },
                { AlloyAudioProcessor::arpRateParamID, 5.889999866485596f },
                { AlloyAudioProcessor::arpSyncParamID, 1.0f },
                { AlloyAudioProcessor::fmCarrierAttackParamID, 0.001000000047497451f },
                { AlloyAudioProcessor::fmCarrierDecayParamID, 0.2020000070333481f },
                { AlloyAudioProcessor::fmCarrierOctaveParamID, 3.0f },
                { AlloyAudioProcessor::fmCarrierReleaseParamID, 0.1000000014901161f },
                { AlloyAudioProcessor::fmCarrierSustainParamID, 29.80000114440918f },
                { AlloyAudioProcessor::fmCarrierVolumeParamID, 43.60000228881836f },
                { AlloyAudioProcessor::fmCarrierWaveformParamID, 0.0f },
                { AlloyAudioProcessor::fmModulatorAttackParamID, 0.001000000047497451f },
                { AlloyAudioProcessor::fmModulatorDecayParamID, 0.7490000128746033f },
                { AlloyAudioProcessor::fmModulatorOctaveParamID, 4.0f },
                { AlloyAudioProcessor::fmModulatorReleaseParamID, 0.05000000074505806f },
                { AlloyAudioProcessor::fmModulatorSustainParamID, 0.0f },
                { AlloyAudioProcessor::fmModulatorVolumeParamID, 35.10000228881836f },
                { AlloyAudioProcessor::fmModulatorWaveformParamID, 1.0f },
                { AlloyAudioProcessor::mixDriveParamID, 77.80000305175781f },
                { AlloyAudioProcessor::mixOutputParamID, -4.099999904632568f },
                { AlloyAudioProcessor::mixToneParamID, 62.70000076293945f },
                { AlloyAudioProcessor::subEnabledParamID, 0.0f },
                { AlloyAudioProcessor::subOctaveParamID, 1.0f },
                { AlloyAudioProcessor::subVolumeParamID, 37.70000076293945f },
                { AlloyAudioProcessor::subWaveformParamID, 2.0f },
                { AlloyAudioProcessor::mixAgeParamID, 77.70000457763672f },
            } },
            { "Lies", {
                { AlloyAudioProcessor::analogAmpAttackParamID, 0.007000000216066837f },
                { AlloyAudioProcessor::analogAmpDecayParamID, 0.1490000039339066f },
                { AlloyAudioProcessor::analogAmpReleaseParamID, 0.08000000566244125f },
                { AlloyAudioProcessor::analogAmpSustainParamID, 80.0f },
                { AlloyAudioProcessor::analogDetuneParamID, 33.90000152587891f },
                { AlloyAudioProcessor::analogFilterAttackParamID, 0.004999999888241291f },
                { AlloyAudioProcessor::analogFilterCutoffParamID, 498.0f },
                { AlloyAudioProcessor::analogFilterDecayParamID, 0.1030000075697899f },
                { AlloyAudioProcessor::analogFilterEnvAmountParamID, 63.60000228881836f },
                { AlloyAudioProcessor::analogFilterReleaseParamID, 0.08000000566244125f },
                { AlloyAudioProcessor::analogFilterResonanceParamID, 23.80000114440918f },
                { AlloyAudioProcessor::analogFilterSustainParamID, 0.0f },
                { AlloyAudioProcessor::analogGlideTimeParamID, 0.0f },
                { AlloyAudioProcessor::analogOctaveParamID, 3.0f },
                { AlloyAudioProcessor::analogUnisonParamID, 2.0f },
                { AlloyAudioProcessor::analogVelocityToFilterParamID, 52.0f },
                { AlloyAudioProcessor::analogVolumeParamID, 64.0f },
                { AlloyAudioProcessor::analogWaveformParamID, 0.0f },
                { AlloyAudioProcessor::arpDivisionParamID, 11.0f },
                { AlloyAudioProcessor::arpEnabledParamID, 1.0f },
                { AlloyAudioProcessor::arpGateParamID, 45.0f },
                { AlloyAudioProcessor::arpHoldParamID, 0.0f },
                { AlloyAudioProcessor::arpOctaveRangeParamID, 1.0f },
                { AlloyAudioProcessor::arpPatternParamID, 0.0f },
                { AlloyAudioProcessor::arpRateParamID, 5.889999866485596f },
                { AlloyAudioProcessor::arpSyncParamID, 1.0f },
                { AlloyAudioProcessor::fmCarrierAttackParamID, 0.001000000047497451f },
                { AlloyAudioProcessor::fmCarrierDecayParamID, 0.1430000066757202f },
                { AlloyAudioProcessor::fmCarrierOctaveParamID, 2.0f },
                { AlloyAudioProcessor::fmCarrierReleaseParamID, 0.1000000014901161f },
                { AlloyAudioProcessor::fmCarrierSustainParamID, 29.80000114440918f },
                { AlloyAudioProcessor::fmCarrierVolumeParamID, 34.70000076293945f },
                { AlloyAudioProcessor::fmCarrierWaveformParamID, 0.0f },
                { AlloyAudioProcessor::fmModulatorAttackParamID, 0.001000000047497451f },
                { AlloyAudioProcessor::fmModulatorDecayParamID, 0.06700000166893005f },
                { AlloyAudioProcessor::fmModulatorOctaveParamID, 4.0f },
                { AlloyAudioProcessor::fmModulatorReleaseParamID, 0.05000000074505806f },
                { AlloyAudioProcessor::fmModulatorSustainParamID, 0.0f },
                { AlloyAudioProcessor::fmModulatorVolumeParamID, 15.60000038146973f },
                { AlloyAudioProcessor::fmModulatorWaveformParamID, 1.0f },
                { AlloyAudioProcessor::mixDriveParamID, 21.20000076293945f },
                { AlloyAudioProcessor::mixOutputParamID, -4.099999904632568f },
                { AlloyAudioProcessor::mixToneParamID, 69.0f },
                { AlloyAudioProcessor::subEnabledParamID, 1.0f },
                { AlloyAudioProcessor::subOctaveParamID, 0.0f },
                { AlloyAudioProcessor::subVolumeParamID, 23.0f },
                { AlloyAudioProcessor::subWaveformParamID, 0.0f },
                { AlloyAudioProcessor::mixAgeParamID, 0.0f },
            } },
            { "Mainframe", {
                { AlloyAudioProcessor::analogAmpAttackParamID, 0.01100000087171793f },
                { AlloyAudioProcessor::analogAmpDecayParamID, 0.09300000220537186f },
                { AlloyAudioProcessor::analogAmpReleaseParamID, 0.1550000011920929f },
                { AlloyAudioProcessor::analogAmpSustainParamID, 13.80000019073486f },
                { AlloyAudioProcessor::analogDetuneParamID, 37.5f },
                { AlloyAudioProcessor::analogFilterAttackParamID, 0.004999999888241291f },
                { AlloyAudioProcessor::analogFilterCutoffParamID, 2419.0f },
                { AlloyAudioProcessor::analogFilterDecayParamID, 0.0650000050663948f },
                { AlloyAudioProcessor::analogFilterEnvAmountParamID, 65.4000015258789f },
                { AlloyAudioProcessor::analogFilterReleaseParamID, 0.0430000014603138f },
                { AlloyAudioProcessor::analogFilterResonanceParamID, 26.0f },
                { AlloyAudioProcessor::analogFilterSustainParamID, 36.60000228881836f },
                { AlloyAudioProcessor::analogGlideTimeParamID, 0.0f },
                { AlloyAudioProcessor::analogOctaveParamID, 3.0f },
                { AlloyAudioProcessor::analogUnisonParamID, 2.0f },
                { AlloyAudioProcessor::analogVelocityToFilterParamID, 72.30000305175781f },
                { AlloyAudioProcessor::analogVolumeParamID, 69.0999984741211f },
                { AlloyAudioProcessor::analogWaveformParamID, 0.0f },
                { AlloyAudioProcessor::arpDivisionParamID, 11.0f },
                { AlloyAudioProcessor::arpEnabledParamID, 0.0f },
                { AlloyAudioProcessor::arpGateParamID, 50.29999923706055f },
                { AlloyAudioProcessor::arpHoldParamID, 0.0f },
                { AlloyAudioProcessor::arpOctaveRangeParamID, 1.0f },
                { AlloyAudioProcessor::arpPatternParamID, 0.0f },
                { AlloyAudioProcessor::arpRateParamID, 5.889999866485596f },
                { AlloyAudioProcessor::arpSyncParamID, 0.0f },
                { AlloyAudioProcessor::fmCarrierAttackParamID, 0.001000000047497451f },
                { AlloyAudioProcessor::fmCarrierDecayParamID, 0.125f },
                { AlloyAudioProcessor::fmCarrierOctaveParamID, 2.0f },
                { AlloyAudioProcessor::fmCarrierReleaseParamID, 0.3009999990463257f },
                { AlloyAudioProcessor::fmCarrierSustainParamID, 7.200000286102295f },
                { AlloyAudioProcessor::fmCarrierVolumeParamID, 12.5f },
                { AlloyAudioProcessor::fmCarrierWaveformParamID, 2.0f },
                { AlloyAudioProcessor::fmModulatorAttackParamID, 0.006000000052154064f },
                { AlloyAudioProcessor::fmModulatorDecayParamID, 0.5680000185966492f },
                { AlloyAudioProcessor::fmModulatorOctaveParamID, 3.0f },
                { AlloyAudioProcessor::fmModulatorReleaseParamID, 0.8080000281333923f },
                { AlloyAudioProcessor::fmModulatorSustainParamID, 36.70000076293945f },
                { AlloyAudioProcessor::fmModulatorVolumeParamID, 38.40000152587891f },
                { AlloyAudioProcessor::fmModulatorWaveformParamID, 1.0f },
                { AlloyAudioProcessor::mixDriveParamID, 21.0f },
                { AlloyAudioProcessor::mixOutputParamID, -4.099999904632568f },
                { AlloyAudioProcessor::mixToneParamID, 54.0f },
                { AlloyAudioProcessor::subEnabledParamID, 1.0f },
                { AlloyAudioProcessor::subOctaveParamID, 1.0f },
                { AlloyAudioProcessor::subVolumeParamID, 27.39999961853027f },
                { AlloyAudioProcessor::subWaveformParamID, 0.0f },
                { AlloyAudioProcessor::mixAgeParamID, 39.20000076293945f },
                { AlloyAudioProcessor::fmVelocityToBrightnessParamID, 43.5f },
                { AlloyAudioProcessor::fmVelocityToCarrierParamID, 66.0999984741211f },
                { AlloyAudioProcessor::fmModulatorBrightnessParamID, 11.69999980926514f },
            } },
            { "Snap to it", {
                { AlloyAudioProcessor::analogAmpAttackParamID, 0.007000000216066837f },
                { AlloyAudioProcessor::analogAmpDecayParamID, 0.1490000039339066f },
                { AlloyAudioProcessor::analogAmpReleaseParamID, 0.08000000566244125f },
                { AlloyAudioProcessor::analogAmpSustainParamID, 80.0f },
                { AlloyAudioProcessor::analogDetuneParamID, 33.90000152587891f },
                { AlloyAudioProcessor::analogFilterAttackParamID, 0.004999999888241291f },
                { AlloyAudioProcessor::analogFilterCutoffParamID, 860.0f },
                { AlloyAudioProcessor::analogFilterDecayParamID, 0.1030000075697899f },
                { AlloyAudioProcessor::analogFilterEnvAmountParamID, 37.20000076293945f },
                { AlloyAudioProcessor::analogFilterReleaseParamID, 0.08000000566244125f },
                { AlloyAudioProcessor::analogFilterResonanceParamID, 23.10000038146973f },
                { AlloyAudioProcessor::analogFilterSustainParamID, 0.0f },
                { AlloyAudioProcessor::analogGlideTimeParamID, 0.01000000070780516f },
                { AlloyAudioProcessor::analogOctaveParamID, 3.0f },
                { AlloyAudioProcessor::analogUnisonParamID, 2.0f },
                { AlloyAudioProcessor::analogVelocityToFilterParamID, 52.0f },
                { AlloyAudioProcessor::analogVolumeParamID, 64.0f },
                { AlloyAudioProcessor::analogWaveformParamID, 0.0f },
                { AlloyAudioProcessor::arpDivisionParamID, 11.0f },
                { AlloyAudioProcessor::arpEnabledParamID, 1.0f },
                { AlloyAudioProcessor::arpGateParamID, 45.0f },
                { AlloyAudioProcessor::arpHoldParamID, 0.0f },
                { AlloyAudioProcessor::arpOctaveRangeParamID, 1.0f },
                { AlloyAudioProcessor::arpPatternParamID, 2.0f },
                { AlloyAudioProcessor::arpRateParamID, 5.889999866485596f },
                { AlloyAudioProcessor::arpSyncParamID, 1.0f },
                { AlloyAudioProcessor::fmCarrierAttackParamID, 0.001000000047497451f },
                { AlloyAudioProcessor::fmCarrierDecayParamID, 0.1430000066757202f },
                { AlloyAudioProcessor::fmCarrierOctaveParamID, 3.0f },
                { AlloyAudioProcessor::fmCarrierReleaseParamID, 0.1000000014901161f },
                { AlloyAudioProcessor::fmCarrierSustainParamID, 29.80000114440918f },
                { AlloyAudioProcessor::fmCarrierVolumeParamID, 34.70000076293945f },
                { AlloyAudioProcessor::fmCarrierWaveformParamID, 0.0f },
                { AlloyAudioProcessor::fmModulatorAttackParamID, 0.001000000047497451f },
                { AlloyAudioProcessor::fmModulatorDecayParamID, 0.2240000069141388f },
                { AlloyAudioProcessor::fmModulatorOctaveParamID, 4.0f },
                { AlloyAudioProcessor::fmModulatorReleaseParamID, 0.1130000054836273f },
                { AlloyAudioProcessor::fmModulatorSustainParamID, 9.40000057220459f },
                { AlloyAudioProcessor::fmModulatorVolumeParamID, 14.30000019073486f },
                { AlloyAudioProcessor::fmModulatorWaveformParamID, 1.0f },
                { AlloyAudioProcessor::mixDriveParamID, 32.70000076293945f },
                { AlloyAudioProcessor::mixOutputParamID, -4.099999904632568f },
                { AlloyAudioProcessor::mixToneParamID, 79.30000305175781f },
                { AlloyAudioProcessor::subEnabledParamID, 1.0f },
                { AlloyAudioProcessor::subOctaveParamID, 1.0f },
                { AlloyAudioProcessor::subVolumeParamID, 20.89999961853027f },
                { AlloyAudioProcessor::subWaveformParamID, 1.0f },
                { AlloyAudioProcessor::mixAgeParamID, 41.70000076293945f },
                { AlloyAudioProcessor::fmModulatorBrightnessParamID, 74.5999984741211f },
                { AlloyAudioProcessor::fmVelocityToBrightnessParamID, 30.0f },
                { AlloyAudioProcessor::fmVelocityToCarrierParamID, 30.0f },
            } },
            { "The Grid", {
                { AlloyAudioProcessor::analogAmpAttackParamID, 0.00800000037997961f },
                { AlloyAudioProcessor::analogAmpDecayParamID, 0.6270000338554382f },
                { AlloyAudioProcessor::analogAmpReleaseParamID, 1.431000113487244f },
                { AlloyAudioProcessor::analogAmpSustainParamID, 0.0f },
                { AlloyAudioProcessor::analogDetuneParamID, 45.29999923706055f },
                { AlloyAudioProcessor::analogFilterAttackParamID, 0.02000000141561031f },
                { AlloyAudioProcessor::analogFilterCutoffParamID, 703.0f },
                { AlloyAudioProcessor::analogFilterDecayParamID, 0.1550000011920929f },
                { AlloyAudioProcessor::analogFilterEnvAmountParamID, 75.30000305175781f },
                { AlloyAudioProcessor::analogFilterReleaseParamID, 0.5060000419616699f },
                { AlloyAudioProcessor::analogFilterResonanceParamID, 45.79999923706055f },
                { AlloyAudioProcessor::analogFilterSustainParamID, 0.0f },
                { AlloyAudioProcessor::analogGlideTimeParamID, 0.0f },
                { AlloyAudioProcessor::analogOctaveParamID, 3.0f },
                { AlloyAudioProcessor::analogUnisonParamID, 2.0f },
                { AlloyAudioProcessor::analogVelocityToFilterParamID, 33.60000228881836f },
                { AlloyAudioProcessor::analogVolumeParamID, 57.60000228881836f },
                { AlloyAudioProcessor::analogWaveformParamID, 0.0f },
                { AlloyAudioProcessor::arpDivisionParamID, 11.0f },
                { AlloyAudioProcessor::arpEnabledParamID, 0.0f },
                { AlloyAudioProcessor::arpGateParamID, 50.29999923706055f },
                { AlloyAudioProcessor::arpHoldParamID, 0.0f },
                { AlloyAudioProcessor::arpOctaveRangeParamID, 1.0f },
                { AlloyAudioProcessor::arpPatternParamID, 0.0f },
                { AlloyAudioProcessor::arpRateParamID, 5.889999866485596f },
                { AlloyAudioProcessor::arpSyncParamID, 1.0f },
                { AlloyAudioProcessor::fmCarrierAttackParamID, 0.002000000094994903f },
                { AlloyAudioProcessor::fmCarrierDecayParamID, 0.6950000524520874f },
                { AlloyAudioProcessor::fmCarrierOctaveParamID, 3.0f },
                { AlloyAudioProcessor::fmCarrierReleaseParamID, 0.2780000269412994f },
                { AlloyAudioProcessor::fmCarrierSustainParamID, 0.0f },
                { AlloyAudioProcessor::fmCarrierVolumeParamID, 27.70000076293945f },
                { AlloyAudioProcessor::fmCarrierWaveformParamID, 1.0f },
                { AlloyAudioProcessor::fmModulatorAttackParamID, 0.01800000108778477f },
                { AlloyAudioProcessor::fmModulatorDecayParamID, 0.5700000524520874f },
                { AlloyAudioProcessor::fmModulatorOctaveParamID, 1.0f },
                { AlloyAudioProcessor::fmModulatorReleaseParamID, 1.42300009727478f },
                { AlloyAudioProcessor::fmModulatorSustainParamID, 0.0f },
                { AlloyAudioProcessor::fmModulatorVolumeParamID, 12.19999980926514f },
                { AlloyAudioProcessor::fmModulatorWaveformParamID, 1.0f },
                { AlloyAudioProcessor::mixDriveParamID, 21.20000076293945f },
                { AlloyAudioProcessor::mixOutputParamID, -4.099999904632568f },
                { AlloyAudioProcessor::mixToneParamID, 100.0f },
                { AlloyAudioProcessor::subEnabledParamID, 1.0f },
                { AlloyAudioProcessor::subOctaveParamID, 0.0f },
                { AlloyAudioProcessor::subVolumeParamID, 40.90000152587891f },
                { AlloyAudioProcessor::subWaveformParamID, 2.0f },
                { AlloyAudioProcessor::mixAgeParamID, 0.0f },
                { AlloyAudioProcessor::fmModulatorBrightnessParamID, 22.10000038146973f },
                { AlloyAudioProcessor::fmVelocityToCarrierParamID, 49.10000228881836f },
            } },
            { "The World To Rust", {
                { AlloyAudioProcessor::analogAmpAttackParamID, 0.01100000087171793f },
                { AlloyAudioProcessor::analogAmpDecayParamID, 0.1490000039339066f },
                { AlloyAudioProcessor::analogAmpReleaseParamID, 0.08000000566244125f },
                { AlloyAudioProcessor::analogAmpSustainParamID, 80.0f },
                { AlloyAudioProcessor::analogDetuneParamID, 70.5f },
                { AlloyAudioProcessor::analogFilterAttackParamID, 0.004999999888241291f },
                { AlloyAudioProcessor::analogFilterCutoffParamID, 1484.0f },
                { AlloyAudioProcessor::analogFilterDecayParamID, 0.1030000075697899f },
                { AlloyAudioProcessor::analogFilterEnvAmountParamID, 55.5f },
                { AlloyAudioProcessor::analogFilterReleaseParamID, 0.08000000566244125f },
                { AlloyAudioProcessor::analogFilterResonanceParamID, 42.60000228881836f },
                { AlloyAudioProcessor::analogFilterSustainParamID, 0.0f },
                { AlloyAudioProcessor::analogGlideTimeParamID, 0.03600000217556953f },
                { AlloyAudioProcessor::analogOctaveParamID, 4.0f },
                { AlloyAudioProcessor::analogUnisonParamID, 1.0f },
                { AlloyAudioProcessor::analogVelocityToFilterParamID, 52.0f },
                { AlloyAudioProcessor::analogVolumeParamID, 36.29999923706055f },
                { AlloyAudioProcessor::analogWaveformParamID, 0.0f },
                { AlloyAudioProcessor::arpDivisionParamID, 11.0f },
                { AlloyAudioProcessor::arpEnabledParamID, 0.0f },
                { AlloyAudioProcessor::arpGateParamID, 50.29999923706055f },
                { AlloyAudioProcessor::arpHoldParamID, 0.0f },
                { AlloyAudioProcessor::arpOctaveRangeParamID, 1.0f },
                { AlloyAudioProcessor::arpPatternParamID, 0.0f },
                { AlloyAudioProcessor::arpRateParamID, 5.889999866485596f },
                { AlloyAudioProcessor::arpSyncParamID, 1.0f },
                { AlloyAudioProcessor::fmCarrierAttackParamID, 0.001000000047497451f },
                { AlloyAudioProcessor::fmCarrierDecayParamID, 0.2060000151395798f },
                { AlloyAudioProcessor::fmCarrierOctaveParamID, 3.0f },
                { AlloyAudioProcessor::fmCarrierReleaseParamID, 0.1000000014901161f },
                { AlloyAudioProcessor::fmCarrierSustainParamID, 39.10000228881836f },
                { AlloyAudioProcessor::fmCarrierVolumeParamID, 52.79999923706055f },
                { AlloyAudioProcessor::fmCarrierWaveformParamID, 0.0f },
                { AlloyAudioProcessor::fmModulatorAttackParamID, 0.02400000020861626f },
                { AlloyAudioProcessor::fmModulatorDecayParamID, 0.737000048160553f },
                { AlloyAudioProcessor::fmModulatorOctaveParamID, 1.0f },
                { AlloyAudioProcessor::fmModulatorReleaseParamID, 0.3710000216960907f },
                { AlloyAudioProcessor::fmModulatorSustainParamID, 48.40000152587891f },
                { AlloyAudioProcessor::fmModulatorVolumeParamID, 23.70000076293945f },
                { AlloyAudioProcessor::fmModulatorWaveformParamID, 2.0f },
                { AlloyAudioProcessor::mixDriveParamID, 17.80000114440918f },
                { AlloyAudioProcessor::mixOutputParamID, -4.899999618530273f },
                { AlloyAudioProcessor::mixToneParamID, 69.0f },
                { AlloyAudioProcessor::subEnabledParamID, 1.0f },
                { AlloyAudioProcessor::subOctaveParamID, 0.0f },
                { AlloyAudioProcessor::subVolumeParamID, 22.70000076293945f },
                { AlloyAudioProcessor::subWaveformParamID, 0.0f },
                { AlloyAudioProcessor::mixAgeParamID, 25.39999961853027f },
                { AlloyAudioProcessor::fmModulatorBrightnessParamID, 77.80000305175781f },
                { AlloyAudioProcessor::fmVelocityToCarrierParamID, 30.0f },
            } },
            { "UseNet", {
                { AlloyAudioProcessor::analogAmpAttackParamID, 0.01100000087171793f },
                { AlloyAudioProcessor::analogAmpDecayParamID, 0.1490000039339066f },
                { AlloyAudioProcessor::analogAmpReleaseParamID, 0.08000000566244125f },
                { AlloyAudioProcessor::analogAmpSustainParamID, 80.0f },
                { AlloyAudioProcessor::analogDetuneParamID, 70.5f },
                { AlloyAudioProcessor::analogFilterAttackParamID, 0.004999999888241291f },
                { AlloyAudioProcessor::analogFilterCutoffParamID, 1484.0f },
                { AlloyAudioProcessor::analogFilterDecayParamID, 0.1030000075697899f },
                { AlloyAudioProcessor::analogFilterEnvAmountParamID, 55.5f },
                { AlloyAudioProcessor::analogFilterReleaseParamID, 0.08000000566244125f },
                { AlloyAudioProcessor::analogFilterResonanceParamID, 42.60000228881836f },
                { AlloyAudioProcessor::analogFilterSustainParamID, 0.0f },
                { AlloyAudioProcessor::analogGlideTimeParamID, 0.03600000217556953f },
                { AlloyAudioProcessor::analogOctaveParamID, 4.0f },
                { AlloyAudioProcessor::analogUnisonParamID, 1.0f },
                { AlloyAudioProcessor::analogVelocityToFilterParamID, 52.0f },
                { AlloyAudioProcessor::analogVolumeParamID, 36.29999923706055f },
                { AlloyAudioProcessor::analogWaveformParamID, 0.0f },
                { AlloyAudioProcessor::arpDivisionParamID, 11.0f },
                { AlloyAudioProcessor::arpEnabledParamID, 0.0f },
                { AlloyAudioProcessor::arpGateParamID, 50.29999923706055f },
                { AlloyAudioProcessor::arpHoldParamID, 0.0f },
                { AlloyAudioProcessor::arpOctaveRangeParamID, 1.0f },
                { AlloyAudioProcessor::arpPatternParamID, 0.0f },
                { AlloyAudioProcessor::arpRateParamID, 5.889999866485596f },
                { AlloyAudioProcessor::arpSyncParamID, 1.0f },
                { AlloyAudioProcessor::fmCarrierAttackParamID, 0.001000000047497451f },
                { AlloyAudioProcessor::fmCarrierDecayParamID, 0.2060000151395798f },
                { AlloyAudioProcessor::fmCarrierOctaveParamID, 4.0f },
                { AlloyAudioProcessor::fmCarrierReleaseParamID, 0.1000000014901161f },
                { AlloyAudioProcessor::fmCarrierSustainParamID, 39.10000228881836f },
                { AlloyAudioProcessor::fmCarrierVolumeParamID, 44.10000228881836f },
                { AlloyAudioProcessor::fmCarrierWaveformParamID, 0.0f },
                { AlloyAudioProcessor::fmModulatorAttackParamID, 0.02400000020861626f },
                { AlloyAudioProcessor::fmModulatorDecayParamID, 0.737000048160553f },
                { AlloyAudioProcessor::fmModulatorOctaveParamID, 1.0f },
                { AlloyAudioProcessor::fmModulatorReleaseParamID, 0.3710000216960907f },
                { AlloyAudioProcessor::fmModulatorSustainParamID, 48.40000152587891f },
                { AlloyAudioProcessor::fmModulatorVolumeParamID, 8.800000190734863f },
                { AlloyAudioProcessor::fmModulatorWaveformParamID, 1.0f },
                { AlloyAudioProcessor::mixDriveParamID, 21.0f },
                { AlloyAudioProcessor::mixOutputParamID, -4.899999618530273f },
                { AlloyAudioProcessor::mixToneParamID, 69.0f },
                { AlloyAudioProcessor::subEnabledParamID, 1.0f },
                { AlloyAudioProcessor::subOctaveParamID, 0.0f },
                { AlloyAudioProcessor::subVolumeParamID, 55.40000152587891f },
                { AlloyAudioProcessor::subWaveformParamID, 0.0f },
                { AlloyAudioProcessor::mixAgeParamID, 25.39999961853027f },
                { AlloyAudioProcessor::fmModulatorBrightnessParamID, 11.10000038146973f },
                { AlloyAudioProcessor::fmVelocityToCarrierParamID, 34.90000152587891f },
            } },
        };

        return presets;
    }
}

AlloyAudioProcessor::AlloyAudioProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "PARAMETERS", createParameterLayout()),
      factoryPresets(getFactoryPresets())
{
    analogWaveformParam = apvts.getRawParameterValue(analogWaveformParamID);
    analogOctaveParam = apvts.getRawParameterValue(analogOctaveParamID);
    analogUnisonParam = apvts.getRawParameterValue(analogUnisonParamID);
    analogDetuneParam = apvts.getRawParameterValue(analogDetuneParamID);
    analogFilterCutoffParam = apvts.getRawParameterValue(analogFilterCutoffParamID);
    analogFilterResonanceParam = apvts.getRawParameterValue(analogFilterResonanceParamID);
    analogFilterEnvAmountParam = apvts.getRawParameterValue(analogFilterEnvAmountParamID);
    analogVelocityToFilterParam = apvts.getRawParameterValue(analogVelocityToFilterParamID);
    analogFilterAttackParam = apvts.getRawParameterValue(analogFilterAttackParamID);
    analogFilterDecayParam = apvts.getRawParameterValue(analogFilterDecayParamID);
    analogFilterSustainParam = apvts.getRawParameterValue(analogFilterSustainParamID);
    analogFilterReleaseParam = apvts.getRawParameterValue(analogFilterReleaseParamID);
    analogAmpAttackParam = apvts.getRawParameterValue(analogAmpAttackParamID);
    analogAmpDecayParam = apvts.getRawParameterValue(analogAmpDecayParamID);
    analogAmpSustainParam = apvts.getRawParameterValue(analogAmpSustainParamID);
    analogAmpReleaseParam = apvts.getRawParameterValue(analogAmpReleaseParamID);
    analogGlideTimeParam = apvts.getRawParameterValue(analogGlideTimeParamID);
    analogVolumeParam = apvts.getRawParameterValue(analogVolumeParamID);

    subEnabledParam = apvts.getRawParameterValue(subEnabledParamID);
    subWaveformParam = apvts.getRawParameterValue(subWaveformParamID);
    subOctaveParam = apvts.getRawParameterValue(subOctaveParamID);
    subVolumeParam = apvts.getRawParameterValue(subVolumeParamID);

    fmCarrierWaveformParam = apvts.getRawParameterValue(fmCarrierWaveformParamID);
    fmCarrierOctaveParam = apvts.getRawParameterValue(fmCarrierOctaveParamID);
    fmCarrierVolumeParam = apvts.getRawParameterValue(fmCarrierVolumeParamID);
    fmVelocityToCarrierParam = apvts.getRawParameterValue(fmVelocityToCarrierParamID);
    fmCarrierAttackParam = apvts.getRawParameterValue(fmCarrierAttackParamID);
    fmCarrierDecayParam = apvts.getRawParameterValue(fmCarrierDecayParamID);
    fmCarrierSustainParam = apvts.getRawParameterValue(fmCarrierSustainParamID);
    fmCarrierReleaseParam = apvts.getRawParameterValue(fmCarrierReleaseParamID);

    fmModulatorWaveformParam = apvts.getRawParameterValue(fmModulatorWaveformParamID);
    fmModulatorOctaveParam = apvts.getRawParameterValue(fmModulatorOctaveParamID);
    fmModulatorVolumeParam = apvts.getRawParameterValue(fmModulatorVolumeParamID);
    fmVelocityToBrightnessParam = apvts.getRawParameterValue(fmVelocityToBrightnessParamID);
    fmModulatorBrightnessParam = apvts.getRawParameterValue(fmModulatorBrightnessParamID);
    fmModulatorAttackParam = apvts.getRawParameterValue(fmModulatorAttackParamID);
    fmModulatorDecayParam = apvts.getRawParameterValue(fmModulatorDecayParamID);
    fmModulatorSustainParam = apvts.getRawParameterValue(fmModulatorSustainParamID);
    fmModulatorReleaseParam = apvts.getRawParameterValue(fmModulatorReleaseParamID);

    arpEnabledParam = apvts.getRawParameterValue(arpEnabledParamID);
    arpSyncParam = apvts.getRawParameterValue(arpSyncParamID);
    arpDivisionParam = apvts.getRawParameterValue(arpDivisionParamID);
    arpRateParam = apvts.getRawParameterValue(arpRateParamID);
    arpPatternParam = apvts.getRawParameterValue(arpPatternParamID);
    arpOctaveRangeParam = apvts.getRawParameterValue(arpOctaveRangeParamID);
    arpGateParam = apvts.getRawParameterValue(arpGateParamID);
    arpHoldParam = apvts.getRawParameterValue(arpHoldParamID);

    mixDriveParam = apvts.getRawParameterValue(mixDriveParamID);
    mixToneParam = apvts.getRawParameterValue(mixToneParamID);
    mixOutputParam = apvts.getRawParameterValue(mixOutputParamID);
    mixAgeParam = apvts.getRawParameterValue(mixAgeParamID);
}

AlloyAudioProcessor::~AlloyAudioProcessor() = default;

const juce::StringArray& AlloyAudioProcessor::getWaveformChoices()
{
    static const juce::StringArray choices { "Saw", "Square", "Triangle" };
    return choices;
}

const juce::StringArray& AlloyAudioProcessor::getSubWaveformChoices()
{
    static const juce::StringArray choices { "Square", "Sine", "Triangle" };
    return choices;
}

const juce::StringArray& AlloyAudioProcessor::getFmWaveformChoices()
{
    static const juce::StringArray choices { "Sine", "Triangle", "Square" };
    return choices;
}

const juce::StringArray& AlloyAudioProcessor::getOctaveChoices()
{
    static const juce::StringArray choices { "+2", "+1", "0", "-1", "-2" };
    return choices;
}

const juce::StringArray& AlloyAudioProcessor::getSubOctaveChoices()
{
    static const juce::StringArray choices { "0", "-1", "-2", "-3" };
    return choices;
}

const juce::StringArray& AlloyAudioProcessor::getUnisonChoices()
{
    static const juce::StringArray choices { "1", "2", "3", "4" };
    return choices;
}

const juce::StringArray& AlloyAudioProcessor::getArpDivisionChoices()
{
    static const juce::StringArray choices = [] {
        juce::StringArray result;
        for (auto& subdivision : arpSubdivisions)
            result.add(subdivision.label);
        return result;
    }();

    return choices;
}

const juce::StringArray& AlloyAudioProcessor::getArpPatternChoices()
{
    static const juce::StringArray choices { "Up", "Down", "Up-Down", "Random", "As Played" };
    return choices;
}

const juce::StringArray& AlloyAudioProcessor::getArpOctaveRangeChoices()
{
    static const juce::StringArray choices { "1", "2", "3", "4" };
    return choices;
}

juce::AudioProcessorValueTreeState::ParameterLayout AlloyAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    auto addPercent = [&params](const char* id, const char* name, float defaultPercent)
    {
        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID{id, 1},
            name,
            juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f),
            defaultPercent,
            juce::AudioParameterFloatAttributes()
                .withLabel("%")
                .withStringFromValueFunction([](float v, int) { return juce::String(v, 1) + "%"; })));
    };

    auto addTimeSeconds = [&params](const char* id, const char* name, float minSeconds, float maxSeconds, float defaultSeconds)
    {
        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID{id, 1},
            name,
            juce::NormalisableRange<float>(minSeconds, maxSeconds, 0.001f, 0.4f),
            defaultSeconds,
            juce::AudioParameterFloatAttributes()
                .withLabel("s")
                .withStringFromValueFunction([](float v, int) { return juce::String(v, 3) + " s"; })));
    };

    // ---- Analog Bass ----
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{analogWaveformParamID, 1}, "Analog Waveform", getWaveformChoices(), 0));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{analogOctaveParamID, 1}, "Analog Octave", getOctaveChoices(), analogOctaveDefaultIndex));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{analogUnisonParamID, 1}, "Analog Unison", getUnisonChoices(), unisonDefaultIndex));
    addPercent(analogDetuneParamID, "Analog Detune", 20.0f);

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{analogFilterCutoffParamID, 1},
        "Filter Cutoff",
        juce::NormalisableRange<float>(20.0f, 20000.0f, 1.0f, 0.3f),
        1200.0f,
        juce::AudioParameterFloatAttributes()
            .withLabel("Hz")
            .withStringFromValueFunction([](float v, int) { return juce::String(v, 0) + " Hz"; })));
    addPercent(analogFilterResonanceParamID, "Filter Resonance", 25.0f);
    addPercent(analogFilterEnvAmountParamID, "Filter Env Amount", 60.0f);
    addPercent(analogVelocityToFilterParamID, "Velocity To Filter", 40.0f);

    addTimeSeconds(analogFilterAttackParamID, "Filter Attack", 0.001f, 2.0f, 0.005f);
    addTimeSeconds(analogFilterDecayParamID, "Filter Decay", 0.005f, 2.0f, 0.18f);
    addPercent(analogFilterSustainParamID, "Filter Sustain", 0.0f);
    addTimeSeconds(analogFilterReleaseParamID, "Filter Release", 0.005f, 2.0f, 0.08f);

    addTimeSeconds(analogAmpAttackParamID, "Amp Attack", 0.001f, 2.0f, 0.003f);
    addTimeSeconds(analogAmpDecayParamID, "Amp Decay", 0.005f, 2.0f, 0.25f);
    addPercent(analogAmpSustainParamID, "Amp Sustain", 80.0f);
    addTimeSeconds(analogAmpReleaseParamID, "Amp Release", 0.005f, 2.0f, 0.08f);

    addTimeSeconds(analogGlideTimeParamID, "Glide Time", 0.0f, maxGlideSeconds, 0.0f);
    addPercent(analogVolumeParamID, "Analog Volume", 80.0f);

    // ---- Sub Bass ----
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{subEnabledParamID, 1}, "Sub Enabled", true));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{subWaveformParamID, 1}, "Sub Waveform", getSubWaveformChoices(), 0));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{subOctaveParamID, 1}, "Sub Octave", getSubOctaveChoices(), subOctaveDefaultIndex));
    addPercent(subVolumeParamID, "Sub Volume", 60.0f);

    // ---- FM Bass (2-op phase modulation - see fmMaxModulationIndex) ----
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{fmCarrierWaveformParamID, 1}, "FM Carrier Waveform", getFmWaveformChoices(), 0));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{fmCarrierOctaveParamID, 1}, "FM Carrier Octave", getOctaveChoices(), analogOctaveDefaultIndex)); // "-1"
    addPercent(fmCarrierVolumeParamID, "FM Carrier Volume", 70.0f);
    addPercent(fmVelocityToCarrierParamID, "Velocity To Carrier", 30.0f);
    addTimeSeconds(fmCarrierAttackParamID, "FM Carrier Attack", 0.001f, 2.0f, 0.002f);
    addTimeSeconds(fmCarrierDecayParamID, "FM Carrier Decay", 0.005f, 2.0f, 0.3f);
    addPercent(fmCarrierSustainParamID, "FM Carrier Sustain", 60.0f);
    addTimeSeconds(fmCarrierReleaseParamID, "FM Carrier Release", 0.005f, 2.0f, 0.1f);

    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{fmModulatorWaveformParamID, 1}, "FM Modulator Waveform", getFmWaveformChoices(), 0));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{fmModulatorOctaveParamID, 1}, "FM Modulator Octave", getOctaveChoices(), 4)); // "-2"
    addPercent(fmModulatorVolumeParamID, "FM Modulator Volume", 35.0f);
    addPercent(fmVelocityToBrightnessParamID, "Velocity To Brightness", 30.0f);
    addPercent(fmModulatorBrightnessParamID, "FM Modulator Brightness", 0.0f);
    addTimeSeconds(fmModulatorAttackParamID, "FM Modulator Attack", 0.001f, 2.0f, 0.001f);
    addTimeSeconds(fmModulatorDecayParamID, "FM Modulator Decay", 0.005f, 2.0f, 0.15f);
    addPercent(fmModulatorSustainParamID, "FM Modulator Sustain", 0.0f);
    addTimeSeconds(fmModulatorReleaseParamID, "FM Modulator Release", 0.005f, 2.0f, 0.05f);

    // ---- Arpeggiator ----
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{arpEnabledParamID, 1}, "Arp Enabled", false));
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{arpSyncParamID, 1}, "Arp Sync", true));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{arpDivisionParamID, 1}, "Arp Division", getArpDivisionChoices(), arpDefaultDivisionIndex));
    {
        const juce::NormalisableRange<float> arpRateRange(0.5f, 20.0f, 0.01f, 0.4f);
        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID{arpRateParamID, 1},
            "Arp Rate",
            arpRateRange,
            8.0f,
            juce::AudioParameterFloatAttributes()
                .withLabel("Hz")
                .withStringFromValueFunction([](float v, int) { return juce::String(v, 2) + " Hz"; })));
    }
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{arpPatternParamID, 1}, "Arp Pattern", getArpPatternChoices(), 0));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{arpOctaveRangeParamID, 1}, "Arp Octave Range", getArpOctaveRangeChoices(), 0));
    // Default well under 100% - short gate length is what gives an arpeggiated industrial
    // bassline its tight, staccato feel (see the plan's research notes); a long/legato gate
    // would undersell the genre's characteristic sound.
    addPercent(arpGateParamID, "Arp Gate", 55.0f);
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{arpHoldParamID, 1}, "Arp Hold", false));

    // ---- Mix ----
    addPercent(mixDriveParamID, "Drive", 20.0f);
    addPercent(mixToneParamID, "Tone", 70.0f);
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{mixOutputParamID, 1},
        "Output",
        juce::NormalisableRange<float>(-24.0f, 12.0f, 0.1f),
        0.0f,
        juce::AudioParameterFloatAttributes()
            .withLabel("dB")
            .withStringFromValueFunction([](float v, int) { return juce::String(v, 1) + " dB"; })));
    addPercent(mixAgeParamID, "Age", 0.0f);

    return {params.begin(), params.end()};
}

void AlloyAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    sampleRateHz = sampleRate;

    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = static_cast<juce::uint32>(samplesPerBlock);
    spec.numChannels = 1;

    analogFilter.prepare(spec);
    analogFilter.setType(juce::dsp::StateVariableTPTFilter<float>::Type::lowpass);
    analogFilter.reset();

    mixToneFilter.prepare(spec);
    mixToneFilter.setType(juce::dsp::StateVariableTPTFilter<float>::Type::lowpass);
    mixToneFilter.reset();

    subShelfFilter.prepare(spec);
    subShelfFilter.coefficients = juce::dsp::IIR::Coefficients<float>::makeHighShelf(sampleRate, subShelfHz, subShelfQ, 1.0f);
    subShelfFilter.reset();

    mixToneEqFilter.prepare(spec);
    mixToneEqFilter.coefficients = juce::dsp::IIR::Coefficients<float>::makePeakFilter(sampleRate, mixToneEqHz, mixToneEqQ, 1.0f);
    mixToneEqFilter.reset();

    analogFilterEnv.setSampleRate(sampleRate);
    analogAmpEnv.setSampleRate(sampleRate);
    fmCarrierEnv.setSampleRate(sampleRate);
    fmModulatorEnv.setSampleRate(sampleRate);

    for (auto& osc : analogOscillators)
        osc.resetPhase(0.0f);
    subOscillator.resetPhase(0.0f);
    fmCarrierOscillator.resetPhase(0.0f);
    fmModulatorOscillator.resetPhase(0.0f);

    heldNotes.clear();
    latchedNotes.clear();
    wasHoldEnabled = false;
    gateOn = false;
    currentSemitone = 60.0f;
    glideTargetSemitone = 60.0f;

    arpStepSampleCounter = 0;
    arpStepIndex = 0;
    arpGateIsOpen = false;
    arpSyncedStepValid = false;

    ageDriftState = 0.0f;
    ageWarbleState = 0.0f;

    fmModulatorEnvSmoothed = 0.0f;
}

void AlloyAudioProcessor::releaseResources() {}

bool AlloyAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::mono()
        || layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

float AlloyAudioProcessor::frequencyForNote(int midiNoteNumber, int octaveShift) const noexcept
{
    return noteToHz((float) (midiNoteNumber + octaveShift * 12));
}

float AlloyAudioProcessor::cachedNoteToHz(float note, float& lastNote, float& lastHz) const noexcept
{
    if (std::abs(note - lastNote) > 0.0f)
    {
        lastNote = note;
        lastHz = noteToHz(note);
    }
    return lastHz;
}

double AlloyAudioProcessor::getCurrentBpm() const
{
    constexpr double fallbackBpm = 120.0;

    if (auto* playHead = getPlayHead())
    {
        if (auto position = playHead->getPosition())
        {
            if (auto bpm = position->getBpm())
                if (*bpm > 0.0)
                    return *bpm;
        }
    }

    return fallbackBpm;
}

void AlloyAudioProcessor::triggerVoiceNoteOn(int midiNoteNumber, float velocity, bool forceRetrigger)
{
    currentVelocity = velocity;
    currentMidiNote = midiNoteNumber;

    if (forceRetrigger || !gateOn)
    {
        // Coming from silence (or a forced arp-step retrigger): snap pitch immediately - no
        // glide into a fresh pluck - and fully retrigger every envelope.
        currentSemitone = (float) midiNoteNumber;
        glideTargetSemitone = (float) midiNoteNumber;
        gateOn = true;
        analogFilterEnv.noteOn();
        analogAmpEnv.noteOn();
        fmCarrierEnv.noteOn();
        fmModulatorEnv.noteOn();
    }
    else
    {
        // Still-legato (raw MIDI only - the arp always forces a retrigger): only the pitch
        // target moves, glide handles the ramp in processBlock, envelopes are left alone.
        glideTargetSemitone = (float) midiNoteNumber;
    }
}

void AlloyAudioProcessor::triggerVoiceNoteOff()
{
    gateOn = false;
    analogFilterEnv.noteOff();
    analogAmpEnv.noteOff();
    fmCarrierEnv.noteOff();
    fmModulatorEnv.noteOff();
}

void AlloyAudioProcessor::handleMidiMessage(const juce::MidiMessage& message)
{
    const auto arpOn = arpEnabledParam->load() > 0.5f;
    const auto holdOn = arpHoldParam->load() > 0.5f;

    if (message.isNoteOn())
    {
        const auto note = message.getNoteNumber();
        const auto velocity = message.getFloatVelocity();
        const bool wasEmpty = heldNotes.empty();
        heldNotes.push_back(note);

        if (arpOn)
        {
            // The arp engine (processBlock) is the only thing that triggers the voice while
            // arping - this just maintains the chord memory it reads from.
            if (holdOn)
            {
                if (wasEmpty)
                    latchedNotes.clear(); // a fresh chord after a full release replaces the old latch
                if (std::find(latchedNotes.begin(), latchedNotes.end(), note) == latchedNotes.end())
                    latchedNotes.push_back(note);
            }
            currentVelocity = velocity;
        }
        else
        {
            triggerVoiceNoteOn(note, velocity, /*forceRetrigger*/ wasEmpty);
        }
    }
    else if (message.isNoteOff())
    {
        const auto note = message.getNoteNumber();
        heldNotes.erase(std::remove(heldNotes.begin(), heldNotes.end(), note), heldNotes.end());

        if (!arpOn)
        {
            if (!heldNotes.empty())
            {
                // Another held note takes over (last-note priority) - re-pitch only, still legato.
                triggerVoiceNoteOn(heldNotes.back(), currentVelocity, /*forceRetrigger*/ false);
            }
            else
            {
                triggerVoiceNoteOff();
            }
        }
        // While arping, note-offs only affect heldNotes (already updated above); the arp engine
        // notices an empty pool on its own next step (or immediately - see processBlock) and
        // releases the voice itself. latchedNotes deliberately isn't touched here while Hold is on.
    }
    else if (message.isAllNotesOff() || message.isAllSoundOff())
    {
        heldNotes.clear();
        latchedNotes.clear();
        triggerVoiceNoteOff();
    }
}

const std::vector<int>& AlloyAudioProcessor::buildArpNotePool()
{
    const auto holdOn = arpHoldParam->load() > 0.5f;
    const auto& sourceNotes = (holdOn && !latchedNotes.empty()) ? latchedNotes : heldNotes;

    arpScratchResultPool.clear();

    if (sourceNotes.empty())
        return arpScratchResultPool;

    const auto pattern = static_cast<ArpPattern>(juce::jlimit(0, 4, (int) arpPatternParam->load()));
    const auto octaveRange = juce::jlimit(1, 4, (int) arpOctaveRangeParam->load() + 1);

    // Persistent scratch buffers, cleared (not deallocated) and refilled each call instead of
    // fresh local vectors - this is called every arp step (up to 20/sec free-running, or faster
    // for fast synced divisions) from inside processBlock's per-sample loop, so repeated
    // malloc/free churn here was a genuine audio-thread heap-allocation anti-pattern.
    arpScratchBaseNotes.assign(sourceNotes.begin(), sourceNotes.end());
    if (pattern != ArpPattern::asPlayed)
        std::sort(arpScratchBaseNotes.begin(), arpScratchBaseNotes.end());

    arpScratchUpPool.clear();
    arpScratchUpPool.reserve(arpScratchBaseNotes.size() * (size_t) octaveRange);
    for (int oct = 0; oct < octaveRange; ++oct)
        for (auto note : arpScratchBaseNotes)
            arpScratchUpPool.push_back(note + oct * 12);

    switch (pattern)
    {
        case ArpPattern::down:
            arpScratchResultPool.assign(arpScratchUpPool.rbegin(), arpScratchUpPool.rend());
            break;
        case ArpPattern::upDown:
            // Ping-pong without repeating the top/bottom notes on the turnaround.
            arpScratchResultPool = arpScratchUpPool;
            if (arpScratchUpPool.size() > 2)
                arpScratchResultPool.insert(arpScratchResultPool.end(),
                                             arpScratchUpPool.rbegin() + 1, arpScratchUpPool.rend() - 1);
            break;
        case ArpPattern::up:
        case ArpPattern::random:
        case ArpPattern::asPlayed:
        default:
            arpScratchResultPool = arpScratchUpPool;
            break;
    }

    return arpScratchResultPool;
}

void AlloyAudioProcessor::advanceArpStep()
{
    const auto& pool = buildArpNotePool();

    if (pool.empty())
    {
        if (gateOn)
            triggerVoiceNoteOff();
        arpStepIndex = 0;
        return;
    }

    const auto pattern = static_cast<ArpPattern>(juce::jlimit(0, 4, (int) arpPatternParam->load()));
    int index;
    if (pattern == ArpPattern::random)
        index = juce::Random::getSystemRandom().nextInt((int) pool.size());
    else
        index = arpStepIndex % (int) pool.size();

    triggerVoiceNoteOn(pool[(size_t) index], currentVelocity, /*forceRetrigger*/ true);

    ++arpStepIndex;
}

void AlloyAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();

    if (buffer.getNumChannels() < 1)
        return;

    if (panicRequested.exchange(false, std::memory_order_relaxed))
    {
        heldNotes.clear();
        latchedNotes.clear();
        triggerVoiceNoteOff();
    }

    const auto numSamples = buffer.getNumSamples();

    const auto waveform = static_cast<AlloyOscWaveform>(juce::jlimit(0, 2, (int) analogWaveformParam->load()));
    const auto octaveChoiceIndex = juce::jlimit(0, analogOctaveChoiceCount - 1, (int) analogOctaveParam->load());
    const auto octaveShift = 2 - octaveChoiceIndex; // choice index 0..4 (top..bottom) -> +2..-2

    const auto unisonVoices = juce::jlimit(1, maxUnisonVoices, (int) analogUnisonParam->load() + 1);
    const auto detuneAmount = analogDetuneParam->load() * 0.01f;

    const auto subOn = subEnabledParam->load() > 0.5f;
    const auto subWaveform = subWaveformForChoiceIndex(juce::jlimit(0, 2, (int) subWaveformParam->load()));
    const auto subOctaveChoiceIndex = juce::jlimit(0, subOctaveChoiceCount - 1, (int) subOctaveParam->load());
    const auto subOctaveShift = -subOctaveChoiceIndex; // choice index 0..3 (top..bottom) -> 0..-3
    const auto subVolume = subVolumeParam->load() * 0.01f;

    const auto subShelfGainLinear = juce::Decibels::decibelsToGain(
        subWaveform == AlloyOscWaveform::square ? subShelfGainDb : 0.0f);
    subShelfFilter.coefficients = juce::dsp::IIR::Coefficients<float>::makeHighShelf(
        sampleRateHz, subShelfHz, subShelfQ, subShelfGainLinear);

    const auto analogVolume = analogVolumeParam->load() * 0.01f;

    const auto baseCutoffHz = analogFilterCutoffParam->load();
    const auto filterEnvAmount = analogFilterEnvAmountParam->load() * 0.01f;
    const auto velToFilterAmount = analogVelocityToFilterParam->load() * 0.01f;
    // Block-constant (baseCutoffHz doesn't vary within a block) - hoisted out of the per-sample
    // loop below. NOT joined with the velocity term (currentVelocity CAN change mid-block, on a
    // note-on landing anywhere in this block via handleMidiMessage() - see the per-sample cache
    // guard below, which reads currentVelocity fresh every sample for exactly that reason).
    const auto baseCutoffLog2 = std::log2(juce::jmax(20.0f, baseCutoffHz));
    const auto resonanceAmount = analogFilterResonanceParam->load() * 0.01f;
    analogFilter.setResonance(juce::jmap(resonanceAmount, 0.0f, 1.0f, 0.1f, 0.99f));

    analogFilterEnv.setParameters({ analogFilterAttackParam->load(), analogFilterDecayParam->load(),
                                     analogFilterSustainParam->load() * 0.01f, analogFilterReleaseParam->load() });
    analogAmpEnv.setParameters({ analogAmpAttackParam->load(), analogAmpDecayParam->load(),
                                  analogAmpSustainParam->load() * 0.01f, analogAmpReleaseParam->load() });

    const auto fmCarrierWaveform = fmWaveformForChoiceIndex(juce::jlimit(0, 2, (int) fmCarrierWaveformParam->load()));
    const auto fmCarrierOctaveShift = 2 - juce::jlimit(0, analogOctaveChoiceCount - 1, (int) fmCarrierOctaveParam->load());
    const auto fmCarrierVolumeBase = fmCarrierVolumeParam->load() * 0.01f;
    const auto fmVelocityToCarrierAmount = fmVelocityToCarrierParam->load() * 0.01f;
    fmCarrierEnv.setParameters({ fmCarrierAttackParam->load(), fmCarrierDecayParam->load(),
                                  fmCarrierSustainParam->load() * 0.01f, fmCarrierReleaseParam->load() });

    const auto fmModulatorWaveform = fmWaveformForChoiceIndex(juce::jlimit(0, 2, (int) fmModulatorWaveformParam->load()));
    const auto fmModulatorOctaveShift = 2 - juce::jlimit(0, analogOctaveChoiceCount - 1, (int) fmModulatorOctaveParam->load());
    const auto fmModulatorVolumeBase = fmModulatorVolumeParam->load() * 0.01f;
    const auto fmVelocityToBrightnessAmount = fmVelocityToBrightnessParam->load() * 0.01f;
    fmModulatorEnv.setParameters({ fmModulatorAttackParam->load(), fmModulatorDecayParam->load(),
                                    fmModulatorSustainParam->load() * 0.01f, fmModulatorReleaseParam->load() });

    const auto fmModulatorBrightnessAmount = fmModulatorBrightnessParam->load() * 0.01f;
    const auto fmBrightnessK = fmModulatorBrightnessAmount * fmBrightnessMaxK;
    const auto fmBrightnessMakeup = 1.0f + fmModulatorBrightnessAmount * fmBrightnessMakeupRange;

    const auto arpOn = arpEnabledParam->load() > 0.5f;
    const auto arpHoldOn = arpHoldParam->load() > 0.5f;
    if (wasHoldEnabled && !arpHoldOn)
        latchedNotes.clear(); // Hold just released - fall back to tracking physically-held keys only
    wasHoldEnabled = arpHoldOn;

    double arpStepSeconds;
    if (arpSyncParam->load() > 0.5f)
    {
        const auto index = juce::jlimit(0, (int) std::size(arpSubdivisions) - 1, (int) arpDivisionParam->load());
        const auto quarterNoteSeconds = 60.0 / getCurrentBpm();
        arpStepSeconds = quarterNoteSeconds * arpSubdivisions[(size_t) index].quarterNoteMultiple;
    }
    else
    {
        arpStepSeconds = 1.0 / (double) juce::jmax(0.01f, arpRateParam->load());
    }
    const auto arpStepLengthSamples = juce::jmax(1, (int) (arpStepSeconds * sampleRateHz));
    const auto arpGateAmount = arpGateParam->load() * 0.01f;
    const auto arpGateCloseSamples = juce::jlimit(1, arpStepLengthSamples, (int) (arpStepLengthSamples * arpGateAmount));

    // Sync locks step boundaries to the host's PPQ position (quarter notes since the timeline's
    // start) rather than to a free-running sample count, so steps land exactly on the beat grid
    // no matter when playback started or the arp was (re)triggered.
    bool arpUseHostSync = false;
    double arpHostPpqPosition = 0.0;
    double arpPpqIncrementPerSample = 0.0;
    double arpStepPpqLength = 1.0;
    if (arpSyncParam->load() > 0.5f)
    {
        if (auto* playHead = getPlayHead())
        {
            if (auto position = playHead->getPosition())
            {
                if (auto ppq = position->getPpqPosition())
                {
                    const auto index = juce::jlimit(0, (int) std::size(arpSubdivisions) - 1, (int) arpDivisionParam->load());
                    arpStepPpqLength = (double) arpSubdivisions[(size_t) index].quarterNoteMultiple;
                    arpHostPpqPosition = *ppq;
                    arpPpqIncrementPerSample = (getCurrentBpm() / 60.0) / sampleRateHz;
                    arpUseHostSync = true;
                }
            }
        }
    }

    const auto mixDriveAmount = mixDriveParam->load() * 0.01f;
    const auto mixDriveK = mixDriveAmount * mixDriveMaxK;
    const auto mixDriveMakeup = 1.0f + mixDriveAmount * mixDriveMakeupRange;

    const auto mixToneAmount = mixToneParam->load() * 0.01f;
    const auto mixToneLog2 = juce::jmap(mixToneAmount, 0.0f, 1.0f, std::log2(mixToneMinHz), std::log2(mixToneMaxHz));
    mixToneFilter.setCutoffFrequency(juce::jlimit(mixToneMinHz, (float) (sampleRateHz * 0.49), std::pow(2.0f, mixToneLog2)));

    // Deepens toward mixToneEqMaxCutDb as Tone moves left (toward 0%), 0dB at Tone=100%.
    const auto mixToneEqGainDb = juce::jmap(mixToneAmount, 0.0f, 1.0f, mixToneEqMaxCutDb, 0.0f);
    mixToneEqFilter.coefficients = juce::dsp::IIR::Coefficients<float>::makePeakFilter(
        sampleRateHz, mixToneEqHz, mixToneEqQ, juce::Decibels::decibelsToGain(mixToneEqGainDb));

    const auto mixOutputGain = juce::Decibels::decibelsToGain(mixOutputParam->load() + masterOutputTrimDb);

    // Squared so the low end of the knob stays subtle (per the original spec) while the high end
    // ramps up sharply into the extreme, near-broken-VCO territory the higher ageMax*Cents ceilings
    // now allow.
    const auto ageAmountRaw = mixAgeParam->load() * 0.01f;
    const auto ageAmount = ageAmountRaw * ageAmountRaw;
    const auto ageDriftCoeff = 1.0f - std::exp(-juce::MathConstants<float>::twoPi * ageDriftCutoffHz / (float) sampleRateHz);
    const auto ageWarbleCoeff = 1.0f - std::exp(-juce::MathConstants<float>::twoPi * ageWarbleCutoffHz / (float) sampleRateHz);

    // One-pole glide coefficient, recomputed every block from the live Glide Time parameter -
    // see the header comment on currentSemitone/glideTargetSemitone for why this isn't
    // juce::SmoothedValue.
    const auto glideSeconds = juce::jmax(0.0f, analogGlideTimeParam->load());
    const auto glideCoeff = glideSeconds > 0.0001f
                                 ? 1.0f - std::exp(-1.0f / (glideSeconds * (float) sampleRateHz))
                                 : 1.0f;

    const auto fmModulatorEnvSmoothCoeff = 1.0f - std::exp(-1.0f / (fmModulatorEnvSmoothingSeconds * (float) sampleRateHz));

    // Symmetric detune spread across the unison voices (e.g. 4 voices -> offsets at
    // -1.5d, -0.5d, +0.5d, +1.5d "detune steps" so the set is centred on 0).
    std::array<float, maxUnisonVoices> unisonCentsOffsets {};
    // Block-constant per-voice pitch multiplier (unisonCentsOffsets doesn't vary within a block) -
    // reused per sample below whenever Age is off (the default), when the per-sample pow() in the
    // loop would otherwise recompute this exact same value every sample for no reason.
    std::array<float, maxUnisonVoices> unisonPitchMultipliers {};
    for (int v = 0; v < unisonVoices; ++v)
    {
        const auto t = unisonVoices > 1 ? (float) v / (float) (unisonVoices - 1) - 0.5f : 0.0f;
        unisonCentsOffsets[(size_t) v] = t * maxDetuneCents * detuneAmount;
        unisonPitchMultipliers[(size_t) v] = std::pow(2.0f, unisonCentsOffsets[(size_t) v] / 1200.0f);
    }
    // Loudness-compensate so adding more unison voices thickens rather than just getting
    // louder in proportion to voice count.
    const auto unisonGain = 1.0f / std::sqrt((float) unisonVoices);

    const auto numChannels = buffer.getNumChannels();

    int midiEventIndex = 0;
    auto midiIterator = midiMessages.cbegin();
    const auto midiEnd = midiMessages.cend();

    for (int sample = 0; sample < numSamples; ++sample)
    {
        while (midiIterator != midiEnd && (*midiIterator).samplePosition == sample)
        {
            handleMidiMessage((*midiIterator).getMessage());
            ++midiIterator;
        }
        juce::ignoreUnused(midiEventIndex);

        if (arpOn)
        {
            if (arpUseHostSync)
            {
                const auto currentPpq = arpHostPpqPosition + (double) sample * arpPpqIncrementPerSample;
                const auto stepNumber = (juce::int64) std::floor(currentPpq / arpStepPpqLength);
                const auto phaseFraction = (currentPpq - (double) stepNumber * arpStepPpqLength) / arpStepPpqLength;

                if (!arpSyncedStepValid || stepNumber != arpSyncedStepNumber)
                {
                    arpSyncedStepNumber = stepNumber;
                    arpSyncedStepValid = true;
                    advanceArpStep();
                    arpGateIsOpen = true;
                }
                else if (arpGateIsOpen && phaseFraction >= (double) arpGateAmount)
                {
                    triggerVoiceNoteOff();
                    arpGateIsOpen = false;
                }
            }
            else
            {
                if (arpStepSampleCounter == 0)
                {
                    advanceArpStep();
                    arpGateIsOpen = true;
                }
                else if (arpGateIsOpen && arpStepSampleCounter >= arpGateCloseSamples)
                {
                    triggerVoiceNoteOff();
                    arpGateIsOpen = false;
                }

                if (++arpStepSampleCounter >= arpStepLengthSamples)
                    arpStepSampleCounter = 0;
            }
        }
        else
        {
            // So turning the arp back on later always starts on a fresh step immediately,
            // rather than resuming mid-way through wherever the counter (or synced step) last was.
            arpStepSampleCounter = 0;
            arpGateIsOpen = false;
            arpSyncedStepValid = false;
        }

        currentSemitone += (glideTargetSemitone - currentSemitone) * glideCoeff;
        const auto baseNoteHz = cachedNoteToHz(currentSemitone + (float) (octaveShift * 12), lastBaseNoteArg, lastBaseNoteHz);

        // Age: two independently-lowpassed noise walks (slow "drift", faster "warble") applied
        // only to the Analog VCO's pitch - all unison voices drift together as one VCO would,
        // on top of their existing relative detune spread. Sub and FM are untouched.
        const auto ageNoiseSample = ageNoiseRandom.nextFloat() * 2.0f - 1.0f;
        ageDriftState += (ageNoiseSample - ageDriftState) * ageDriftCoeff;
        ageWarbleState += (ageNoiseSample - ageWarbleState) * ageWarbleCoeff;
        const auto ageCentsOffset = ageAmount * (ageDriftState * ageMaxDriftCents + ageWarbleState * ageMaxWarbleCents);

        float analogSum = 0.0f;
        for (int v = 0; v < unisonVoices; ++v)
        {
            // ageCentsOffset is exactly 0.0f whenever Age is off (0.0f * finite == 0.0f in
            // IEEE-754) - the block-constant multiplier above is then bit-identical to computing
            // pow() fresh here, so reuse it instead. Falls back to the original per-sample
            // computation, unchanged, whenever Age is actually perturbing pitch.
            const auto voiceHz = ageCentsOffset == 0.0f
                                      ? baseNoteHz * unisonPitchMultipliers[(size_t) v]
                                      : baseNoteHz * std::pow(2.0f, (unisonCentsOffsets[(size_t) v] + ageCentsOffset) / 1200.0f);
            analogOscillators[(size_t) v].setFrequency(voiceHz, sampleRateHz);
            analogSum += analogOscillators[(size_t) v].renderAndAdvance(waveform);
        }
        analogSum *= unisonGain;

        float subRaw = 0.0f;
        if (subOn)
        {
            const auto subHz = cachedNoteToHz(currentSemitone + (float) (octaveShift * 12) + (float) (subOctaveShift * 12), lastSubNoteArg, lastSubNoteHz);
            subOscillator.setFrequency(subHz, sampleRateHz);
            subRaw = subOscillator.renderAndAdvance(subWaveform);
            subRaw = subShelfFilter.processSample(subRaw);
        }

        // Sub joins the mix here, before the filter - matching how a real analog synth's
        // oscillator mixer (osc + sub + noise) feeds a single shared VCF, rather than the sub
        // bypassing it. analogVolume/subVolume are the mixer-stage levels; ampEnvValue below is
        // the single VCA stage shared by the whole mixed signal, not per-oscillator.
        const auto mixedPreFilter = analogSum * analogVolume + subRaw * subVolume;

        const auto filterEnvValue = analogFilterEnv.getNextSample();
        // Skip the pow()/tan() (inside setCutoffFrequency()) below when neither genuinely
        // per-sample-varying input has changed since the last sample - true for the entire
        // sustain portion of a held note (ADSR::getNextSample() returns exactly the same value
        // every call in that state) and for idle gaps between notes (exactly 0.0f every call),
        // a bass synth's dominant use pattern. -1.0f is a provably-impossible sentinel for both:
        // ADSR values are always in [0,1], velocity is always >= 0.
        if (std::abs(filterEnvValue - lastFilterEnvValue) > 0.0f || std::abs(currentVelocity - lastCutoffVelocity) > 0.0f)
        {
            lastFilterEnvValue = filterEnvValue;
            lastCutoffVelocity = currentVelocity;
            const auto cutoffLog2 = baseCutoffLog2
                                     + filterEnvAmount * filterEnvMaxOctaves * filterEnvValue
                                     + velToFilterAmount * filterVelocityMaxOctaves * currentVelocity;
            const auto cutoffHz = juce::jlimit(20.0f, (float) (sampleRateHz * 0.49), std::pow(2.0f, cutoffLog2));
            analogFilter.setCutoffFrequency(cutoffHz);
        }

        const auto filtered = analogFilter.processSample(0, mixedPreFilter);

        const auto ampEnvValue = analogAmpEnv.getNextSample();
        const auto analogAndSubOut = filtered * ampEnvValue;

        // FM Bass: modulator's own envelope shapes how much it drives the carrier over the
        // note's life (a full ADSR here, not just a static depth) - that's what gives a classic
        // FM patch its "bright pluck settling into a duller tone" character. Not routed through
        // analogFilter - a separate voice, summed with the analog+sub layer only at the very end.
        const auto fmModulatorHz = cachedNoteToHz(currentSemitone + (float) (fmModulatorOctaveShift * 12), lastFmModulatorNoteArg, lastFmModulatorNoteHz);
        fmModulatorOscillator.setFrequency(fmModulatorHz, sampleRateHz);
        const auto modulatorRaw = fmModulatorOscillator.renderAndAdvance(fmModulatorWaveform);
        // Brightness: waveshapes the raw Modulator oscillator itself (adding harmonics), before
        // Volume/velocity/envelope scale how much of it reaches the carrier - independent of those,
        // since it changes the modulator's own timbre rather than the overall FM depth.
        const auto modulatorShaped = fmBrightnessK > 0.0f
                                          ? (std::tanh(modulatorRaw * fmBrightnessK) / fmBrightnessK) * fmBrightnessMakeup
                                          : modulatorRaw;
        const auto modulatorEnvValue = fmModulatorEnv.getNextSample();
        fmModulatorEnvSmoothed += (modulatorEnvValue - fmModulatorEnvSmoothed) * fmModulatorEnvSmoothCoeff;
        // Velocity To Brightness: louder notes drive more Modulator depth (= more sidebands/
        // harmonic content, i.e. a brighter FM timbre), read live each sample so it responds
        // immediately to a note-on rather than lagging a block behind.
        const auto fmModulatorVolume = fmModulatorVolumeBase * (1.0f + fmVelocityToBrightnessAmount * currentVelocity * velToFmMaxBoost);
        const auto modulatorOutput = modulatorShaped * fmModulatorEnvSmoothed * fmModulatorVolume;

        const auto fmCarrierHz = cachedNoteToHz(currentSemitone + (float) (fmCarrierOctaveShift * 12), lastFmCarrierNoteArg, lastFmCarrierNoteHz);
        fmCarrierOscillator.setFrequency(fmCarrierHz, sampleRateHz);
        const auto carrierRaw = fmCarrierOscillator.renderAndAdvanceWithPhaseOffset(
            fmCarrierWaveform, modulatorOutput * fmMaxModulationIndex);
        const auto carrierEnvValue = fmCarrierEnv.getNextSample();
        // Velocity To Carrier: louder notes are louder, on top of the knob-set Carrier Volume.
        const auto fmCarrierVolume = fmCarrierVolumeBase * (1.0f + fmVelocityToCarrierAmount * currentVelocity * velToFmMaxBoost);
        const auto fmOut = carrierRaw * carrierEnvValue * fmCarrierVolume;

        const auto combined = analogAndSubOut + fmOut;

        const auto driven = mixDriveK > 0.0f ? (std::tanh(combined * mixDriveK) / mixDriveK) * mixDriveMakeup : combined;
        const auto eqShaped = mixToneEqFilter.processSample(driven);
        const auto toned = mixToneFilter.processSample(0, eqShaped);
        const auto withOutputGain = toned * mixOutputGain;

        // Fixed safety soft-clip, always on - see mixSafetyDrive. Skip the tanh() call itself
        // during exact silence (idle gaps between notes, once the filter tail has fully decayed
        // to 0) - tanh(0)==0 exactly per IEEE-754/C++ <cmath>, so this is bit-identical, not an
        // approximation.
        const auto out = std::abs(withOutputGain) <= 0.0f ? withOutputGain : std::tanh(withOutputGain * mixSafetyDrive) / mixSafetyDrive;

        for (int channel = 0; channel < numChannels; ++channel)
            buffer.setSample(channel, sample, out);
    }
}

// createEditor() lives in PluginEditor.cpp (not here) specifically so this file has no
// PluginEditor.h/GUI dependency - AlloyTests links only this file plus juce_audio_processors/juce_dsp.

bool AlloyAudioProcessor::hasEditor() const { return true; }

const juce::String AlloyAudioProcessor::getName() const { return JucePlugin_Name; }

bool AlloyAudioProcessor::acceptsMidi() const { return true; }
bool AlloyAudioProcessor::producesMidi() const { return false; }
bool AlloyAudioProcessor::isMidiEffect() const { return false; }
double AlloyAudioProcessor::getTailLengthSeconds() const { return 2.0; }

int AlloyAudioProcessor::getNumPrograms() { return factoryPresets.getNumPrograms(); }
int AlloyAudioProcessor::getCurrentProgram() { return factoryPresets.getCurrentProgram(); }
void AlloyAudioProcessor::setCurrentProgram(int index) { factoryPresets.setCurrentProgram(index, apvts); }
const juce::String AlloyAudioProcessor::getProgramName(int index) { return factoryPresets.getProgramName(index); }

void AlloyAudioProcessor::changeProgramName(int, const juce::String&) {}

void AlloyAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    if (auto state = apvts.copyState(); state.isValid())
    {
        if (auto xml = state.createXml())
            copyXmlToBinary(*xml, destData);
    }
}

void AlloyAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(apvts.state.getType()))
            apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AlloyAudioProcessor();
}
