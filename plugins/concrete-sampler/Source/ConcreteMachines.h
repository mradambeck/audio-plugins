#pragma once

#include "ConcreteFilterModels.h"
#include "ConcretePitchEngine.h"
#include "ConcreteQuantizer.h"
#include "ConcreteVoice.h" // ConcreteAmpEnvelopeMode

#include <array>

// Phase 7: the twelve machines from concrete-sampler-plugin-plan.md's machine table, as data - one
// ConcreteMachine bundles everything a machine preset sets. Values marked (est.) in the plan's own
// table are reasonable starting points to be tuned by ear, not specifications - see that table for
// the full research and rationale behind every choice below. This file has no APVTS/JUCE-parameter
// dependency at all, just the plain enums/values a machine sets - PluginProcessor.cpp is what
// turns this table into both the host's native factory-preset list (getFactoryPresets()) and the
// live Machine parameter's own apply-on-change behavior (see its own comment on why both exist).
//
// Deliberately excluded from every machine here: filterCutoffHz/filterResonance01. The plan's
// table names which FILTER FAMILY each machine uses (its defining, characteristic trait) but never
// prescribes specific cutoff/resonance numbers - those stay live, user-adjustable performance
// controls at their existing fully-open/no-resonance defaults, the same "no coloration until asked
// for" convention every other control in this plugin already follows. Selecting a machine changes
// WHICH filter it uses, not where the knobs on it currently sit.
//
// Every machine also leaves captureBypass on (the capture pass itself OFF) - see the plan's table
// notes: "every preset ships with the capture pass off - it's a technique the user applies, not
// part of a machine's stock behavior." What a machine sets instead is captureTransposeSemitones:
// the value the Capture Transpose control lands on once the user manually engages it.
struct ConcreteMachine
{
    const char* name;
    ConcretePitchEngine::Mode pitchEngineMode;
    float baseRateHz;
    int bitDepthBits;
    ConcreteQuantizer::Mode quantizerMode;
    float captureTransposeSemitones;
    ConcreteFilterModel::Mode filterModel;
    int voiceCount;
    ConcreteAmpEnvelopeMode ampEnvelopeMode;
};

