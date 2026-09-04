#include "../GradientPitchShiftEngine.h"

#include <juce_core/juce_core.h>

#include <algorithm>
#include <cmath>
#include <vector>

// Diagnostic follow-up to a by-ear report during Milestone 3: at high Feedback (~115%) with a
// real Delay time, the user heard the repeats decay away quickly rather than self-sustaining or
// growing into runaway regeneration. This measures the per-repeat RMS energy directly (rather
// than trusting a by-ear impression) to determine whether the feedback loop's gain is actually
// behaving as designed (>=1 at the knob's nominal setting) or whether something in the loop
// (interpolation loss, the darkening filter, or - per Milestone 2's documented splice-phase
// characteristic - crossfade blending under pitch shift) is quietly pulling the real per-pass
// gain below 1.
class GradientFeedbackTests : public juce::UnitTest
{
public:
    GradientFeedbackTests() : juce::UnitTest("GradientPitchShiftEngine feedback sustain", "Gradient") {}

    // Feeds a short burst then silence, and measures RMS energy in successive windows the length
    // of the delay time (i.e. one window per expected repeat).
    static std::vector<double> measureRepeatEnergies(double sampleRate, float delayMs, float feedbackPercent,
                                                       float semitones, int numRepeatsToMeasure,
                                                       GradientPitchShiftEngine::SpliceMode mode
                                                           = GradientPitchShiftEngine::SpliceMode::glitch,
                                                       float crossfadeLengthMs = 5.0f)
    {
        GradientPitchShiftEngine engine;
        engine.prepare(sampleRate);
        engine.setPitchSemitones(semitones, 0.0f);
        engine.setDelayTimeMs(delayMs);
        engine.setFeedback(feedbackPercent);
        engine.setMix(100.0f); // fully wet - observe the internal loop directly
        engine.setOutputTrimDb(0.0f);
        engine.setSpliceMode(mode);
        engine.setCrossfadeLengthMs(crossfadeLengthMs);

        const auto delaySamples = (int) std::round(delayMs * 0.001 * sampleRate);

        const int burstSamples = (int) std::round(0.05 * sampleRate); // 50ms burst
        const int totalSamples = burstSamples + delaySamples * numRepeatsToMeasure;

        std::vector<double> energies((size_t) numRepeatsToMeasure, 0.0);

        double phase = 0.0;
        const auto phaseIncrement = juce::MathConstants<double>::twoPi * 220.0 / sampleRate;

        for (int i = 0; i < totalSamples; ++i)
        {
            float inputSample = 0.0f;
            if (i < burstSamples)
            {
                inputSample = 0.3f * (float) std::sin(phase);
                phase += phaseIncrement;
            }

            const auto out = engine.process(inputSample);

            const auto repeatIndex = (i - burstSamples) / delaySamples;
            if (repeatIndex >= 0 && repeatIndex < numRepeatsToMeasure)
                energies[(size_t) repeatIndex] += (double) out * (double) out;
        }

        for (auto& e : energies)
            e = std::sqrt(e / (double) delaySamples);

        return energies;
    }

