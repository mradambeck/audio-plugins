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

            // Real onset 5-band measurement (see findings.md's "High: broadband tilt" section and
            // build_measured_gate_curves.py, which computes these same numbers from the real
            // captures rather than transcribing them): H=0 is exactly neutral by construction
            // (the offset curve is anchored there); H=-9's magnitude reflects the real measured
            // +4.5dB low / -9.5dB high onset tilt, NOT the earlier placeholder's rough dB->gain
            // conversion of the same measurement, and NOT the fit's own value (which
            // TILT_REGULARIZATION_WEIGHT suppressed to roughly a third of the real magnitude).
            expect(std::abs(atZero.lowGain - 1.0f) < 1.0e-4f, "H=0 low gain should be neutral (1.0)");
            expect(std::abs(atZero.highGain - 1.0f) < 1.0e-4f, "H=0 high gain should be neutral (1.0)");
            expect(std::abs(atNine.lowGain - 1.6772f) < 1.0e-3f, "H=-9 low gain should match the direct onset measurement");
            expect(std::abs(atNine.highGain - 0.3364f) < 1.0e-3f, "H=-9 high gain should match the direct onset measurement");
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
    }
};

static InhaltParameterMapTests inhaltParameterMapTests;
