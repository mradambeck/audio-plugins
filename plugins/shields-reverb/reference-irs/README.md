# reference-irs/

Ground-truth impulse response captures from a real Alesis Midiverb II, used to tune Shields's FDN
parameters and to score `../common/tools/compare_wavs.py` runs. **Not** used at runtime - Shields
is a real-time parametric algorithm, not a convolution reverb; these files only ever get read
offline by the tuning workflow.

## Expected files

Drop the captures in directly (WAV, any sample rate/bit depth - `compare_wavs.py` resamples as
needed). Suggested naming, matching the two Bloom presets called out in the spec:

```
reference-irs/
├── preset-45.wav
└── preset-49.wav
```

If a capture has significant pre-impulse silence or a visible direct-click before the diffuse
tail starts, note that in this file (or trim it) so `compare_wavs.py`'s alignment stays honest -
the comparison script does a simple onset-alignment, not a full cross-correlation search.

## Usage

Once files are here, render Shields's own IR at matching settings and compare:

```sh
cd shields-reverb
build/ShieldsRenderIR_artefacts/Release/ShieldsRenderIR --out rendered-irs/mine.wav --seconds 4 \
    --diffusion 0.5 --feedback 90 --size 1.0 --damping 35 --bandwidth 15000 --bitdepth 16
python3 ../common/tools/compare_wavs.py rendered-irs/mine.wav reference-irs/preset-45.wav
```

See `../common/tools/compare_wavs.py`'s own header comment for what each metric means and how to
read the similarity score. This script is shared across every plugin in the catalog, not
Shields-specific.
