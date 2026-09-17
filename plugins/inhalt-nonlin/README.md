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

Two things are documented as genuine, open gaps rather than silently fixed or hidden:

- Rendered stereo decorrelation (IACC ~0.04-0.08) is closer to the real hardware's (~0.006-0.04)
  after an asymmetric-delay-range change, but not fully matched - see `InhaltIRSynth.cpp`'s own
  comment on what was tried (more lines made it worse; removing the shared gate envelope barely
  moved it) and what actually helped.
- Gate timing at very negative High still shows real per-capture error - an additive High-offset
  fix was tried and reverted after it badly regressed short-Time captures despite improving the
  aggregate mean (see `InhaltParameterMap.cpp`'s own comment). Needs a High sweep at a short Time
  setting the current 9-capture set doesn't have.

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
