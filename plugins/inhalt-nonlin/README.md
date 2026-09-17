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

**Direct/early-arrival tap - added after a real, ear-caught complaint about playing a synth line
through the plugin at 100% wet**: "the convolution version sounds almost like a bow across
strings, while the version you built still retains the pluck/attack/envelope of the synth."
Measured the real captures' own very first few ms directly (not just their onset-density
statistic) and found a genuine, consistent property across 5 captures spanning different Time/
High settings: the real hardware's response starts at 13-16% of its eventual peak on the FIRST
SAMPLE, with the first 10ms already averaging ~35-49% of the established 40-50ms RMS level - a
genuinely immediate response, not built up from silence. This engine's tank alone cannot produce
that: its shortest delay line (~10ms) means the tank's own output is EXACTLY ZERO until that first
round trip completes - a true silence gap the real hardware doesn't have. Convolving that silence
with a percussive input passes the input's own attack through completely unprocessed for the
first ~10ms, before the reverb "catches up" - a real, structural explanation for the complaint.

Fixed by tapping each channel's own input diffuser output directly (in addition to feeding the
tank) and summing it into that channel's output before tilt/gate - see `InhaltIRSynth.h`'s own
`Params::directGain` comment. Each channel now has its OWN diffuser instance with a different
delay set (previously a single shared diffuser fed both tanks), so this early tap doesn't
correlate the two channels the way a shared mono tap would have. `directGain` (0.79) is a
hand-measured constant (not fit-derived), calibrated by rendering candidates and matching the
real captures' own RMS[0-10ms]/RMS[40-50ms] ratio - reaches ~0.46 at Time=2.2/High=0 (within the
real 0.44-0.49 cluster) but only ~0.27 at Time=9.8/High=-9 (short of that single point's own
0.35), a known residual gap given only one real High!=0 data point to calibrate against.

**A real, understood side effect, not silently absorbed**: `validate.py`'s automated knee-time
error got measurably WORSE at High!=0 (-36.8ms -> -49.5ms), concentrated in specific captures (the
two shortest-Time ones worsened most). Investigated directly rather than assumed: comparing the
render's own early-envelope shape (0.5ms-resolution RMS) against the real capture's own shows BOTH
are genuinely spiky/discrete in the first 20ms (individual early-reflection-like peaks and dips,
not a smooth ramp) - the render's onset shape is now qualitatively MORE like the real hardware's,
not a new artifact. `core.features.gate_envelope_params()`'s swept two-segment breakpoint fit was
implicitly validated against a smoother, more monotonic build-up shape; it is measurably less
stable against this newly-spikier (and more realistic) onset, which best explains the knee-time
regression as an analysis-algorithm sensitivity rather than an audible DSP regression. Not fixed
in this pass - `gate_envelope_params()`'s own robustness to a textured onset is a separate,
un-started investigation, flagged here rather than left undocumented.

**A separate caveat found while investigating the above, unrelated to whether it explains the
regression**: `Source/Tools/RenderIR.cpp` calls `processor.processBlock()` directly and writes
its raw output, without reading or compensating for `processor.getLatencySamples()` (the
convolution engine's own real, reported FFT-partition latency - a real DAW host applies this
compensation automatically, shifting the whole track earlier by that amount before the user ever
hears it). Every `InhaltRenderIR` render is therefore delayed by a small, fixed amount (~3ms
observed) relative to what a real host would actually play. This does NOT invalidate the
onset-relative measurements in this README (`core.features.find_onset` re-detects the true start
of content in every render independently, so a constant shift washes out of anything measured
relative to it), but it means `InhaltRenderIR`'s raw WAV output is not sample-accurate against
true t=0 - worth fixing in `RenderIR.cpp` before any future measurement that needs an ABSOLUTE
(not onset-relative) time reference.

**Tank delay-line set - revised a third time, after a real, ear-caught complaint comparing this
render directly against the real IR loaded into the catalog's convolution reverb**: "the stereo
spread feels pretty different, even when adjusting the width of the new plugin... Conv has more
going on around ~400Hz, while the algo has more going on around ~3-4k Hz." Measured directly
(level-matched LTAS, 4 Time/High settings): NOT excess energy at 3-4kHz (the render is actually
slightly *below* real there too) - a genuine, consistent deficit at 128-323Hz instead (worst at
161Hz: -5 to -9dB relative to neighboring bands), which reads as "more going on up top" only
because the low-mid is comparatively thin. Confirmed via direct A/B (tilt and direct tap both
disabled) that this is a tank-topology/modal property, not a downstream stage.

