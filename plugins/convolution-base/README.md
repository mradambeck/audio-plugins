# ConvBase

**A development harness, not a shipped plugin.**

ConvBase exists so the shared convolution engine in
[`../common/convolution/`](../common/convolution/) has a real plugin around it — something CI can
build and test, and something that loads in a DAW so the engine can be heard. It is not in the
product catalogue, has no installer, and does not appear on the site.

The actual products built from this engine are **branded variants**: one name, one accent colour
pair and one bundled set of impulse responses each. They live in a separate private repository that
consumes this one as a git submodule, because their IRs must not enter public git history.

## What a variant is

The shared engine owns the processor, the editor, the DSP, and the plugin entry point. A variant
supplies two functions and some audio files — there is no per-variant processor, editor or
LookAndFeel subclass to write:

| File | What it provides |
|---|---|
| `Source/VariantConfig.cpp` | `variantConfig()` — the IR table, names, and default selection |
| `Source/VariantTheme.cpp` | `variantTheme()` — the accent colours and typefaces |
| `irs/*.flac` | the impulse responses themselves |
| `CMakeLists.txt` | the plugin's name, `PLUGIN_CODE`, `BUNDLE_ID` and its BinaryData list |

Those two function declarations are the entire contract, and they live in
`../common/convolution/ConvolutionVariant.h` and `ConvolutionVariantTheme.h`.

## The IRs here are synthetic

`irs/` holds four generated impulse responses, not captures. Each one exists to exercise a specific
path through the engine:

| File | Exercises |
|---|---|
| `dirac-48k.flac` | convolution as an identity operation — the measurement tool behind the zero-latency and transparency tests |
| `room-stereo-48k.flac` | stereo at the session rate: no resampling |
| `hall-stereo-44k.flac` | 44.1 kHz in a 48 kHz session: upsampling |
| `plate-mono-96k.flac` | 96 kHz and mono: downsampling, plus the mono-IR branch of `loadIR()` |

Regenerate with `python3 tools/make_test_irs.py` (needs numpy; uses macOS's built-in `afconvert`).
The RNG seed is fixed, so a diff in `irs/` means the generator changed.

## Build and test

```bash
cmake -B build -G Xcode
cmake --build build --config Release --target ConvBaseTests
./build/ConvBaseTests_artefacts/Release/ConvBaseTests      # exit 0 = all passed

cmake --build build --config Release --target ConvBase_All
auval -v aufx CvBs WJag
```

`COPY_PLUGIN_AFTER_BUILD` is on, so building `ConvBase_All` installs a **ConvBase - Convolution**
AU/VST3 alongside the real catalogue. That is deliberate — the harness is meant to be auditioned —
but it does mean a dev-only plugin shows up in your DAW's plugin list.

## What the tests hold in place

The suite is where the engine's non-obvious claims are checked, because none of them survive
inspection alone:

- **Zero latency.** `getLatencySamples()` is asserted to be 0, *and* an impulse convolved with the
  Dirac IR is asserted to emerge at sample 0. The non-uniform partitioning is chosen for CPU; this
  is the check that it did not quietly buy that back as delay.
- **Transparent defaults.** Length 100%, Attack 0 and both filters at their extremes must leave a
  signal alone. `IRShaper` is asserted to be *bit*-identical at its defaults; the filters are
  skipped outright rather than run at a near-transparent cutoff.
- **No clicks.** Bypassing mid-tail and swapping the IR mid-tail are each compared against an
  untoggled control run, measuring the worst sample-to-sample step across the transition.
- **Resampling preserves onset time.** `juce::LagrangeInterpolator` has a 2-sample group delay,
  which `IRLibrary::resample()` compensates for — otherwise the same room captured at 44.1 kHz and
  at 48 kHz would start at different moments in the same session.

## Registration

Registered in `.github/workflows/build-and-test.yml` (both matrices), `scripts/build-all.sh` and
`scripts/test-all.sh`.

Deliberately **not** registered in `release.yml`, `installers/`, `sync-site-versions.yml`, the
gh-pages site, or `download-counter/src/index.ts` — it is not a product. Adding it to any of those
would publish a dev harness as a Wild Jag plugin.