    void runTest() override
    {
        runLongSessionModeSwitchTest();
        runModeSwitchThenFeedbackChangeTest();

        constexpr double sampleRate = 48000.0;
        constexpr float delayMs = 250.0f;
        constexpr int numRepeats = 8;

        beginTest("Control: feedback well below unity decays away (confirms measurement sensitivity)");
        {
            const auto energies = measureRepeatEnergies(sampleRate, delayMs, 50.0f, 0.0f, numRepeats);
            logMessage("50% feedback, 0st: " + energiesToString(energies));
            expect(energies[(size_t) numRepeats - 1] < energies[0] * 0.3,
                   "Repeats should have clearly decayed by 115% feedback's absence");
        }

        beginTest("Feedback just past unity (115%), no pitch shift: repeats should sustain, not decay away");
        {
            const auto energies = measureRepeatEnergies(sampleRate, delayMs, 115.0f, 0.0f, numRepeats);
            logMessage("115% feedback, 0st: " + energiesToString(energies));
            expect(energies[(size_t) numRepeats - 1] > energies[0] * 0.7,
                   "115% feedback with no pitch shift should sustain (not decay) across repeats");
        }

        beginTest("Feedback just past unity (115%) WITH pitch shift: isolates whether splice blending"
                   " (Milestone 2's documented phase characteristic) costs energy under recirculation");
        {
            const auto energies = measureRepeatEnergies(sampleRate, delayMs, 115.0f, 12.0f, numRepeats);
            logMessage("115% feedback, 12st: " + energiesToString(energies));
            // NOT expected to self-oscillate here (ratio < 1, genuine decay) - confirmed by a by-ear
            // report and a feedback%/semitones sweep during Milestone 4: splice-phase loss under ANY
            // pitch shift, even mild (3st), needs roughly 150-200% feedback to overcome, well above
            // this "just past unity" figure the original plan assumed would be enough. See the
            // self-oscillation threshold test below and the plan's Milestone 4 write-up.
        }

        // Follow-up: the two tests above used a Delay time (250ms @ 48kHz = exactly 12000.0
        // samples) that happens to have ZERO fractional part, so readInterpolated() never actually
        // interpolates between two samples - sidestepping a real loss mechanism a knob almost never
        // lands exactly on. Linear interpolation at a fractional delay is itself a lowpass filter
        // (unity gain only exactly at DC), so it costs a little real energy on every single pass
        // even at 0 semitones. Re-run the 0-semitone/115% case with a deliberately fractional delay
        // and many more repeats, to see the true long-run trend rather than just 8 repeats' worth.
        constexpr float fractionalDelayMs = 233.7f; // 233.7ms @ 48kHz = 11217.6 samples - 0.6 frac
        constexpr int manyRepeats = 40;

        beginTest("115% feedback, 0st, fractional-sample delay, long run: settles to a loud, audible"
                   " equilibrium rather than the quiet plateau the un-tuned safety drive produced");
        {
            const auto energies = measureRepeatEnergies(sampleRate, fractionalDelayMs, 115.0f, 0.0f, manyRepeats);
            logMessage("115% feedback, 0st, frac delay, 40 repeats: " + energiesToString(energies));
            expect(energies[(size_t) manyRepeats - 1] > 0.3,
                   "115% feedback should settle to a clearly audible equilibrium (was ~0.16 with the "
                   "un-tuned drive=1.6 before this milestone's retuning), not a barely-there plateau");
        }

        beginTest("50% feedback, 0st, fractional-sample delay, long run (control)");
        {
            const auto energies = measureRepeatEnergies(sampleRate, fractionalDelayMs, 50.0f, 0.0f, manyRepeats);
            logMessage("50% feedback, 0st, frac delay, 40 repeats: " + energiesToString(energies));
        }

        // Milestone 4 finding: De-glitch smart's per-tap zero-crossing search measurably tightens
        // pitch accuracy (see GradientPitchShiftEngineTests), but under feedback recirculation it
        // trails Glitch on raw sustain, and - found by ear, not by this test - the underlying drift
        // was ALSO producing extra irregular audible dropouts (worse than Glitch's regular seam).
        // Root cause: each tap searches for its own splice moment independently, so each wrap gets
        // its own small, data-dependent extra delay; over many wraps these accumulate into a random
        // walk that drifts the two taps away from their exact half-window phase relationship - the
        // invariant the whole crossfade design (see the class comment in GradientPitchShiftEngine.h)
        // relies on. Fixed with a searchDebt-based throttle (see advanceTap()): a tap that's already
        // spent more time searching than its partner gets a shorter budget next time, bounding the
        // drift instead of letting it random-walk unboundedly. This measurably improved sustain here
        // too (ratio ~0.11-0.14 before the throttle, ~0.15 after) without touching the pitch-accuracy
        // improvement at all - but smart still trails glitch on this specific metric, since the
        // throttle bounds drift rather than eliminating the underlying cost of a wider/searched
        // splice; that residual gap is the genuine, accepted cost of the plan's explicitly-scoped
        // "cheap, real-time-feasible" simplification versus true coupled autocorrelation (H949 ALG-3).
        beginTest("De-glitch smart vs Glitch feedback sustain under pitch shift: documents the "
                   "search-drift trade-off (smart trails glitch here, unlike pitch accuracy)");
        {
            const auto glitchEnergies = measureRepeatEnergies(sampleRate, delayMs, 115.0f, 12.0f, numRepeats,
                                                                GradientPitchShiftEngine::SpliceMode::glitch, 5.0f);
            const auto smartEnergies = measureRepeatEnergies(sampleRate, delayMs, 115.0f, 12.0f, numRepeats,
                                                               GradientPitchShiftEngine::SpliceMode::deglitchSmart, 20.0f);

            logMessage("115% feedback, 12st, glitch: " + energiesToString(glitchEnergies));
            logMessage("115% feedback, 12st, smart:  " + energiesToString(smartEnergies));

            const auto glitchRatio = glitchEnergies[(size_t) numRepeats - 1] / glitchEnergies[0];
            const auto smartRatio = smartEnergies[(size_t) numRepeats - 1] / smartEnergies[0];

            logMessage("glitch ratio=" + juce::String(glitchRatio, 3) + ", smart ratio=" + juce::String(smartRatio, 3));

            // Regression guard, not a "smart is better" assertion (it currently isn't, for this
            // metric - see the comment above): smart's sustain shouldn't collapse well below what
            // it measures today, which would indicate the drift got worse, not better.
            expect(smartRatio > glitchRatio * 0.5,
                   "De-glitch smart's feedback sustain regressed further than the documented "
                   "search-drift trade-off accounts for (glitch ratio=" + juce::String(glitchRatio, 3)
                   + ", smart ratio=" + juce::String(smartRatio, 3) + ")");
        }

        // Milestone 4 finding, prompted by a by-ear report ("feedback no longer self-oscillates at
        // 115% with pitch shift active"): earlier feedback tests only checked RELATIVE sustain
        // (e.g. "better than the 50% control", "smart vs glitch") and never verified any pitch-
        // shifted case actually crosses the self-oscillation threshold (ratio > 1, genuine growth)
        // at the parameter range's old 115% ceiling. A feedback%/semitones sweep found that even a
        // mild 3-semitone shift needs ~150% to cross that threshold, and 12-24 semitones need ~200%,
        // across all three splice modes - the "just past unity" (115%) ceiling the original plan
        // assumed would be enough only actually holds for 0-semitone (bypass) feedback, which has no
        // splice loss at all. Raised feedbackPercentA's range to 0-250% (PluginProcessor) so the
        // "self-oscillation capable" feedback described in the spec is actually reachable with
        // pitch shift active - 250% gives comfortable margin above the ~200% empirical threshold.
        // Ceiling raised to 350% (from 250%): adding the DC blocker below (fixes a separate bug -
        // see the "stuck feedback" test) removed some near-DC/aliased energy that was, at the most
        // extreme case (24 semitones - repeated 2-octave shifts alias past Nyquist quickly), quietly
        // propping up self-oscillation right at 250%. 300% comfortably crosses threshold there
        // post-DC-blocker; 350% keeps the same margin philosophy as before.
        beginTest("Feedback actually reaches self-oscillation (ratio > 1) at the new range's ceiling, "
                   "with pitch shift active, across splice modes - the real fix for the by-ear report");
        {
            for (auto mode : { GradientPitchShiftEngine::SpliceMode::glitch, GradientPitchShiftEngine::SpliceMode::deglitchSmart })
            {
                for (float semitones : { 3.0f, 12.0f, 24.0f })
                {
                    const auto e = measureRepeatEnergies(sampleRate, 187.3f, 350.0f, semitones, numRepeats, mode, 12.0f);
                    const auto ratio = e[(size_t) numRepeats - 1] / e[0];
                    expect(ratio > 1.0,
                           "350% feedback should genuinely self-oscillate (not just decay slower) at "
                           + juce::String(semitones, 0) + "st, mode=" + juce::String((int) mode)
                           + " (measured ratio=" + juce::String(ratio, 3) + ")");
                }
            }
        }
    }