A randomized search (40 candidates, perturbing the existing delay-line set +-15%) over notch
depth and IACC found a set that closed the notch AND improved IACC - in a **bare-tank test**.
Built, re-fit (~42min), and validated for real: the notch was genuinely closed, but IACC at
Time=9.8/High=-9 got WORSE (0.0712 vs. the original's own ~0.0352) - the exact "stereo spread
feels different" complaint, not fixed but made WORSE by the first attempt. Investigated rather
than shipped: isolated the direct tap (not the cause - IACC was slightly worse with it OFF) and
then added the input diffuser stage to the Python search harness (the actual missing piece,
confirmed by reproducing the same regression in Python once the diffuser was included) - the
first candidate's own delay values interact badly with the diffuser-fed-into-tank stage under the
tilt's strong low-frequency boost at negative High, an effect invisible in a bare-tank test.

Re-ran the search scoring the FULL chain (diffuser + the real LTAS-calibrated tilt at High=-9,
not just the bare tank) and found a second candidate, verified across 5 real Time/High settings
before committing to another re-fit:

| Metric (Time=9.8/High=-9) | Original set | 1st candidate (discarded) | Final set |
|---|---|---|---|
| 128-323Hz notch (dB, relative to neighbors) | -5.47 | +0.20 (fixed) | +0.31 (fixed) |
| IACC | ~0.0352 | 0.0712 (regressed) | 0.0547 |

Net result across all 9 real captures after the final set: the specific 161Hz notch is gone
(flows smoothly with its neighbors now, not a standout dip), log-spectral distance improved
2.93dB (from 3.19dB before this delay-line work), and IACC at High=0 settings measurably
IMPROVED (e.g. 0.0429->0.0307 at Time=4.8) while negative-High settings, though still above real
hardware's own very low values (a persisting, already-documented gap - see the stereo
decorrelation item above), are clearly better than the discarded first candidate and only
modestly behind the original set's own negative-High numbers - a real, disclosed three-way
trade-off (EQ vs. neutral-IACC vs. negative-High-IACC) resolved without a straightforward win on
every single axis, not pretended otherwise. A broader, milder ~3-6dB low-mid softness across
100-400Hz remained after this fix - see the next section for what that turned out to actually be.

**The remaining low-mid softness - NOT an EQ issue at all, a real gate-envelope bug**: measuring
its own time-evolution (0.5ms-resolution band envelopes, both render and real capture) found the
deficit was concentrated in the PLATEAU/sustain portion specifically and converged or reversed
during the fall - and appeared identically in 500-1000Hz and 1000-2000Hz too, not just 100-400Hz,
ruling out a spectral/EQ explanation. Traced to `plateau_droop_db_per_s`: at Time=9.8/High=0, the
real capture's own directly-measured droop is -10.3dB/s; the exported curve had -33.0dB/s - a
real bug in `build_measured_gate_curves.py`'s own pooling loop, which averaged plateau_droop
across EVERY High value at a given Time (correct for tau_a_ms/t_knee_ms/fall_rate_db_per_s, which
genuinely are close enough to High-neutral for that pooling to be an honest compromise) even
though `plateau_droop_db_per_s` is the ONE parameter this project's own findings already
established as NOT High-neutral - averaging in Time=9.8's own High=-4/-9 captures (-45.1/-43.6dB/s)
corrupted the High=0 baseline by more than 3x.

