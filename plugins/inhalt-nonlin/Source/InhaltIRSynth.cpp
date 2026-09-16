#include "InhaltIRSynth.h"

#include <cmath>

namespace inhalt
{

// Two disjoint, mutually non-simple-ratio delay sets, mean ~16-17ms - directly from
// ml-toolkit/effects/nonlin/model.py's LEFT_DELAY_SAMPLES_AT_44K / RIGHT_DELAY_SAMPLES_AT_44K
// (471,503,589,639,735,817,941,1067 and 483,549,607,699,781,867,979,1133 samples @44.1kHz),
// converted to milliseconds here so they scale to any session rate the same way every other
// engine in this catalog scales its own ms delay table (e.g. AuraFDNEngine::baseLineLengthsMs).
const std::array<float, InhaltIRSynth::numLines> InhaltIRSynth::leftDelaysMs { {
    10.680272f, 11.405896f, 13.356009f, 14.489796f, 16.666667f, 18.526077f, 21.337868f, 24.195011f,
} };
const std::array<float, InhaltIRSynth::numLines> InhaltIRSynth::rightDelaysMs { {
    10.952381f, 12.448980f, 13.764172f, 15.850340f, 17.709751f, 19.659864f, 22.199546f, 25.691610f,
} };

// Same fixed 8x8 Hadamard matrix as AuraFDNEngine.h/ShieldsFDNEngine.h - Sylvester construction,
// normalized by 1/sqrt(8) at the point of use (see processSample()).
const std::array<std::array<float, InhaltIRSynth::numLines>, InhaltIRSynth::numLines> InhaltIRSynth::hadamard { {
    { 1, 1, 1, 1, 1, 1, 1, 1 },
    { 1, -1, 1, -1, 1, -1, 1, -1 },
    { 1, 1, -1, -1, 1, 1, -1, -1 },
    { 1, -1, -1, 1, 1, -1, -1, 1 },
    { 1, 1, 1, 1, -1, -1, -1, -1 },
    { 1, -1, 1, -1, -1, 1, -1, 1 },
    { 1, 1, -1, -1, -1, -1, 1, 1 },
    { 1, -1, -1, 1, -1, 1, 1, -1 },
} };

void InhaltIRSynth::Tank::prepare(const std::array<float, numLines>& delayMs, double sampleRate)
{
    for (int i = 0; i < numLines; ++i)
    {
        const auto maxDelaySamples = (int) std::ceil(delayMs[(size_t) i] * 0.001 * sampleRate) + 4;
        lineBuffers[(size_t) i].setSize(maxDelaySamples);
        lineDelaySamples[(size_t) i] = (int) std::round(delayMs[(size_t) i] * 0.001 * sampleRate);
        dampingFilter[(size_t) i].reset();
    }
}

void InhaltIRSynth::Tank::reset() noexcept
{
    for (int i = 0; i < numLines; ++i)
    {
        lineBuffers[(size_t) i].reset();
        dampingFilter[(size_t) i].reset();
    }
}

float InhaltIRSynth::Tank::processSample(float input, float feedbackGain, float dampingWeight) noexcept
{
    std::array<float, numLines> lineOut {};
    for (int i = 0; i < numLines; ++i)
        lineOut[(size_t) i] = lineBuffers[(size_t) i].read(lineDelaySamples[(size_t) i] - 1);

    std::array<float, numLines> damped {};
    for (int i = 0; i < numLines; ++i)
    {
        dampingFilter[(size_t) i].setWeight(dampingWeight);
        damped[(size_t) i] = dampingFilter[(size_t) i].processSample(lineOut[(size_t) i]);
    }

    std::array<float, numLines> mixed {};
    constexpr float hadamardNorm = 0.353553390593f; // 1/sqrt(8)
    for (int i = 0; i < numLines; ++i)
    {
        float sum = 0.0f;
        for (int j = 0; j < numLines; ++j)
            sum += hadamard[(size_t) i][(size_t) j] * damped[(size_t) j];
        mixed[(size_t) i] = sum * hadamardNorm * feedbackGain;
    }

    for (int i = 0; i < numLines; ++i)
    {
        const auto lineIn = mixed[(size_t) i] + input;
        // NaN/Inf guard at the recirculation point, same convention as every other FDN engine in
        // this catalog (ShieldsFDNEngine/IntruderFDNEngine/AuraFDNEngine) - a poisoned sample must
        // not be allowed to circulate forever.
        lineBuffers[(size_t) i].write(std::isfinite(lineIn) ? lineIn : 0.0f);
    }

    // Taps the RAW delay-line output (read at the top of this call, before this sample's own
    // damping/mixing/feedback are computed for the NEXT round trip) - same convention
    // AuraFDNEngine.cpp uses for its own tankL/tankR sum, not the damped+mixed value. NOTE: this
    // means the audible tap isn't bit-identical to model.py's _render_tank(), which sums the
    // DAMPED+MIXED spectrum instead (Hmix @ DampY).sum(...) - the same gap exists between
    // AmbienceFDN.forward() and AuraFDNEngine.cpp's hand-port and was absorbed by re-calibrating
    // decayGain/dampingWeight against the C++ engine itself rather than transplanting the Python
    // fit's raw numbers (see AuraDecayGainData.h's own history). Treat this engine's output the
    // same way: verify and calibrate against ITS OWN behaviour (see InhaltCalibrateProbe), don't
    // assume the Python fit's numbers transfer unchanged.
    float sum = 0.0f;
    for (int i = 0; i < numLines; ++i)
        sum += lineOut[(size_t) i];
    constexpr float tankNorm = 0.353553390593f; // 1/sqrt(8) - 8 lines summed per channel
    return sum * tankNorm;
}

namespace
{
    // Numerically-stable softplus: log(1+exp(x)), computed to avoid overflow for large x (same
    // shape PyTorch's own F.softplus uses) - matches model.py's F_torch.softplus() exactly, so
    // the fitted t_knee_ms/tau_k_ms/fall_rate_db_per_s values render identically in both places.
    float softplus(float x) noexcept
    {
        if (x > 20.0f)
            return x;
        return std::log1p(std::exp(x));
    }