    // Reproduces a by-ear report: self-oscillation ran fine, but after letting it ring for a while
    // and then switching Splice Mode live (Glitch -> De-glitch Smart), audio stopped entirely and
    // didn't recover until Feedback was lowered. This engine-level run (20s+ self-oscillation, no
    // NaN/Inf, no internal silence, live mode switch mid-stream) never reproduced actual silence or
    // NaN - but it DID reveal the real cause: sustained RMS around 1.4, i.e. output genuinely
    // exceeding +-1.0 (full digital scale) for a long period. That's almost certainly what triggers
    // host/OS/audio-interface protective limiting or muting externally - "stops working" rather than
    // an audible clip. Fixed with a final output safety ceiling (see outputSafetyLimit()); this test
    // now also asserts peak output never exceeds 1.0.
    void runLongSessionModeSwitchTest()
    {
        beginTest("Long self-oscillation session, then live Splice Mode switch: no NaN, no silence, "
                   "and peak output stays within +-1.0 (the real fix for 'audio stops working')");
        {
            constexpr double sampleRate = 48000.0;
            GradientPitchShiftEngine engine;
            engine.prepare(sampleRate);
            engine.setPitchSemitones(3.0f, 0.0f);
            engine.setDelayTimeMs(187.3f);
            engine.setFeedback(200.0f);
            engine.setMix(100.0f);
            engine.setOutputTrimDb(0.0f);
            engine.setSpliceMode(GradientPitchShiftEngine::SpliceMode::glitch);
            engine.setCrossfadeLengthMs(8.0f);

            double phase = 0.0;
            const auto phaseIncrement = juce::MathConstants<double>::twoPi * 220.0 / sampleRate;
            const int burstSamples = (int) (0.05 * sampleRate);
            const int preSwitchSamples = (int) (20.0 * sampleRate); // 20 seconds before switching
            const int postSwitchSamples = (int) (3.0 * sampleRate); // 3 seconds after

            bool sawNaNOrInf = false;
            float peakAbsOutput = 0.0f;
            double preSwitchLastRms = 0.0;
            {
                const int rmsWindow = (int) (0.1 * sampleRate);
                for (int i = 0; i < preSwitchSamples; ++i)
                {
                    float inputSample = 0.0f;
                    if (i < burstSamples)
                    {
                        inputSample = 0.3f * (float) std::sin(phase);
                        phase += phaseIncrement;
                    }
                    const auto out = engine.process(inputSample);
                    if (!std::isfinite(out))
                        sawNaNOrInf = true;
                    peakAbsOutput = std::max(peakAbsOutput, std::abs(out));
                    if (i >= preSwitchSamples - rmsWindow)
                        preSwitchLastRms += (double) out * (double) out;
                }
                preSwitchLastRms = std::sqrt(preSwitchLastRms / (double) rmsWindow);
            }

            logMessage("RMS just before switch (after 20s of self-oscillation): " + juce::String(preSwitchLastRms, 5));
            expect(!sawNaNOrInf, "No NaN/Inf should appear during the pre-switch 20s self-oscillation");
            expect(preSwitchLastRms > 0.05, "Should still be audibly self-oscillating just before the switch");

            // Live mode switch, mid-stream - exactly like a user turning the Splice Mode knob while
            // the plugin keeps processing.
            engine.setSpliceMode(GradientPitchShiftEngine::SpliceMode::deglitchSmart);

            double postSwitchRms = 0.0;
            const int tailWindow = (int) (0.5 * sampleRate);
            for (int i = 0; i < postSwitchSamples; ++i)
            {
                const auto out = engine.process(0.0f);
                if (!std::isfinite(out))
                    sawNaNOrInf = true;
                peakAbsOutput = std::max(peakAbsOutput, std::abs(out));
                if (i >= postSwitchSamples - tailWindow)
                    postSwitchRms += (double) out * (double) out;
            }
            postSwitchRms = std::sqrt(postSwitchRms / (double) tailWindow);

            logMessage("Peak |output| across the whole 23s run: " + juce::String(peakAbsOutput, 5));
            expect(peakAbsOutput <= 1.0f,
                   "Output must never exceed +-1.0, even under sustained self-oscillation - a value "
                   "of ~1.43 (before the output safety ceiling) is what likely triggered external "
                   "host/OS/interface protective muting (measured peak=" + juce::String(peakAbsOutput, 5) + ")");

            logMessage("RMS 3s after switching to De-glitch Smart: " + juce::String(postSwitchRms, 5));
            expect(!sawNaNOrInf, "No NaN/Inf should appear after switching Splice Mode mid-session");
            expect(postSwitchRms > 0.01,
                   "Output should not go silent after a live Splice Mode switch during self-oscillation "
                   "(measured RMS=" + juce::String(postSwitchRms, 5) + ")");
        }
    }

