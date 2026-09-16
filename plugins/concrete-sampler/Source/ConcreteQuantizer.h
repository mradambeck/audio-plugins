#pragma once

#include <algorithm>
#include <cmath>

// Phase 3: bit-depth reduction and the E-mu-style compand/expand curve (concrete-sampler-plugin-
// plan.md's Phase 3) - applied per-sample in ConcreteVoice right after the pitch engine, on the
// raw source value BEFORE the amp envelope/velocity gain, since on the real hardware the converter
// quantized whatever hit it, and velocity (where it affected level at all) was a post-conversion
// VCA stage, not baked into the stored bits.
//
// Framework-free like ConcretePitchEngine, so ConcreteTests exercises it directly with plain
// floats - no JUCE types needed. Deliberately has NO dither anywhere: none of the twelve machines
// had it, and it would clean up exactly the quantization grit this class exists to produce (see
// the plan's Phase 3 "No dither, ever").
class ConcreteQuantizer
{
public:
    enum class Mode
    {
        linear,     // Direct mid-tread rounding at bitDepthBits - a constant quantization step
                    // size regardless of signal level.
        companded,  // Compress (mu-law-shaped) before quantizing, expand after - the E-mu storage
                    // scheme (see the plan's machine table: Emulator II/Emax store 8-bit
                    // companded). The step size is constant in the COMPRESSED domain but
                    // corresponds to a smaller step near zero in the original domain, so
                    // quantization error scales with signal level instead of staying constant -
                    // that's the entire point, and why it isn't the same as a linear reduction to
                    // the same nominal bit depth.
    };

    // sample: normalised signal value (not necessarily clamped to [-1, 1] going in - clamped
    // internally before quantizing). bitDepthBits: the storage word's width, 1-16 (clamped). For
    // Mode::companded this describes the width of the COMPRESSED-domain word, matching how the
    // real machines describe their companded storage (e.g. "8-bit companded"), not a separate
    // linear reduction applied on top of compression.
    static float process(float sample, Mode mode, int bitDepthBits) noexcept
    {
        const auto bits = std::clamp(bitDepthBits, 1, 16);
        return mode == Mode::linear ? quantize(sample, bits)
                                     : expand(quantize(compress(sample), bits));
    }

private:
    // mu-law shaping, mu = 255 (the ITU-T G.711 constant) - a standard, well-characterized
    // logarithmic companding curve standing in for a scheme none of the source research pinned
    // down more precisely than "compressed 8-bit storage" (see the plan's Phase 3/machine-table
    // notes on the Emulator II and Emax); tune by ear against reference recordings later if needed.
    static constexpr float mu = 255.0f;

    static float compress(float sample) noexcept
    {
        const auto sign = sample < 0.0f ? -1.0f : 1.0f;
        const auto magnitude = std::min(std::abs(sample), 1.0f);
        return sign * std::log1p(mu * magnitude) / std::log1p(mu);
    }

    static float expand(float sample) noexcept
    {
        const auto sign = sample < 0.0f ? -1.0f : 1.0f;
        const auto magnitude = std::min(std::abs(sample), 1.0f);
        return sign * (std::pow(1.0f + mu, magnitude) - 1.0f) / mu;
    }

    // Mid-tread quantizer: (bits - 1) magnitude bits plus a sign, matching a signed PCM word of
    // that width - rounds to the nearest step. No noise is added anywhere in this function (see
    // the class comment on why dither never belongs here).
    static float quantize(float sample, int bits) noexcept
    {
        const auto levels = (float) (1 << (bits - 1));
        const auto clamped = std::clamp(sample, -1.0f, 1.0f);
        return std::round(clamped * levels) / levels;
    }
};