    float gateEnvelopeDb(float tSeconds, const InhaltIRSynth::Params& p) noexcept
    {
        const auto tauA = std::max(p.buildUpMs, 0.001f) * 0.001f;
        const auto tauK = std::max(p.kneeSoftnessMs, 0.001f) * 0.001f;
        const auto tKnee = p.kneeTimeMs * 0.001f;

        const auto attackLin = std::max(1.0f - std::exp(-tSeconds / tauA), 1e-6f);
        const auto attackDb = 20.0f * std::log10(attackLin);
        const auto plateauDb = p.plateauDroopDbPerSec * tSeconds;
        const auto kneeDb = p.fallRateDbPerSec * tauK * softplus((tSeconds - tKnee) / tauK);

        return attackDb + plateauDb + kneeDb;
    }
} // namespace

void InhaltIRSynth::render(const Params& params, double sampleRate, int numSamples,
                            std::vector<float>& left, std::vector<float>& right)
{
    left.assign((size_t) std::max(numSamples, 0), 0.0f);
    right.assign((size_t) std::max(numSamples, 0), 0.0f);
    if (numSamples <= 0)
        return;

    Tank leftTank, rightTank;
    leftTank.prepare(leftDelaysMs, sampleRate);
    rightTank.prepare(rightDelaysMs, sampleRate);

    const auto feedbackGain = std::min(std::max(params.feedbackGain, 0.0f), maxFeedbackGain);
    const auto dampingWeight = std::min(std::max(params.dampingWeight, 1e-6f), maxDampingWeight);

    wildjag::dsp::BandShelf tiltL, tiltR;
    tiltL.lowGain = tiltR.lowGain = params.tiltLowGain;
    tiltL.highGain = tiltR.highGain = params.tiltHighGain;
    tiltL.setPivotHz(params.tiltPivotHz, sampleRate);
    tiltR.setPivotHz(params.tiltPivotHz, sampleRate);

    for (int n = 0; n < numSamples; ++n)
    {
        const auto impulse = (n == 0) ? 1.0f : 0.0f;

        const auto tankL = leftTank.processSample(impulse, feedbackGain, dampingWeight);
        const auto tankR = rightTank.processSample(impulse, feedbackGain, dampingWeight);

        // Input tilt - see the Params struct's own comment on why this is applied here, to the
        // closed-loop tank output, rather than recirculated inside the loop.
        const auto tiltedL = tiltL.processSample(tankL);
        const auto tiltedR = tiltR.processSample(tankR);

        const auto tSeconds = (float) n / (float) sampleRate;
        const auto gateLin = std::pow(10.0f, gateEnvelopeDb(tSeconds, params) / 20.0f);

        left[(size_t) n] = tiltedL * gateLin;
        right[(size_t) n] = tiltedR * gateLin;
    }
}

} // namespace inhalt