    // Reproduces a by-ear report: after switching Splice Mode live, lowering Feedback again "doesn't
    // scale properly" unless Feedback is dropped to 0 first, then brought back up. Compares a
    // "history" engine (self-oscillates at one Feedback/mode, switches mode, THEN changes Feedback
    // without zeroing first) against a "fresh" engine started directly at the final settings.
    //
    // Root cause, found by inspecting the "stuck" output directly: it was pure DC (zero crossings
    // over a 50ms window, values slowly drifting rather than oscillating) - not audible as a tone,
    // matching the by-ear report of "a low thump" / "inaudible lower frequencies". Pitch-shifting a
    // DC signal costs NO energy at the splice (blending two identical values loses nothing), so DC
    // entirely bypasses the splice-loss mechanism that limits ordinary tonal self-oscillation - it
    // can self-sustain even at Feedback settings (confirmed: 100%) well below what any audible tone
    // needs (~150-200%+), and doesn't decay when Feedback is lowered because nothing in the signal
    // path ever removes it. This is a well-known issue in delay/feedback effects generally, normally
    // solved (as here) with a fixed DC-blocking highpass in the feedback path - see dcBlock().
    // Mode-switching itself was a red herring: the same "won't decay" behaviour reproduced with no
    // splice mode change involved at all, confirming it as a general feedback-path issue, not
    // something specific to Splice Mode.
    static double runToSteadyStateRms(GradientPitchShiftEngine& engine, double sampleRate, double seconds)
    {
        const int n = (int) (seconds * sampleRate);
        const int tailWindow = (int) (0.5 * sampleRate);
        double sumSq = 0.0;
        for (int i = 0; i < n; ++i)
        {
            const auto out = engine.process(0.0f);
            if (i >= n - tailWindow)
                sumSq += (double) out * (double) out;
        }
        return std::sqrt(sumSq / (double) tailWindow);
    }

