#pragma once

#include <array>

#include "../../common/dsp/CircularDelayBuffer.h"
#include "../../common/dsp/OnePoleFilter.h"

// The AMS RMX16 "Ambience" reverb core: an 8-line Hadamard-mixed FDN, direct hand-port of
// ml-toolkit/effects/ambience/model.py's AmbienceFDN (the model actually fit against 65 real
// captures - see ml-toolkit/effects/ambience/findings.md), not a re-derivation from scratch.
//
// Deliberately simpler than IntruderFDNEngine/ShieldsFDNEngine: no input diffuser, no envelope
// shaper/gate-retrigger, no delay-line wobble. findings.md found Ambience needs none of those -
// short-Time captures show individually-resolvable early echoes and longer-Time captures a dense
// wash, both consistent with one fixed-topology FDN's natural behavior at different feedback
// gains, not a separate early-reflection or gating mechanism (unlike Intruder's Non-Lin 2, which
// genuinely needed a hold/decay envelope stage - see IntruderFDNEngine.h's own class comment).
//
// Signal flow per line, matching AmbienceFDN.forward()'s frequency-domain derivation exactly
// (see core/dsp_primitives.py's module docstring for why the Python side fits via frequency-
// sampling rather than simulating this same per-sample loop - fitting speed only, this engine IS
// the per-sample simulation):
//   read (per-line delay) -> per-line HF damping (OnePoleFilter, one shared weight, independent
//   state per line) -> Hadamard mix -> per-line low/high-band feedback shelf (TiltFilter, one
//   shared low/high gain pair, independent state per line - this is what AmbienceFDN's
//   `feedback_response` shelf becomes in the time domain) -> write back with new input injected.
//
// highBandGain/lowBandGain/dampingWeight are DSP-ready coefficients from
// AuraParameterMap::mapTimeAndHighToBandGains(), not raw Time/High values - same separation of
// concerns as Intruder/Shields (the engine has no measured-data/interpolation logic of its own).
//
// One acknowledged, not-yet-independently-validated deviation from the fitted model: the Python
// fit was against MONO-mixed captures (core/io.py's load_audio mixes L+R down before fitting, and
// AmbienceFDN.forward() renders a single mono waveform), but this engine splits the 8 lines
// across stereo (4 lines feeding each of L/R, even/odd convention) for a proper stereo image,
// matching every other reverb in this catalog (Shields, Intruder). Overall decay time shouldn't
// meaningfully change (both halves keep a similar geometric spread of delay lengths and the same
// gain/damping values), but each channel's echo density is now lower than what B8's cross-
// validation actually measured (which compared mono renders throughout). Check this specifically
// in Phase D - if the real plugin's per-channel density reads noticeably sparser than the
// reference captures, feeding all 8 lines from a mono sum of the dry input (at the cost of stereo
// width) is the fallback.
class AuraFDNEngine
{
public:
    AuraFDNEngine() = default;

    void prepare(double sampleRate);
    void reset();

    void setPreDelayMs(float ms);

    // Feedback-path coefficients (already mapped from Time/High via AuraParameterMap) - clamped
    // here to a safe ceiling below 1.0 regardless of what the caller passes in. Not just paranoia:
    // FittedCurve1D linearly extrapolates beyond the 0.1-5.5s/-8..0dB measured range, and the UI's
    // own Time range extends past 5.5s (see PluginProcessor.cpp) - an extrapolated gain that
    // crept past unity would stop the signal decaying entirely rather than just decaying slowly,
    // the same class of bug the model-fitting side hit and fixed with a bounded
    // reparameterization (see AmbienceFDN's class comment) - this is that same ceiling's
    // real-time-engine equivalent.
    void setBandGains(float highBandGain, float lowBandGain);
    void setDampingWeight(float weight);

    // In-place stereo process: L/R in, replaced with the wet signal out. Dry/wet mixing and
    // input/output gain happen in the processor, not here - same convention as Shields/Intruder.
    void processStereo(float* left, float* right, int numSamples);

private:
    static constexpr int numLines = 8;

    // Fixed topology (delay lengths + mixing matrix) - matches
    // ml-toolkit/effects/ambience/model.py's _BASE_DELAY_SAMPLES_AT_44K exactly (converted to ms)
    // and hadamard_matrix(8)'s Sylvester construction. This is what the fitted curves in
    // AuraReferenceData.h were actually fit against - changing these without re-fitting would
    // silently invalidate every curve.
    static constexpr std::array<float, numLines> baseLineLengthsMs {
        30.0907f, 35.3515f, 41.0658f, 47.5964f, 55.2608f, 60.8390f, 68.0045f, 75.8050f
    };

    static const std::array<std::array<float, numLines>, numLines> hadamard;

    // Same shelf pivot as model.py's _SHELF_PIVOT_HZ.
    static constexpr float shelfPivotHz = 1000.0f;

    // See setBandGains()'s comment.
    static constexpr float maxFeedbackGain = 0.985f;
    static constexpr float maxDampingWeight = 0.99f;

    // Independent low/high-band gain shelf, one-pole split - same structure as
    // common/dsp/TiltFilter.h, but with two independently-settable linear gains instead of one
    // symmetric tiltDb (TiltFilter always moves the two bands by equal-and-opposite dB amounts,
    // which doesn't fit here: model.py's fitted high_band_gain/low_band_gain are independent
    // values, not a symmetric tilt around a center). Not added as a new TiltFilter capability -
    // per AGENTS.md's LookAndFeel-extension convention applied the same way to common/dsp/, a
    // new shared capability gets added to shared code once a second plugin needs it, not
    // speculatively for the first one that does.
    struct BandShelf
    {
        wildjag::dsp::OnePoleFilter lowpass;
        float lowGain = 1.0f;
        float highGain = 1.0f;

        void reset() noexcept { lowpass.reset(); }
        void setPivotHz(float hz, double sampleRate) noexcept { lowpass.setCutoffHz(hz, sampleRate); }

        float processSample(float x) noexcept
        {
            const auto low = lowpass.processSample(x);
            const auto high = x - low;
            return low * lowGain + high * highGain;
        }
    };

    double sampleRateHz = 44100.0;
    bool prepared = false;

    std::array<wildjag::dsp::CircularDelayBuffer, numLines> lineBuffers;
    std::array<int, numLines> lineDelaySamples {};
    std::array<wildjag::dsp::OnePoleFilter, numLines> dampingFilter;
    std::array<BandShelf, numLines> feedbackShelf;

    wildjag::dsp::CircularDelayBuffer preDelayBufferL, preDelayBufferR;
    int preDelaySamples = 0;
    int maxPreDelaySamples = 1;

    void updateLineLengths();
};