// Table order matches the plan's own machine table exactly - this order is also the live Machine
// parameter's AudioParameterChoice index order, offset by one to leave room for that parameter's
// own index 0 ("(Custom)" - see PluginProcessor.h's machineParamID for why), and the host factory-
// preset program list's index order (no offset there - see PluginProcessor.cpp's getFactoryPresets()).
inline const std::array<ConcreteMachine, 12>& getConcreteMachines()
{
    static const std::array<ConcreteMachine, 12> machines
    {{
        // Fixed-rate drop-sample (Mode B), the reconstruction filter's SSM2044 left "in circuit"
        // by default - the Filter Model control itself is the toggle for the plan's "ship two
        // variants or a toggle" note, since the user can flip it to Bypass at any time to hear the
        // real unit's filterless path. Highest out-of-band image energy of any preset, by design.
        { "E-mu SP-1200", ConcretePitchEngine::Mode::dropSampleDecimation, 26040.0f, 12,
          ConcreteQuantizer::Mode::linear, 5.0f, ConcreteFilterModel::Mode::ssm, 8,
          ConcreteAmpEnvelopeMode::adsr },

        // Variable-clock ZOH (Mode A), companded 8-bit storage (the E-mu family trait), SSM-family
        // filter. Warmer/lower-rate sibling of the Emax below.
        { "E-mu Emulator II", ConcretePitchEngine::Mode::variableClockZeroOrderHold, 27700.0f, 8,
          ConcreteQuantizer::Mode::companded, 5.0f, ConcreteFilterModel::Mode::ssm, 8,
          ConcreteAmpEnvelopeMode::adsr },

        // Brighter/higher-rate sibling of the Emulator II - same companding scheme, default base
        // rate around 42kHz per the plan's table notes.
        { "E-mu Emax", ConcretePitchEngine::Mode::variableClockZeroOrderHold, 42000.0f, 8,
          ConcreteQuantizer::Mode::companded, 5.0f, ConcreteFilterModel::Mode::ssm, 8,
          ConcreteAmpEnvelopeMode::adsr },

        // Akai/S900 lineage - no analog filter chip modeled (the plan's "gentle output stage" note
        // is a future refinement, not yet a distinct model - see that row's own text).
        { "Akai MPC60", ConcretePitchEngine::Mode::variableClockZeroOrderHold, 40000.0f, 12,
          ConcreteQuantizer::Mode::linear, 12.0f, ConcreteFilterModel::Mode::bypass, 16,
          ConcreteAmpEnvelopeMode::adsr },

        // Per-channel-card CEM filter with resonance loss - the 32kHz II/IIx rate, not the 24kHz
        // Series I variant (a reasonable second preset for later, per the plan's own note, not
        // built here).
        { "Fairlight CMI", ConcretePitchEngine::Mode::variableClockZeroOrderHold, 32000.0f, 8,
          ConcreteQuantizer::Mode::linear, 12.0f, ConcreteFilterModel::Mode::cemResonanceLoss, 8,
          ConcreteAmpEnvelopeMode::adsr },

        // No filter modeled, 16-bit/50kHz default (programmable up to 100kHz live via Base Rate) -
        // cleanest preset at root pitch by design.
        { "Synclavier II", ConcretePitchEngine::Mode::variableClockZeroOrderHold, 50000.0f, 16,
          ConcreteQuantizer::Mode::linear, 5.0f, ConcreteFilterModel::Mode::bypass, 8,
          ConcreteAmpEnvelopeMode::adsr },

        // The one machine using the contoured amp envelope (Phase 6) instead of a flat ADSR
        // sustain, modeling the CEM3335's dual-VCA amplitude contouring - "no analog filter model"
        // per the plan, hence Digital+VCA rather than one of the ladder models.
        { "Kurzweil K250", ConcretePitchEngine::Mode::variableClockZeroOrderHold, 44100.0f, 16,
          ConcreteQuantizer::Mode::linear, 5.0f, ConcreteFilterModel::Mode::digitalVca, 12,
          ConcreteAmpEnvelopeMode::contoured },

        // The one machine confirmed to use the resonance-COMPENSATED CEM3328 - holds passband
        // level as resonance rises, unlike Fairlight/Linn's uncompensated 3320 (see
        // analysis/verify_phase5.py's own CEM sweep check for the underlying behavior).
        { "Ensoniq Mirage", ConcretePitchEngine::Mode::variableClockZeroOrderHold, 28000.0f, 8,
          ConcreteQuantizer::Mode::linear, 12.0f, ConcreteFilterModel::Mode::cemResonanceCompensated, 8,
          ConcreteAmpEnvelopeMode::adsr },

        // 13-bit is the unusual, defining trait here - sits between the Mirage and the 16-bit
        // machines.
        { "Ensoniq EPS", ConcretePitchEngine::Mode::variableClockZeroOrderHold, 22000.0f, 13,
          ConcreteQuantizer::Mode::linear, 12.0f, ConcreteFilterModel::Mode::cemResonanceLoss, 8,
          ConcreteAmpEnvelopeMode::adsr },

        // The odd one out - delta-sigma (Mode C), character from noise shaping, not bit-depth
        // reduction, so bitDepth/quantizerMode stay at their neutral defaults (this machine's own
        // hardware has no comparable stage - see the plan's "Companding: N/A" for this row). No
        // filter chip distinct enough to warrant its own model - Bypass rather than implying a
        // saturation character (Digital+VCA) the real unit doesn't have. The plan doesn't assign
        // this machine to either capture-transpose default group (+5 or +12) and explicitly flags
        // it for separate verification - +5 here is a starting point, not a researched value.
        { "Ensoniq ASR-10", ConcretePitchEngine::Mode::deltaSigma, 30000.0f, 16,
          ConcreteQuantizer::Mode::linear, 5.0f, ConcreteFilterModel::Mode::bypass, 8,
          ConcreteAmpEnvelopeMode::adsr },

        // Per-voice cards, CEM filter with resonance loss (uncompensated, like the Fairlight) - the
        // 13-poly figure is used for Voice Count (the 18-multitimbral figure describes several
        // simultaneous different PARTS, which v1's single-zone engine doesn't model yet - see
        // maxVoices's own comment in PluginProcessor.h for why the pool's capacity is still 18).
        // 33kHz (distinct from the Mirage's own 28kHz pick above, still within this machine's own
        // 11-37kHz documented range) - CEM Loss and CEM Compensated are mathematically identical
        // at Filter Resonance's default of 0 (the compensation gain term is 1 + resonance*3.6, a
        // no-op at resonance 0 - see ConcreteFilterModels.h), so sharing a base rate with the
        // Mirage here would leave these two presets indistinguishable out of the box despite using
        // different filter models on paper - confirmed via analysis/verify_phase7.py's pairwise
        // null check, which is exactly the "any pair nulling to near-silence means one is
        // misconfigured" case the plan's own Phase 7 Analysis calls for.
        { "Linn 9000", ConcretePitchEngine::Mode::variableClockZeroOrderHold, 33000.0f, 8,
          ConcreteQuantizer::Mode::linear, 5.0f, ConcreteFilterModel::Mode::cemResonanceLoss, 13,
          ConcreteAmpEnvelopeMode::adsr },

        // The darkest/crudest preset by design - fixed 9.38kHz, one-pole lowpass, only 4 voices,
        // no per-voice card architecture on the real unit.
        { "Casio SK-1", ConcretePitchEngine::Mode::variableClockZeroOrderHold, 9380.0f, 8,
          ConcreteQuantizer::Mode::linear, 5.0f, ConcreteFilterModel::Mode::onePole, 4,
          ConcreteAmpEnvelopeMode::adsr },
    }};
    return machines;
}