    static double runHistoryScenario(double sampleRate, GradientPitchShiftEngine::SpliceMode startMode,
                                      GradientPitchShiftEngine::SpliceMode endMode,
                                      float startFeedback, float endFeedback)
    {
        GradientPitchShiftEngine engine;
        engine.prepare(sampleRate);
        engine.setPitchSemitones(3.0f, 0.0f);
        engine.setDelayTimeMs(187.3f);
        engine.setCrossfadeLengthMs(8.0f);
        engine.setMix(100.0f);
        engine.setOutputTrimDb(0.0f);

        engine.setFeedback(startFeedback);
        engine.setSpliceMode(startMode);
        double phase = 0.0;
        const auto phaseIncrement = juce::MathConstants<double>::twoPi * 220.0 / sampleRate;
        const int burstSamples = (int) (0.05 * sampleRate);
        for (int i = 0; i < (int) (5.0 * sampleRate); ++i)
        {
            float inputSample = 0.0f;
            if (i < burstSamples) { inputSample = 0.3f * (float) std::sin(phase); phase += phaseIncrement; }
            engine.process(inputSample);
        }

        engine.setSpliceMode(endMode);
        for (int i = 0; i < (int) (5.0 * sampleRate); ++i)
            engine.process(0.0f);

        engine.setFeedback(endFeedback);
        return runToSteadyStateRms(engine, sampleRate, 25.0);
    }

