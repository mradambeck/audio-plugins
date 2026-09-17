# Inhalt

*NonLin Reverb.*

A recreation (AU / VST3 / Standalone) of the AMS RMX16's **NonLin** gated-reverb program, built
from `ml-toolkit/` (the same pipeline that produced Aura from the RMX16's Ambience program - see
[`../AGENTS.md`](../AGENTS.md)'s ML toolkit section). A prior, unrelated attempt at this same
hardware program exists in this catalog (`plugins/intruder-gated-reverb`) - Inhalt reuses none of
its analysis, data, or tuned constants; every DSP conclusion here is re-derived from scratch, per
explicit instruction.

See the [root README](../README.md) for shared build requirements and running tests across all
plugins at once.

## Status

**Calibrated against the real NonLin capture set** (9 captures, `ml-toolkit/effects/nonlin/
captures/`, gitignored - see `ml-toolkit/effects/nonlin/findings.md` for the analysis). The full
Phase 2-4 pipeline (`analyze.py` -> `fit_nonlin.py` -> `build_curves.py` ->
`build_measured_gate_curves.py` -> `export_params.py`) has run for real, and `InhaltParameterMap`
consumes the resulting `InhaltReferenceData.h` rather than placeholder tables. 368 C++ assertions
and the full `ml-toolkit` test suite pass. `analysis/validate.py` (a real render-vs-real-capture
comparison, not a synthetic check) is committed with current numbers in
`analysis/validation_report.md`.

**Onset density / tonal texture** (added after direct listening against the real IR captures found
the render's initial attack thinner/less gritty than the real hardware): `model.py` and
`InhaltIRSynth.cpp` now share a 3-stage Schroeder allpass input diffuser ahead of both tanks, with
its gain FITTED (not hand-tuned) via two new loss terms in `fit_nonlin.py`
(`core.fit.onset_density_loss`/`spectral_flatness_loss`, a differentiable windowed-kurtosis proxy
and log spectral flatness respectively) - see `core.features.onset_echo_density` for the matching
analysis-side metric and `analysis/validate.py`'s "Flagged concerns" section for automatic
interpretation thresholds on both. Real, measured effect, not just "should help in theory":

| Metric (mean, all 9 captures) | Before diffuser | After |
|---|---|---|
| Onset NED signed error (0-20ms) | -0.153 | +0.076 |
| Log-spectral distance (dB) | ~5.06 | 3.45 |
| Spectral flatness error (dB) | 4.78 | 4.29 |

**A real confound was found and fixed along the way, not just the diffuser itself**: the render's
onset-density error initially looked strongly High-dependent (~0.10 at High=0 vs. ~0.20-0.26 at
High!=0), which looked like it needed a High-dependent diffuser gain. A direct A/B render test
(tilt forced neutral vs. normal, same Time/High settings) proved this was almost entirely an
artifact of `BandShelf`'s own tilt filter biasing the windowed onset-density statistic's effective
degrees of freedom, NOT a real diffusion gap - with tilt forced neutral, three different High
settings at the same Time produced IDENTICAL onset density. `core.features.onset_echo_density` now
applies a first-order pre-emphasis/whitening filter (`pre_emphasis=0.95`) before measuring, which
removes most of this confound (see that function's own docstring). Applying the SAME whitening to
`core.fit.onset_density_loss` was tried next (a reasonable-looking fix) and tested at full scale -
a real ~44min refit, not just reasoning - and made things WORSE: it drove the fitted `diffuser_gain`
from 0.23-0.29 up to 0.57-0.64, overshooting the render's onset statistics to within noise of the
theoretical white-noise ceiling. Reverted; `onset_density_loss`'s own `pre_emphasis` now defaults
to 0.0, with the full before/after numbers (0.076 vs. 0.536 mean error under the identical
corrected metric) recorded in its docstring as the reason. **Net result: the onset-density gap
that motivated this whole investigation is now well within its own interpretation threshold**
(0.076 vs. the 0.15 concern threshold) and the High=0/High!=0 split has shrunk from ~2-3x to a
small, unflagged residual - a genuinely closed gap, not just a relabeled one.

Spectral flatness/"grit" remains a flagged, open gap (3.48dB mean error, worse at negative High)
and is unexamined so far - see the "attack/pluck vs. bow" gap below, under active investigation.

**Tilt pivot and gain magnitude - fixed after a real, ear-caught complaint about playing a synth
line through the plugin at 100% wet**: "The High Frequency cutoff doesn't seem to get as murky and
dark as the convolution [i.e. the real captured IR]... Bringing it down to -9dB to match a -9dB IR
it is still much brighter and clear." Measuring the real captures' WHOLE-DECAY average spectrum
(LTAS), not just the onset window `tilt_low_gain`/`tilt_high_gain` were calibrated from, found the
render's own low/high band spread was roughly HALF the real hardware's at negative High (e.g.
Time=9.8/High=-9: real 16.3dB spread, render only 7.3dB). This was not a gain-calibration
shortfall: the fit's own `tilt_pivot_hz` (~4200-4700Hz, kept because it "looked physically
plausible") puts `BandShelf`'s one-pole transition too close to the 6-16kHz band being darkened -
verified directly that even at extreme gain, that pivot structurally caps the achievable spread at
~8.8dB, short of the ~10-16dB the real captures need at High=-4/-9. Lowering the pivot to 1500Hz
(an onset-band pivot ESTIMATE this project's own earlier work flagged as "less precise" and
discarded in favor of the fit's value) raises the achievable ceiling to ~17-28dB - enough headroom
to actually reach the target instead of asymptoting toward it. Gains are now solved numerically
against the real captures' own LTAS (not a closed-form formula, since a one-pole shelf's
band-averaged dB shift isn't its own asymptotic endpoint value) - see
`build_measured_gate_curves.py`'s own docstring, including a real bug caught along the way (the
first version's per-High-point gain optimization exploited an inherent scale gauge-freedom
inconsistently, breaking monotonicity - caught by `InhaltParameterMapTests`, fixed by collapsing to
a single monotonic "tilt strength" scalar per High point).

| Metric (Time=9.8/High=-9, low/high band spread) | Before | After | Real hardware |
|---|---|---|---|
| LTAS spread (dB) | 7.3 | 15.8 | 16.3 |

Net effect across all 9 captures: log-spectral distance improved 3.45dB -> 2.71dB (no longer
flagged at all) and spectral flatness improved 4.29dB -> 3.48dB, with no regression to onset
density or gate timing (both fixes are independent signal-chain stages).

Three further things are documented as genuine, open gaps rather than silently fixed or hidden:

- Rendered stereo decorrelation (IACC ~0.04-0.08) is closer to the real hardware's (~0.006-0.04)
  after an asymmetric-delay-range change, but not fully matched - see `InhaltIRSynth.cpp`'s own
  comment on what was tried (more lines made it worse; removing the shared gate envelope barely
  moved it) and what actually helped.
- Gate timing at very negative High still shows real per-capture error - an additive High-offset
  fix was tried and reverted after it badly regressed short-Time captures despite improving the
  aggregate mean (see `InhaltParameterMap.cpp`'s own comment). Needs a High sweep at a short Time
  setting the current 9-capture set doesn't have.
- `effects/nonlin/build_measured_gate_curves.py`'s `_repool_fit_only_time_params` exists because a
  fit run can fail `build_curves.py`'s own H-timing-neutrality check (gated on `t_knee_ms`
  specifically) for reasons unrelated to `feedback_gain`/`damping_weight_mean`/`diffuser_gain`/
  `tau_k_ms`, silently losing their Time=0.1s/0.8s coverage and causing `InhaltParameterMap` to
  linearly extrapolate those four parameters below Time=2.2s - concretely caught by
  `InhaltParameterMapTests` (`tau_k_ms` went negative; `feedback_gain` exceeded its own 0.95
  ceiling) when the new onset-density/flatness loss terms shifted the fit's `t_knee_ms`
  convergence enough to flip that check. `InhaltIRSynth::render()`'s own defensive clamps meant
  this was never audible, but it's a real architectural coupling worth knowing about before adding
  another loss term that could shift `t_knee_ms` again.

The UI is still a plain-JUCE placeholder (see "How it works" below) - not yet the real
hardware-panel chassis.

**Plugin code note**: this plugin's own `PLUGIN_CODE` was changed from `Inhl` to `Nlin` after
discovering it collided with a separate, pre-existing product - see `CMakeLists.txt`'s own comment
on `PLUGIN_CODE` for the full story. If a new Wild Jag plugin ever needs an unused code again,
check `~/code/wildjag-convolution-variants/variants/*/CMakeLists.txt` too, not just this public
catalog's `plugins/*/CMakeLists.txt` - Apple identifies an AU by its (type, subtype, manufacturer)
triple regardless of product name or bundle ID, so a collision there is invisible until both
plugins are actually built and registered.

## How it works

Unlike this catalog's other reverbs (a live feedback-delay network running per-sample in
`processBlock`), Inhalt **convolves a synthesized impulse response**. The real hardware's own
response to any input is a fixed, gated shape; convolving a synthesized copy of that shape is
exact under any input signal, where a live envelope-follower-driven gate would only match under a
single impulse. See `ml-toolkit/effects/nonlin/model.py`'s docstring and the project plan for the
full architectural reasoning (including why the tank's feedback-gain ceiling is 0.95, not the
0.985 this catalog's other FDN-based reverbs inherited from Aura - a real, empirically-measured
correction, not stylistic).

- `Source/InhaltIRSynth.h/.cpp` - the DSP core. Two fully independent 8-line FDN tanks (not one
  matrix split across channels - see that file's own comment on why), an input tilt (High's tonal
  effect), and an explicit dB-domain gate envelope (Time's timing effect) applied after the tank.
  JUCE-free, like every other hand-rolled DSP class in this catalog - drives standalone
  `clang++` diagnostics directly.
- `Source/InhaltIRWorker.h/.cpp` - synthesizes off the audio thread whenever Time or High change,
  mirroring `plugins/common/convolution/IRLoadWorker.h`'s threading contract (lock-free request,
  debounced, try-lock pop) without reusing that class (its job is "which bundled IR", not
  "synthesize one from DSP parameters").
- `Source/InhaltParameterMap.h/.cpp` - knob values to synthesis coefficients. See its own header
  comment for exactly what is a real hardware measurement vs. placeholder pending Phase 2.
- `Source/PluginProcessor.h/.cpp` - owns `plugins/common/convolution/ConvolutionEngine` (pre-delay,
  low cut, dry/wet, a click-free ramped bypass - shared with the convolution-base/variant family)
  plus two Inhalt-specific post-stages, Width and Converter, both applied to the **wet contribution
  only** (isolated via linear un-mixing after `engine.process()` - see `processBlock()`'s own
  comment for why this matters: an early version applied them to the combined dry+wet signal and
  audibly smeared what should have been a pristine Dry=100%/Wet=0% passthrough, caught by
  `InhaltProcessorTests`).
- `Source/PluginEditor.h/.cpp` - a **plain-JUCE placeholder**, not the final hardware-panel UI,
  matching Aura's own Phase C precedent. An HTML mockup needs to exist and be approved (via the
  `juce-hardware-panel-ui` skill) before any chassis/knob styling work happens.

## Controls

| Control | Range | Grounding |
|---|---|---|
| Time | 0.1-9.8 (hardware's own label scale) | Real hardware measurement (gate length); internal shape breakdown is placeholder |
| High Frequency | -9-0 dB | Real hardware measurement (input tilt, both range endpoints) |
| Pre-Delay | 0-200 ms | Catalog convention |
| Low Cut | 0-300 Hz (0 = off) | Catalog convention |
| Converter | Vintage / Modern | AMS RMX16 spec sheet (bandwidth + noise floor only, no saturation - harmonics are out of scope, see the project plan) |
| Width | 0-150% (100% = measured hardware width) | Real hardware measurement (stereo decorrelation) |
| Dry / Wet | 0-100% each | Catalog convention |
| Bypass | - | Click-free, via `ConvolutionEngine`'s own ramped bypass |

## Building

```sh
cd inhalt-nonlin
cmake -B build -G Xcode
cmake --build build --config Release --target Inhalt_All
```

To build a single format only: `--target Inhalt_AU`, `Inhalt_VST3`, or `Inhalt_Standalone`.

## Installation

`COPY_PLUGIN_AFTER_BUILD` is enabled, so building `Inhalt_All` (or the individual `Inhalt_AU`/
`Inhalt_VST3` targets) automatically copies the plugin into the standard user plugin directories:

- **AU:** `~/Library/Audio/Plug-Ins/Components/Inhalt - Reverb.component`
- **VST3:** `~/Library/Audio/Plug-Ins/VST3/Inhalt - Reverb.vst3`

**If a host (Logic, etc.) was already running when you built/installed the plugin, it won't see a
brand-new AU/VST3 until it rescans** - hosts scan for plugins at launch, not while already open.
Fully quit the host (⌘Q, not just closing the project) and relaunch. If it still doesn't show up,
open the host's plugin manager (Logic: **Logic Pro → Settings → Plug-in Manager**), find "Inhalt"
under manufacturer "Wild Jag", and use its rescan/reset action if it's listed but greyed out.

Not yet registered in the catalog's shared installer/CI - see the project plan's Phase 7 (deferred
until the UI and remaining calibration gaps are further along).

## Launching the Standalone app

```sh
cmake --build build --config Release --target Inhalt_Standalone
open "build/Inhalt_artefacts/Release/Standalone/Inhalt - Reverb.app"
```

(The product name has spaces in it - `Inhalt - Reverb.app` - so if typing the path by hand rather
than globbing it, it needs to stay quoted as one argument, as above.)

## Validating the AU (auval)

```sh
auval -v aufx Nlin WJag
```

## Testing

```sh
cmake --build build --config Release --target InhaltTests
./build/InhaltTests_artefacts/Release/InhaltTests
```

## Offline rendering

```sh
cmake --build build --config Release --target InhaltRenderIR
./build/InhaltRenderIR_artefacts/Release/InhaltRenderIR --out /tmp/inhalt.wav \
    --timeKnob 4.8 --high -4 --dry 0 --wet 100
```

Flags map 1:1 onto `PluginProcessor.h`'s APVTS parameter IDs, in the plugin's own native units -
see `Source/Tools/RenderIR.cpp`'s own usage comment for the full flag list. Analyze a render with
`ml-toolkit`'s own `core.features` functions (`gate_envelope_params`, `interchannel_correlation`,
`normalized_echo_density`, ...) - that is the actual verification loop this plugin is built around,
not a separate analysis script (effects/nonlin/analysis/validate.py, once real captures exist).

## Installation

Not yet registered in the catalog's shared installer/CI - see the project plan's Phase 7. Until
then, `COPY_PLUGIN_AFTER_BUILD` still installs a built plugin into the standard user plugin
directories, matching every other plugin in this catalog.

## Next steps

1. Adam supplies the NonLin capture set into `ml-toolkit/effects/nonlin/captures/` (see that
   directory's own naming convention, `NonLin_<time>s_<high>H.wav`).
2. Run the Phase 2-4 pipeline for real (`analyze.py` -> hand-written `findings.md` -> `fit_nonlin.py`
   -> `build_curves.py` -> `export_params.py`), replacing `InhaltParameterMap`'s placeholder tables
   with the real generated `InhaltReferenceData.h`.
3. Re-run `InhaltRenderIR` + the same `core.features` verification loop against the real captures
   (`analysis/validate.py`) to confirm the calibration gap documented in `InhaltParameterMap.h` is
   closed.
4. HTML mockup + the `juce-hardware-panel-ui` skill for the real UI.
5. Catalog registration (CI, scripts, installers) and a version bump before the first real release.
