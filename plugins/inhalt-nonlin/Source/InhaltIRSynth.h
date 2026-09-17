#pragma once

#include "../../common/dsp/BandShelf.h"
#include "../../common/dsp/CircularDelayBuffer.h"
#include "../../common/dsp/OnePoleFilter.h"

#include <array>
#include <vector>

// Time-domain synthesis of Inhalt's gated stereo impulse response - the offline C++ counterpart
// to ml-toolkit/effects/nonlin/model.py's NonLinGatedFDN. That Python module renders via
// frequency-sampling ONLY for gradient-fit speed (hundreds of Adam steps); this class is called
// once per parameter change on a background thread (see InhaltIRWorker), so a plain per-sample
// time-domain simulation - exact, no circular-aliasing concern at all - is both simpler and
// sufficient. Keep the two in sync by construction, not by convention: every constant here
// (delay sets, feedback-gain ceiling, gate-envelope formula) is copied from that module's own
// constants/derivation, not re-derived - see model.py's docstring for why each one is what it is.
//
// Two fully INDEPENDENT 8-line tanks (left/right), not one shared Hadamard-mixed tank split
// across channels the way AuraFDNEngine.h's does (see that file's own header comment on why:
// mixing all 8 lines through one matrix means the two halves share every mode, which is not
// decorrelation). The measured hardware (effects/nonlin/findings.md) runs two genuinely
// independent networks - IACC over +-1ms of 0.006-0.037, not a fixed inter-channel delay
// masquerading as decorrelation.
//
// JUCE-free, like every other hand-rolled DSP class in this catalog (ShieldsFDNEngine,
// AuraFDNEngine, ...) - callable from a standalone clang++ diagnostic, not just from JUCE code.
namespace inhalt
{

class InhaltIRSynth
{
public:
    struct Params
    {
        // Tank (in feedback loop).
        float feedbackGain = 0.78f;   // 0 < gain <= maxFeedbackGain
        float dampingWeight = 0.5f;   // 0 < weight <= maxDampingWeight, one-pole per-line HF damping

        // Input tilt (High's tonal effect) - applied ONCE to the closed-loop tank output, not
        // recirculated; see model.py's NonLinGatedFDN.forward() for why this is mathematically an
        // output-stage multiply on the whole tank spectrum even though it's conceptually "the
        // input tilt" (LTI systems commute a single non-recirculated filter stage either way).
        float tiltLowGain = 1.0f;
        float tiltHighGain = 1.0f;
        float tiltPivotHz = 1500.0f;

        // Gate envelope (Time's timing effect) - explicit, dB-domain, applied after the tank/tilt
        // stage. Every field here has a directly-measured twin in
        // core.features.gate_envelope_params() - see effects/nonlin/findings.md.
        float buildUpMs = 3.0f;
        float plateauDroopDbPerSec = 0.0f;
        float kneeTimeMs = 150.0f;
        float fallRateDbPerSec = -250.0f;
        float kneeSoftnessMs = 4.0f;

        // Input diffuser (ahead of both tanks - see the Diffuser struct's own comment; TWO
        // independent instances, one per channel, since the direct tap below reads their raw
        // output). Fitted jointly with feedbackGain/dampingWeight, not hand-tuned - see
        // ml-toolkit/effects/nonlin/model.py's DIFFUSER_DELAY_SAMPLES_AT_44K/MAX_DIFFUSER_GAIN and
        // core.fit.onset_density_loss for why this exists and how it's fit.
        float diffuserGain = 0.45f;

        // Direct/early-arrival tap - each channel's OWN diffuser output, scaled by this gain and
        // summed with that channel's tank output BEFORE tilt/gate. Added after a real, ear-caught
        // complaint ("the convolution version sounds almost like a bow across strings, while the
        // version you built still retains the pluck/attack/envelope of the synth"): direct
        // measurement of the real captures found their own response starts at 13-16% of eventual
        // peak on the VERY FIRST SAMPLE (RMS over the first 10ms averaging ~35-49% of the
        // established 40-50ms RMS) - genuinely immediate, not built up from silence. This engine's
        // tank alone cannot reproduce that: its shortest delay line (~10ms) means the tank's own
        // output is EXACTLY ZERO until that first round trip completes, a true silence gap the
        // real hardware doesn't have. Convolving that silent gap with a percussive input passes
        // the input's own attack through completely unprocessed for ~10ms, before the reverb
        // "catches up" - audibly different from real hardware, which starts blending from sample
        // zero. Hand-measured constant (Time/High-independent within the real capture set's own
        // sampling) - see InhaltParameterMap.cpp's own directGainConstant comment for the
        // calibration and its known residual gap at very negative High.
        float directGain = 0.96f;
    };