    static double runFreshScenario(double sampleRate, GradientPitchShiftEngine::SpliceMode endMode, float endFeedback)
    {
        GradientPitchShiftEngine engine;
        engine.prepare(sampleRate);
        engine.setPitchSemitones(3.0f, 0.0f);
        engine.setDelayTimeMs(187.3f);
        engine.setCrossfadeLengthMs(8.0f);
        engine.setMix(100.0f);
        engine.setOutputTrimDb(0.0f);
        engine.setSpliceMode(endMode);
        engine.setFeedback(endFeedback);

        double phase = 0.0;
        const auto phaseIncrement = juce::MathConstants<double>::twoPi * 220.0 / sampleRate;
        const int burstSamples = (int) (0.05 * sampleRate);
        for (int i = 0; i < (int) (5.0 * sampleRate); ++i)
        {
            float inputSample = 0.0f;
            if (i < burstSamples) { inputSample = 0.3f * (float) std::sin(phase); phase += phaseIncrement; }
            engine.process(inputSample);
        }
        return runToSteadyStateRms(engine, sampleRate, 25.0);
    }

    void runModeSwitchThenFeedbackChangeTest()
    {
        beginTest("Lowering Feedback after a self-oscillating session decays properly (DC blocker "
                   "fix) - history and fresh engines converge to the same steady state");
        {
            constexpr double sampleRate = 48000.0;
            using SM = GradientPitchShiftEngine::SpliceMode;

            struct Scenario { SM startMode, endMode; float startFb, endFb; bool endFbSelfOscillates; const char* label; };
            const Scenario scenarios[] = {
                { SM::glitch, SM::deglitchSmart, 150.0f, 200.0f, true, "glitch->smart, fb 150->200 (raise)" },
                { SM::deglitchSmart, SM::glitch, 150.0f, 200.0f, true, "smart->glitch, fb 150->200 (raise)" },
                { SM::glitch, SM::deglitchSmart, 200.0f, 100.0f, false, "glitch->smart, fb 200->100 (lower)" },
                { SM::deglitchSmart, SM::glitch, 200.0f, 100.0f, false, "smart->glitch, fb 200->100 (lower)" },
                { SM::glitch, SM::deglitchSoft, 200.0f, 100.0f, false, "glitch->soft, fb 200->100 (lower)" },
            };

            for (const auto& s : scenarios)
            {
                const auto historyRms = runHistoryScenario(sampleRate, s.startMode, s.endMode, s.startFb, s.endFb);
                const auto freshRms = runFreshScenario(sampleRate, s.endMode, s.endFb);
                logMessage(juce::String(s.label) + ": history=" + juce::String(historyRms, 5)
                           + " fresh=" + juce::String(freshRms, 5));

                // The real bug (pre-DC-blocker): a history engine stuck near its OLD, hotter level
                // even after Feedback was lowered well below the self-oscillation threshold, while a
                // fresh engine at the same final settings correctly decayed near-silent. Check both
                // land in the same REGIME (self-oscillating vs. decayed-to-silence) as the fresh
                // engine, rather than requiring exact numeric agreement (amplitude-dependent
                // convergence time in a nonlinear saturating loop is expected and fine).
                if (s.endFbSelfOscillates)
                    expect(historyRms > 0.05, juce::String(s.label) + ": history engine should still be "
                           "clearly self-oscillating (measured " + juce::String(historyRms, 5) + ")");
                else
                    expect(historyRms < 0.01, juce::String(s.label) + ": history engine should have decayed "
                           "near-silent, not stayed stuck at its earlier hotter level (measured "
                           + juce::String(historyRms, 5) + ", fresh reference=" + juce::String(freshRms, 5) + ")");
            }
        }
    }

    static juce::String energiesToString(const std::vector<double>& energies)
    {
        juce::String s;
        for (auto e : energies)
            s << juce::String(e, 5) << " ";
        return s;
    }
};

static GradientFeedbackTests gradientFeedbackTests;