Fixing the pooling bug alone wasn't enough - verifying the fix (rendering and re-measuring, not
assuming) found the render's own total droop STILL didn't match the corrected target. Reason:
`plateauDroopDbPerSec` is ADDITIVE to whatever droop the tank already produces on its own
(`InhaltIRSynth.cpp`'s gate formula sums attackDb + plateauDb + kneeDb on top of a tank that
already decays somewhat from `feedbackGain < 1`) - setting it directly to the real capture's own
measured TOTAL double-counts the tank's own natural contribution. Verified directly at Time=9.8:
tank-alone natural droop measured -38.5dB/s; setting the explicit term to the naive -10.3dB/s
target produced a rendered TOTAL of -48.8dB/s (matching natural + naive exactly, not the target at
all); the corrected explicit value (target minus the tank's own natural droop, +28.2dB/s) produced
a rendered total of -11.3dB/s, matching within noise. A Python re-implementation of this
measurement (to keep it reproducible for future re-fits, not just a one-off manual patch) is
built into `build_measured_gate_curves.py` now, with a documented, verified escape hatch: at
Time=2.2 specifically, the Python estimate disagreed with a direct C++ measurement by 61dB/s
(traced to that Time's own short plateau window destabilizing the swept-breakpoint fit
differently between the two implementations) - all four Time settings now use hand-verified,
C++-measured constants rather than trust an approximation proven unreliable in at least one case.

Net result at all four High=0 settings: plateau droop error dropped from -25 to -38dB/s down to
0.2-1.8dB/s (essentially exact), and the 100-2048Hz LTAS deficit that originally looked like an
EQ problem is now within +-1.5dB almost everywhere (was -3 to -9dB) - confirming the low-mid
softness was a gate-envelope bug wearing an EQ-shaped disguise, not a second tank/topology issue
on top of the notch fix above.

**Knee timing at negative High - the previously-reverted High-offset fix, tried again after
tracing WHY it regressed the first time**: a real, ear-caught complaint ("it feels like the
convolution has a bit less of the reverb ringing out") pointed at the gate's own knee timing.
Measured directly: at Time=7.0/9.8 and High=0, the render's knee lands 44-59ms LATE (the plateau
holds measurably longer than real hardware before the fall begins) - the same shape of gap the
original High-offset attempt tried to close and got reverted over, months (well, hours) earlier
in this same session. Tracing the regression found the actual bug: `t_knee_ms`'s Time-only
baseline pools Time=0.1s/0.8s's own raw High=-3 measurement directly in, so adding a FURTHER
High-dependent offset on top double-counted that capture's own High=-3 effect - the exact same
double-counting bug class as `plateau_droop_db_per_s`'s fix above, not a genuine "the data doesn't
support an offset" finding as first assumed. Fixed the same way: every capture is converted to an
H=0-equivalent value (raw minus the offset curve, evaluated at that capture's own High) before
the Time baseline is built, so the offset and baseline stay self-consistent and can be safely
combined in `InhaltParameterMap.cpp` now. Doing this correctly also unmasked a genuine measurement
quirk in the raw data (Time=7.0's own real High=0 knee measures 190ms, slightly BELOW Time=4.8's
own 204.875ms - a single-capture noise artifact, not a hardware non-monotonicity, confirmed
against the independently-measured, already-trusted `gate_length_ms_at_20db` table which shows
strict monotonic increase there) - closed with isotonic regression (pool-adjacent-violators) on
the corrected baseline, the smallest edit that restores the non-decreasing order a hardware-
labeled Time knob should have.

Net result: knee-time error at High!=0 dropped from -32.6ms (flagged) to -13.6ms (no longer
flagged) - Time=9.8/High=0 alone went from +58.9ms to -0.9ms, and Time=7.0/High=-7 from -53.6ms to
+5.2ms. The two shortest-Time captures (0.1s/0.8s, High=-3 only) are unchanged by construction -
correcting them to an H=0-equivalent baseline and then re-adding the same offset at their own
captured High reconstructs their original values exactly, so this fix doesn't touch their already-
documented gap at all.

**Harmonics/saturation - reopened after being explicitly scoped out at the project's start**: a
real, ear-caught complaint ("the convolution still has more brassiness and maybe like harmonic
richness") led to three separate linear-architecture experiments, each measuring
`core.features.resonant_peaks()`'s own peak count/Q against the real captures' own (real hardware:
~7 peaks, mean Q~6.6; this engine before this fix: ~11-14 peaks, mean Q~10-11) - reducing
`dampingWeight`, reducing `feedbackGain`, and even HALVING the tank's own line count (8 lines -> 4,
a quick Python-only diagnostic, not shipped) all left peak count/Q essentially unmoved. A linear
FDN, however tuned, can only redistribute and delay energy already present in its input - it
cannot manufacture new harmonic content, which is exactly what "brassiness"/"harmonic richness" as
descriptors point at. This is real, if indirect, evidence the remaining gap is the actual
hardware's own nonlinear saturation character, not a reachable modal/EQ property of this engine's
tank - the same territory the project plan's original "Skip harmonics entirely" decision
deliberately excluded (0.002-0.03% THD+N wasn't judged worth a dedicated capture session at the
time).

Reopened with Adam's explicit go-ahead, SPEC-DERIVED rather than measured (no capture exists that
isolates the real unit's own saturation curve - the 9 NonLin captures are single-impulse-response
style, not the level-swept sine material a real THD curve needs): a simple, standard, unity-gain
tanh waveshaper (`y = tanh(drive*x)/tanh(drive)`, no hard clipping) added to the existing Converter
stage (Vintage/Modern), with `drive` numerically solved so a 0dBFS sine produces the spec sheet's
own worst-case THD at each position - 0.03% for Vintage, 0.002% for Modern, the same "Vintage is
the more colored position" asymmetry the bandwidth/quantization figures already use. Deliberately
NOT calibrated against `resonant_peaks()`'s own count/Q numbers (that would just be curve-fitting a
linear-domain metric with a nonlinear knob, the same mismatch the three ruled-out experiments
already demonstrated) - this is a first, honest, spec-grounded pass, not a verified match to the
real hardware's own harmonic character. A future dedicated capture session (level-swept sine or
similar through the real unit) would be needed to measure and refine this properly, per Adam's own
"wait for new captures" option, deferred rather than chosen this session.

Two further things are documented as genuine, open gaps rather than silently fixed or hidden:

- Rendered stereo decorrelation (IACC ~0.04-0.08) is closer to the real hardware's (~0.006-0.04)
  after an asymmetric-delay-range change, but not fully matched - see `InhaltIRSynth.cpp`'s own
  comment on what was tried (more lines made it worse; removing the shared gate envelope barely
  moved it) and what actually helped.
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
| Converter | Vintage / Modern | AMS RMX16 spec sheet (bandwidth + noise floor + a spec-derived saturation stage, ~0.03%/~0.002% THD - see "Status" below for why harmonics were reopened) |
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
