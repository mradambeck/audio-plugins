# AMS RMX16 Non-Lin 2 Reverb Plugin: Implementation Plan

## Goal

Build a JUCE plugin emulating the AMS RMX16 "Non-Lin 2" reverb program, using 19 captured impulse responses as ground truth. The IRs aren't just a bounce target for a final quality check: they're the primary source for figuring out the algorithm's structure and control mapping before any DSP gets written.

## What we know going in

Filenames follow a pattern encoding decay time, an "H" value in dB, and an optional "Tighter" flag:

- `NonLin_0.1s_-3H_1_Tighter.wav`
- `NonLin_0.8s_-3H.wav`
- `NonLin_9.8s_-9H.wav`
- `NonLin_9.8s_0H_Tighter.wav`

We don't know what H controls or what "Tighter" changes on the actual hardware. Don't guess at this before analyzing the audio. Treat the mapping as an open question to be answered empirically in Phase 2, then confirm against the RMX16's actual control set afterward. The IR wav files are under the `/ir-captures` folder.

## Stylistic Details

The plug-in will be called "Intruder" and it's highlight color will be #CFD640.

## Phase 0: Inventory

- Parse all 19 filenames into a table: decay label, H value, tighter flag.
- Note which combinations exist and which don't (the set is very unlikely to be a full 3x3x2 grid, so the interpolation model in Phase 4 needs to handle gaps rather than assume a rectangular parameter space).
- Confirm file sample rate, bit depth, and length are consistent across the set before analysis.

## Phase 1: IR analysis pipeline (Python, offline)

Build this in an `analysis/` folder separate from the plugin source. Output should be a JSON or CSV feature table, one row per IR, plus saved plots for Adam to eyeball.

For each IR, extract:

- **Measured decay time (RT60)** via Schroeder backward integration. Compare against the filename's decay label to confirm that label really is reverb time and not something else.
- **Envelope shape**, via Hilbert transform or smoothed RMS, plotted on a log-amplitude scale. This is the fingerprint that separates Non-Lin 2 from a natural exponential decay: look for hold segments, knees, and cutoff slope rather than a straight line down.
- **Spectral tilt over time**: short-time spectral centroid or a simple high/low energy ratio tracked across the decay, to see whether the tail brightens, darkens, or holds steady, and how fast.
- **Early reflection taps**: peak-pick the first 80-150ms window for discrete delay times and gains.
- **Echo density buildup**: the point where discrete reflections merge into a dense diffuse field. This is a strong candidate for what "Tighter" changes.

## Phase 2: Parameter mapping

With the feature table built, correlate across the 19 files:

- Does measured RT60 track the filename decay label linearly? If so, decay time is a direct control with a near 1:1 mapping, and the plugin's own decay parameter can extrapolate past the three sampled values.
- Does the H value track spectral tilt or the rate of high-frequency loss in the tail? If yes, H is an HF damping control (a "brighter louder to darker quieter" convention would explain why more negative H sounds like less HF energy). If H instead shifts overall level or envelope shape with no consistent effect on tilt, revise the hypothesis rather than forcing the damping story.
- Does "Tighter" mainly shift the echo density onset time and early reflection spacing, with the late-field character otherwise similar? That would point to a definition or diffusion-onset control rather than a decay or tone control.

Write up the actual findings in a short doc before starting Phase 3. If the data doesn't cleanly support one clean explanation for a parameter, say so and describe what it does affect, rather than picking the closest guess.

## Phase 3: DSP architecture

- **Early reflections**: multi-tap delay line with taps and gains taken from the Phase 1 analysis. If the pattern shifts under "Tighter," parametrize spacing rather than hardcoding one tap set.
- **Late diffuse field**: FDN (8x8 with a Householder or Hadamard mixing matrix is a solid starting point) sized and tuned to match the measured echo density and modal density from Phase 1, not picked arbitrarily.
- **HF damping**: one-pole lowpass filters in the FDN feedback paths, with the damping coefficient driven by H, calibrated against the measured spectral tilt curves.
- **Envelope shaping**: a separate multiplicative gain stage applied on top of the FDN's natural decay. Model it as a piecewise envelope (attack, hold, knee, decay segments) fit to the Phase 1 envelope extractions rather than left to decay naturally. This is the part that actually makes it sound like Non-Lin 2 instead of a generic algorithmic reverb.
- **Output stage**: any final coloration or EQ the spectral analysis shows is present across all settings (a fixed signature rather than something the controls change).

## Phase 4: Parameter interpolation

19 discrete points won't cover the full control range continuously, so decide how the plugin extrapolates:

- Decay time: likely safe to treat as continuous and scale the envelope/FDN feedback gain formula directly, since reverb time controls are usually smooth in real hardware.
- H (damping) and Tighter: interpolate between the nearest measured settings for envelope shape and damping coefficient. Where a setting has no reference at all (say, 0.1s with Tighter engaged, if that combination wasn't captured), extrapolate from the nearest neighbors and flag it as unverified rather than presenting it as fitted.

Note that the original unit has the following controls:

- Decay: Adjusts the duration of the reverb tail.
- Pre-Delay (Pre): Sets the initial time delay before the reverb signal starts.
- Mix: Blends the wet and dry audio signals.
- Input/Output Levels: Manages gain staging to properly drive the internal processing.
- High and Low Filters: Modifies the frequency response of the decay on supported programs.

## Phase 5: JUCE plugin

- Standard JUCE plugin scaffold, matching the structure already used for the H910, SPX90 reverse gate, and Karplus-Strong projects.
- Parameters: Decay Time, HF Damp (rename once Phase 2 confirms what H actually is), Tighter/Definition, Mix, Output Level.
- A parameter mapping layer that converts UI control values into DSP coefficients using the Phase 4 interpolation model, keeping that logic separate from the FDN/envelope core so the mapping can be refit without touching the DSP.

## Phase 6: Validation

- Render test transients (impulse, and a real drum hit if one's available) through the plugin at each of the 19 reference settings.
- Run the same Phase 1 analysis pipeline on the plugin's output.
- Compare measured RT60, envelope shape, spectral tilt, and early reflection taps against the original IR's extracted features.
- Iterate on FDN tuning and envelope fit until it holds up by ear, with RT60 and spectral tilt matched within a tolerance Adam sets after seeing the first comparison.

## Phase 7: Styling

- Using the `juce-hardware-panel-ui` skill, and the `/plugins/common/LookAndFeel/MOCKUP+GOUNRD_TRUTH.md`, style the plug-in to look and feel like it belongs within the branding of the other plug-ins hosted in the `audio-plugins` repo.

## Deliverables

1. `analysis/` folder: Python scripts for Phases 0-2, plus the resulting feature table and plots.
2. A short written findings doc on what H and Tighter actually do, before any plugin code is written.
3. JUCE plugin project implementing Phases 3-5.
4. A validation report from Phase 6 comparing plugin output against the reference IRs.
