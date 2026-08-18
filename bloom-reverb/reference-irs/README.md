# reference-irs/

Ground-truth impulse response captures from a real Alesis Midiverb II, used to tune Bloom's FDN
parameters and to score `tools/compare_irs.py` runs. **Not** used at runtime - Bloom is a real-time
parametric algorithm, not a convolution reverb; these files only ever get read offline by the
tuning workflow.

## Expected files

Drop the captures in directly (WAV, any sample rate/bit depth - `compare_irs.py` resamples as
needed). Suggested naming, matching the two Bloom presets called out in the spec:

```
reference-irs/
├── preset-45.wav
└── preset-49.wav
```

If a capture has significant pre-impulse silence or a visible direct-click before the diffuse
tail starts, note that in this file (or trim it) so `compare_irs.py`'s alignment stays honest -
the comparison script does a simple onset-alignment, not a full cross-correlation search.

## Usage

Once files are here, render Bloom's own IR at matching settings and compare:

```sh
cd bloom-reverb
build/BloomRenderIR_artefacts/Release/BloomRenderIR --out rendered-irs/mine.wav --seconds 4 \
    --diffusion 0.5 --feedback 90 --size 1.0 --damping 35 --bandwidth 15000 --bitdepth 16
python3 tools/compare_irs.py rendered-irs/mine.wav reference-irs/preset-45.wav
```

See `tools/compare_irs.py`'s own header comment for what each metric means and how to read the
similarity score.