    static constexpr int numLines = 8;
    static constexpr int numDiffuserStages = 3;

    // Matches ml-toolkit/effects/nonlin/model.py's MAX_FEEDBACK_GAIN exactly - see that module's
    // "Feedback gain ceiling" docstring section for the empirical derivation (0.985, inherited
    // from AmbienceFDN, left an under-converged near-DC mode at this topology's ~16ms mean delay;
    // 0.95 measured two orders of magnitude better). This C++ engine doesn't face the same
    // frequency-sampling aliasing risk (it's an exact time-domain simulation), but the ceiling is
    // still capped here to keep the two implementations' valid parameter range identical - a
    // ParameterMap-fitted value the Python side would refuse is not something this should render
    // "successfully" with different-sounding results.
    static constexpr float maxFeedbackGain = 0.95f;
    static constexpr float maxDampingWeight = 0.99f;

    // Renders `numSamples` of a stereo gated IR at `sampleRate` into left/right (resized to
    // numSamples, overwritten). Allocates (line buffer sizing) - never call this on the audio
    // thread; see InhaltIRWorker, which is exactly the class built to keep this off it.
    static void render(const Params& params, double sampleRate, int numSamples,
                        std::vector<float>& left, std::vector<float>& right);

private:
    struct Tank
    {
        std::array<wildjag::dsp::CircularDelayBuffer, numLines> lineBuffers;
        std::array<int, numLines> lineDelaySamples {};
        std::array<wildjag::dsp::OnePoleFilter, numLines> dampingFilter;

        void prepare(const std::array<float, numLines>& delayMs, double sampleRate);

        // Runs one sample: reads each line, dampens, Hadamard-mixes, scales by feedbackGain,
        // injects `input` (the impulse - 1.0 at n=0, 0.0 thereafter) and writes back. Returns the
        // tank's raw output for this sample (sum of the values read at the START of the call,
        // before this sample's mix/inject - matches AuraFDNEngine.cpp's own convention).
        float processSample(float input, float feedbackGain, float dampingWeight) noexcept;

        void reset() noexcept;
    };

    // A short chain of Schroeder allpass diffusers, applied to the impulse before it feeds a tank
    // (matches model.py's allpass_chain_transfer_function - see that function's docstring for the
    // full "why this exists" story: a bare impulse into an 8-line FDN measured a real, ear-caught
    // onset-density gap against the real hardware captures). TWO independent instances (different
    // delay sets, matching leftDelaysMs/rightDelaysMs's own asymmetric-decorrelation convention) -
    // each channel's OWN diffuser output also feeds that channel's own direct/early tap (see
    // Params::directGain), so a shared mono diffuser would have correlated the two channels'
    // earliest arrivals, undoing the stereo decorrelation work already done. Delays are FIXED
    // (topology, like the tank's own delay lines - not fitted); diffuserGain is the one fitted
    // parameter, shared across both instances and every stage.
    struct Diffuser
    {
        std::array<wildjag::dsp::CircularDelayBuffer, numDiffuserStages> stageBuffers;
        std::array<int, numDiffuserStages> stageDelaySamples {};

        void prepare(const std::array<float, numDiffuserStages>& delayMs, double sampleRate);
        float processSample(float input, float gain) noexcept;
        void reset() noexcept;
    };

    static const std::array<float, numLines> leftDelaysMs;
    static const std::array<float, numLines> rightDelaysMs;
    static const std::array<std::array<float, numLines>, numLines> hadamard;
    static const std::array<float, numDiffuserStages> leftDiffuserDelaysMs;
    static const std::array<float, numDiffuserStages> rightDiffuserDelaysMs;
};

} // namespace inhalt
