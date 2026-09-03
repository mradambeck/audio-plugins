# ML-Assisted Audio Effect Replication: A Reusable Toolkit

## Purpose

A shared pipeline for turning captured reference audio from real hardware, impulse responses or paired dry/wet recordings, into fitted, real-time-portable DSP parameters. Built once as a general tool, with each hardware algorithm as a module plugged into it rather than a standalone script rebuilt from scratch each time.

The toolkit has two halves: a Python-side fitting pipeline that turns captures into parameters, and a small shared JUCE-side runtime that loads those parameters into a plugin.

## Python toolkit structure

**`core/io.py`**
Loads capture manifests. Two capture formats need support:

- IR-based: a single impulse response per parameter combination. Works for linear, time-invariant effects only (reverb, static EQ, cab emulation).
- Paired dry/wet: an arbitrary input signal recorded through the hardware alongside its output. Needed for anything nonlinear or time-varying (distortion, compression, chorus, pitch shifting).

Each capture needs its parameter values attached. For many this will be in the filenames, though if a JSON file or something else is needed, we can accomadate.

**`core/features.py`**
Shared analysis functions, each effect module uses the subset it needs:

- Spectral envelope and band-energy comparison against a flat reference, for the tonal coloration present in almost every hardware effect independent of its primary function.
- Multi-band decay curves (Schroeder backward integration) and echo density, for reverbs
- Harmonic/THD analysis relative to a known fundamental, for distortion-type nonlinearities
- Envelope/RMS tracking, for compressors and gates
- Pitch and periodicity tracking, for pitch-shifting and harmonizer effects
- Modulation extraction (LFO rate and depth from delay-time variation over time), for chorus, flanger, and vibrato

**`core/dsp_primitives.py`**
Differentiable building blocks in PyTorch. Effect-specific models are compositions of these, not bespoke implementations:

- Delay line, fixed and fractional/interpolated (fractional needed for modulation)
- Feedback matrix (Householder or Hadamard) for FDN-style diffuse networks
- Biquad and shelf filters, for damping and tone shaping
- Parametric static waveshaper, for distortion-type nonlinearities
- Envelope follower, for dynamics-based effects
- LFO with learnable rate and depth, for modulation effects

**`core/fit.py`**
Effect-agnostic optimization harness. Takes a composed model, a loss (built from `core/features.py` metrics), and a target capture, then runs Adam to convergence with logging and a held-out check. This is the same code for every effect; only the model and loss selection change per module.

**`core/interp.py`**
Fits smooth curves or splines across a captured parameter grid. Works for any effect with more than one captured setting, not just reverb decay time.

**`core/export.py`**
Writes fitted parameters to a portable format (JSON or a compact binary table) with a schema the JUCE side can load, plus optional C++ header codegen for parameters you want baked in at compile time instead of loaded at runtime.

**`effects/<name>/`**
One folder per effect. Each contains a short config or script that composes primitives into that effect's model, picks the relevant features and loss weights, and calls `core/fit.py`. This is the only effect-specific part, and it should stay small: composition and configuration, not new infrastructure.

## JUCE-side runtime

A shared `FittedParameterTable` class that loads the format written by `core/export.py` and reproduces the same interpolation logic as `core/interp.py` at runtime. Each plugin still owns its own DSP (the FDN for ambience, whatever the H910 needs), but parameter loading and interpolation are written once.

## ambience as the first module

`effects/ambience/` composes the delay-line, feedback-matrix, and biquad-damping primitives into an FDN, uses the Schroeder decay-curve and echo-density features for the loss, fits through the shared harness, and interpolates through the shared spline module. None of the reverb-specific design changes, it becomes the first thing built against the shared core instead of code that only knows about reverb.

## Extending to future effects

Currently we will start with the ambience Reverb, but in the future will move into other reverb IR's and then into some other effects such as spreaders, modulation, distortion.

## Build order

Build only what ambience needs first: IR loading, the decay and echo-density features, the delay/feedback-matrix/biquad primitives, the fitting harness, spline interpolation, and the export format. Leave the rest as known gaps rather than building fractional delay, pitch tracking, or waveshaping primitives before a second effect actually needs them. That keeps the abstraction honest, since you won't know if the module boundaries are right until something other than reverb is built against them.

## Ambience background

I have 65 IR's from an AMS RMX16 Ambience algorithm. They are titled things like "Ambience*1.8s*+5L-2H.wav". Ambience is the name of the setting, 1.8s is the timing set on the hardware (1.8 seconds), +5L means the Low knob was turned to +5, -2H means the High knob was turned to -2.

When the folder structure is complete, I can put all of these into a folder for you to analyze.
