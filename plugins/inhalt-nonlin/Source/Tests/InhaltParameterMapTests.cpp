#include "../InhaltParameterMap.h"

#include <juce_core/juce_core.h>

#include <cmath>

class InhaltParameterMapTests : public juce::UnitTest
{
public:
    InhaltParameterMapTests() : juce::UnitTest("InhaltParameterMap", "Inhalt") {}

    void runTest() override
    {
        beginTest("gateLengthMsForDisplay reproduces the hand-measured hardware table");
        {
            // Directly from the project plan's "already measured" section - a real hardware
            // measurement, not this map's own placeholder data. Ties the shipped C++ back to the
            // hardware measurement the same way AuraParameterMapTests spot-checks its own fitted
            // data against findings.md.
            struct { float timeKnob, expectedMs; } table[] = {
                { 0.1f, 102.5f }, { 0.8f, 102.5f }, { 2.2f, 149.3f },
                { 4.8f, 216.6f }, { 7.0f, 278.1f }, { 9.8f, 300.4f },
            };
            for (const auto& row : table)
            {
                bool extrapolated = false;
                const auto measured = InhaltParameterMap::gateLengthMsForDisplay(row.timeKnob, &extrapolated);
                expect(std::abs(measured - row.expectedMs) < 5.0f,
                    "gateLengthMsForDisplay(" + juce::String(row.timeKnob) + ") should match the hardware table within 5ms");
                expect(! extrapolated, "table points are within the measured range and must not report extrapolation");
            }
        }

        beginTest("gateLengthMsForDisplay flags extrapolation outside the measured 0.1-9.8 range");
        {
            bool belowRange = false, aboveRange = false;
            InhaltParameterMap::gateLengthMsForDisplay(0.0f, &belowRange);
            InhaltParameterMap::gateLengthMsForDisplay(15.0f, &aboveRange);
            expect(belowRange, "below the measured range should be flagged extrapolated");
            expect(aboveRange, "above the measured range should be flagged extrapolated");
        }

        beginTest("gateLengthMsForDisplay is monotonically non-decreasing across the measured range");
        {
            float previous = -1.0f;
            for (float t = 0.1f; t <= 9.8f; t += 0.1f)
            {
                const auto value = InhaltParameterMap::gateLengthMsForDisplay(t);
                expect(value >= previous - 0.01f, "gate length should not decrease as Time increases");
                previous = value;
            }
        }

        beginTest("mapHighKnobToTilt reproduces the two measured endpoints and interpolates monotonically");
        {
            bool ex0 = false, ex9 = false;
            const auto atZero = InhaltParameterMap::mapHighKnobToTilt(0.0f, &ex0);
            const auto atNine = InhaltParameterMap::mapHighKnobToTilt(-9.0f, &ex9);

            // LTAS (whole-decay average spectrum) calibration - see build_measured_gate_curves.py's
            // own docstring. Superseded the real onset 5-band measurement this test originally
            // checked (+4.5dB low/-9.5dB high, findings.md's "High: broadband tilt" section) after
            // a real, ear-caught complaint ("Bringing it down to -9dB to match a -9dB IR it is
            // still much brighter and clear") - the onset-only window wasn't representative of the
            // SUSTAINED tone, and the fit's own tilt_pivot_hz (kept at the time because it "looked
            // physically plausible") turned out to structurally cap the achievable darkness well
            // below what the real captures measure over their whole decay. H=0 is exactly neutral
            // by construction (the offset curve is anchored there); H=-9's magnitude (+7.90dB low/
            // -16.67dB high) is now solved numerically against the real captures' own LTAS band
            // levels at a lowered (1500Hz, not ~4500Hz) pivot - see InhaltParameterMap.cpp's own
            // tiltPivotHz comment for why the pivot itself had to move, not just the gains.
            expect(std::abs(atZero.lowGain - 1.0f) < 1.0e-4f, "H=0 low gain should be neutral (1.0)");
            expect(std::abs(atZero.highGain - 1.0f) < 1.0e-4f, "H=0 high gain should be neutral (1.0)");
            expect(std::abs(atNine.lowGain - 2.4817f) < 1.0e-3f, "H=-9 low gain should match the LTAS calibration");
            expect(std::abs(atNine.highGain - 0.1468f) < 1.0e-3f, "H=-9 high gain should match the LTAS calibration");
            expect(! ex0 && ! ex9, "both range endpoints must not report extrapolation");

            bool first = true;
            float previousLow = 0.0f, previousHigh = 0.0f;
            for (float h = -9.0f; h <= 0.0f; h += 0.5f)
            {
                const auto tilt = InhaltParameterMap::mapHighKnobToTilt(h);
                if (! first)
                {
                    expect(tilt.lowGain <= previousLow + 0.01f, "low-band gain should not increase as High rises toward 0");
                    expect(tilt.highGain >= previousHigh - 0.01f, "high-band gain should not decrease as High rises toward 0");
                }
                previousLow = tilt.lowGain;
                previousHigh = tilt.highGain;
                first = false;
            }
        }

        beginTest("mapHighKnobToTilt flags extrapolation outside -9..0");
        {
            bool extrapolated = false;
            InhaltParameterMap::mapHighKnobToTilt(-20.0f, &extrapolated);
            expect(extrapolated, "below -9 should be flagged extrapolated");

            extrapolated = false;
            InhaltParameterMap::mapHighKnobToTilt(5.0f, &extrapolated);
            expect(extrapolated, "above 0 should be flagged extrapolated");
        }

        beginTest("mapTimeAndHighToGateParams produces sane, finite values across the whole Time range");
        {
            for (float t = 0.1f; t <= 9.8f; t += 0.5f)
            {
                const auto gate = InhaltParameterMap::mapTimeAndHighToGateParams(t, 0.0f);
                expect(std::isfinite(gate.buildUpMs) && gate.buildUpMs > 0.0f, "buildUpMs must be finite and positive");
                expect(std::isfinite(gate.kneeTimeMs) && gate.kneeTimeMs > 0.0f, "kneeTimeMs must be finite and positive");
                expect(std::isfinite(gate.fallRateDbPerSec) && gate.fallRateDbPerSec < 0.0f, "fallRateDbPerSec must be finite and negative");
                expect(std::isfinite(gate.kneeSoftnessMs) && gate.kneeSoftnessMs > 0.0f, "kneeSoftnessMs must be finite and positive");
            }
        }

        beginTest("kneeTimeMs is monotonically non-decreasing with Time (regression guard)");
        {
            // The Python fit's own t_knee_ms was found systematically wrong here (non-monotonic -
            // e.g. its Time=0.1 value was LONGER than its Time=9.8 value) and was replaced with a
            // curve built directly from measurement (see InhaltParameterMap.h's own comment and
            // ml-toolkit/effects/nonlin/build_measured_gate_curves.py). This holds that correction
            // in place: a future regeneration of InhaltReferenceData.h from a bad fit, without
            // re-running the correction script, would fail this test.
            float previous = -1.0f;
            for (float t = 0.1f; t <= 9.8f; t += 0.1f)
            {
                const auto gate = InhaltParameterMap::mapTimeAndHighToGateParams(t, 0.0f);
                expect(gate.kneeTimeMs >= previous - 0.01f, "kneeTimeMs should not decrease as Time increases");
                previous = gate.kneeTimeMs;
            }
        }

        beginTest("mapTimeKnobToTankParams produces sane, in-range values across the whole Time range");
        {
            // diffuserGain in particular is a new fitted parameter (see InhaltParameterMap.h's own
            // comment on why it exists) - this guards it lands in-range even if a future fit run
            // pins it at an extreme raw value, the same ceiling InhaltIRSynth::render() itself
            // clamps to defensively.
            for (float t = 0.1f; t <= 9.8f; t += 0.5f)
            {
                const auto tank = InhaltParameterMap::mapTimeKnobToTankParams(t);
                expect(std::isfinite(tank.feedbackGain) && tank.feedbackGain > 0.0f && tank.feedbackGain <= 0.95f,
                    "feedbackGain must be finite and within (0, 0.95]");
                expect(std::isfinite(tank.dampingWeight) && tank.dampingWeight > 0.0f && tank.dampingWeight <= 0.99f,
                    "dampingWeight must be finite and within (0, 0.99]");
                expect(std::isfinite(tank.diffuserGain) && tank.diffuserGain >= 0.0f && tank.diffuserGain <= 0.9f,
                    "diffuserGain must be finite and within [0, 0.9]");
            }
        }
    }
};

static InhaltParameterMapTests inhaltParameterMapTests;
