# Bloom

*Shoegaze inspired reverb.* (Highlight colour: `#D74377`.)

A diffuse algorithmic reverb (AU / VST3 / Standalone) emulating the Alesis Midiverb II "Bloom"
algorithm (presets 45 and 49): energy density builds slowly before decaying, rather than a
discrete-tap swelling delay or an envelope applied to a normal reverb tail. The buildup emerges
from an 8-line, Hadamard-mixed feedback delay network (FDN) run with the diffusion coefficient
near its 0.5 sweet spot - see "How it works" below for why that specifically is what produces the
character, and what "buildup" actually means here.

See the [root README](../README.md) for shared build requirements, the exFAT/apostrophe build
gotchas, and running tests across all plugins at once.

## Building

```sh
cd bloom-reverb
cmake -B build -G Xcode
cmake --build build --config Release --target Bloom_All
```

To build a single format only: `--target Bloom_AU`, `Bloom_VST3`, or `Bloom_Standalone`.

## Installation

`COPY_PLUGIN_AFTER_BUILD` is enabled, so a successful build automatically copies the plugin into
the standard user plugin directories:

- **AU:** `~/Library/Audio/Plug-Ins/Components/Bloom.component`
- **VST3:** `~/Library/Audio/Plug-Ins/VST3/Bloom.vst3`

Restart your DAW (or run AU validation, below) after installing. The Standalone app is built to
`build/Bloom_artefacts/Release/Standalone/Bloom.app`.

## Launching the Standalone app

```sh
cmake --build build --config Debug --target Bloom_Standalone
open build/Bloom_artefacts/Debug/Standalone/Bloom.app
```

## Validating the AU (auval)

```sh
auval -v aufx Blom WJag
```

## Offline validation workflow

This plugin's DSP is tuned against real hardware captures rather than by ear alone. Two extra
pieces support that, on top of the usual `BloomTests` unit-test target:

1. **`reference-irs/`** - ground-truth Midiverb II impulse response captures. See that folder's own
   README for expected filenames. Never read at runtime; the algorithm is real-time and parametric,
   not a convolution reverb.
2. **`BloomRenderIR`** (a console app, not a plugin format) - feeds an impulse through the real
   `BloomAudioProcessor` and writes the result to WAV:

   ```sh
   cmake --build build --config Release --target BloomRenderIR
   build/BloomRenderIR_artefacts/Release/BloomRenderIR --out rendered-irs/mine.wav --seconds 4 \
       --diffusion 0.5 --feedback 90 --size 1.0 --damping 35 --bandwidth 15000 --bitdepth 16
   ```
3. **`tools/compare_irs.py`** - scores a rendered IR against a reference one (RMS envelope overlay,
   echo-density-over-time overlay, spectrogram, envelope correlation, log-spectral distance):

   ```sh
   python3 -m venv .venv && source .venv/bin/activate && pip install -r tools/requirements.txt
   python3 tools/compare_irs.py rendered-irs/mine.wav reference-irs/preset-45.wav
   ```

Rerun steps 2-3 after every parameter/topology change while tuning - that's the point of having
them as scripts rather than one-off checks.

## How it works

The core is `BloomFDNEngine`: 8 delay lines (mutually prime lengths, so no periodic reinforcement/
metallic ringing), mixed every sample by a fixed 8x8 Hadamard matrix (energy-preserving, so overall
stability is governed purely by the scalar Feedback gain and the per-line one-pole Damping filter,
not by the matrix). A short 3-stage allpass chain diffuses the input ahead of the network. No
delay-line modulation anywhere - the network is deliberately static/unmodulated, matching the
original hardware's grainy character.

**On "buildup":** an orthogonal (energy-preserving) cross-mix scaled by a single feedback gain is
provably front-loaded - its *raw energy/RMS* can only ever be highest at the moment of injection
and fall from there (confirmed empirically while building this; see `BloomFDNEngineTests.cpp`'s
comments for the reasoning). What genuinely does build over time in this topology is **echo
density**: the count of distinguishable reflections per time window, which starts sparse (a
handful of first-order arrivals) and measurably increases as multiple generations of reflections
through the Hadamard mix interleave into a denser texture, before eventually thinning out again as
the tail decays. That density curve - not the loudness contour - is Bloom's actual signature, and
it's what both `BloomFDNEngineTests.cpp` and `tools/compare_irs.py` measure. `Size` scales the
delay line lengths and is therefore the de facto attack-time control: longer lines mean more
samples/round-trips before the network reaches full density, i.e. a slower bloom.

Lo-fi coloration (bandwidth-limiting lowpass + bit-depth quantization) is applied to the wet output
after the FDN, reintroducing the ~15kHz bandwidth and 12-16 bit grain of the original hardware -
both are tunable parameters rather than hardcoded, so they get dialled in against the reference IRs
rather than guessed.

The UI is currently a stock `GenericAudioProcessorEditor` (auto-built sliders, one per parameter) -
per the build order, the hardware-panel UI (see the `juce-hardware-panel-ui` skill; accent colour
`#D74377`) comes only after the core algorithm is validated against the reference IRs.

## Parameters

| Parameter | Range | Description |
|---|---|---|
| Diffusion | 0.3 - 0.7 | Input allpass diffuser coefficient; centered near 0.5 per the Bloom spec |
| Feedback | 0 - 100% | Decay/tail-length control (internally capped below unity gain) |
| Size | 0.25x - 4.0x | Scales the FDN's delay line lengths - the de facto attack-time control |
| Damping | 0 - 100% | Per-line feedback-path one-pole lowpass - controls HF decay rate |
| Bandwidth | 1kHz - 20kHz | Output lowpass cutoff (lo-fi bandwidth limit) |
| Bit Depth | 4 - 16 bit | Output quantization depth (lo-fi grain) |
| Mix | 0 - 100% | Dry/wet balance |
| Bypass | on/off | |

## Project structure

```
bloom-reverb/
├── CMakeLists.txt
├── Source/
│   ├── PluginProcessor.h/.cpp   # Parameter state, dry/wet mix, buffer plumbing
│   ├── BloomFDNEngine.h/.cpp    # The diffuse reverb core (see "How it works" above)
│   ├── PluginEditor.h/.cpp      # Currently a stock GenericAudioProcessorEditor
│   ├── Tests/                   # BloomTests: headless UnitTest console app (DSP core only)
│   └── Tools/RenderIR.cpp       # BloomRenderIR: offline IR-render console app
├── reference-irs/                # Ground-truth Midiverb II captures (see its own README)
├── rendered-irs/                 # BloomRenderIR output (gitignored, regenerable)
├── tools/compare_irs.py          # Offline IR comparison/scoring script
└── installer/                    # This plugin's .pkg installer (see installers/README.md)
```

## License

[AGPLv3](../LICENSE), same as the rest of this repo.
