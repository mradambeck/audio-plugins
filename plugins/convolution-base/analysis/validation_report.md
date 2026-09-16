# ConvBase engine validation

Measured from audio rendered through the real `ConvolutionProcessor` by `ConvBaseRenderIR`,
not from the DSP re-implemented in Python. Regenerate with `python3 analysis/validate.py`.

**9 of 9 checks passed.**

| Check | Result | Measurement |
|---|---|---|
| Reported and actual latency are zero | PASS | impulse peak at sample 0, amplitude 1.0000 |
| Wet path is transparent at defaults | PASS | worst deviation 5.35e-05 against a peak of 0.50 |
| Pre-delay lands within a sample of the requested time | PASS | requested 25.0 ms, measured 25.000 ms |
| Bypass introduces no step beyond the signal's own | PASS | toggled 0.05546 vs control 0.05548 |
| Bypass actually removes the wet signal | PASS | RMS after: 0.3536 bypassed vs 1.3609 active |
| IR swap introduces no step beyond the signal's own | PASS | swapped 0.04972 vs control 0.04972 |
| Decay time is independent of session sample rate | PASS | T20 44100 Hz: 691.7 ms, 48000 Hz: 692.3 ms, 96000 Hz: 692.2 ms (spread 0.52 ms) |
| Low cut attenuates below its corner | PASS | 80 Hz tone at 1.0% of its unfiltered level with an 800 Hz low cut |
| High cut attenuates above its corner | PASS | 9 kHz tone at 1.0% of its unfiltered level with a 1 kHz high cut |

## Why these and not others

Each row is a claim the plugin makes that a compile cannot check:

- **Latency** is the one that justifies the rest. `juce::dsp::Convolution` is built with
  `NonUniform{256}` for CPU rather than for latency, so the zero has to be measured. It is
  also what makes the ramped bypass safe - with no plugin delay compensation in play, there
  is no timing discontinuity to reconcile when a host toggles bypass.
- **Transparency at defaults** covers the promise that Length, Attack and both filters start
  out doing nothing. Convolving with a Dirac IR is an identity, so any deviation is the wet
  chain's own doing.
- **The two step measurements** are what 'no clicks' means once it is written down. Both are
  compared against an identical run that never toggles, so the threshold is the signal's own
  steady-state roughness rather than a number picked to pass.
- **Decay time across sample rates** is the check that the resampler used the right ratio.
  `IRLibrary::resample()` also compensates `LagrangeInterpolator`'s 2-sample group delay;
  without that, the same IR would start at different moments in 44.1 and 48 kHz sessions.
