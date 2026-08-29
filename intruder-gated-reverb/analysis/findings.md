# Phase 2 findings: what Decay, H, and Tighter actually do

Based on `analysis/analyze_irs.py` output (`features.json`, `plots/*.png`) over all 19 captures.
Numbers below are Schroeder RT60 (measured over the -5 to -25dB region of the backward-integrated
energy curve — the only cleanly linear region available in these short captures), spectral tilt
(10·log10(HF/LF energy), 2kHz split), and echo-density half-rise time (time for peak-count/10ms to
reach half its eventual peak). All fits verified by eye against the envelope plots, not just the
numbers — see the "Method notes" section for a false lead that visual inspection caught.

## Decay: NOT literal seconds — a compressed, monotonic internal scale

The filename decay label spans 0.1–9.8s (98x range). Measured RT60 spans only 0.19–0.49s (2.5x):

| label (s) | RT60 (s) | rms→-60dB (s) |
|---|---|---|
| 0.1  | 0.192 | 0.33 |
| 0.8  | 0.192 | (didn't reach -60dB in-frame) |
| 2.2  | 0.227 | — |
| 4.8  | 0.318 | — |
| 7.0  | 0.42–0.43 | 0.44–0.53 |
| 9.8  | 0.45–0.49 | 0.46–0.55 |

RT60 increases monotonically with the label — 0.1s and 0.8s are statistically identical
(0.1918 vs 0.1918/0.1912s), then it climbs — but the mapping is clearly nonlinear/compressed, not
1:1 seconds. This confirms IMPLEMENTATION.md's caution: **the label is a decay control's nominal
value on the RMX16's own display scale, not a literal RT60 in seconds.** For Phase 4, the plugin's
Decay parameter should be fit as a monotonic curve through these 6 measured RT60 points (e.g.
piecewise-linear or a light power-law fit) and treated as continuous/extrapolable beyond them, per
IMPLEMENTATION.md's Phase 4 guidance — just not with a seconds-literal mapping.

**Also confirmed by eye (not just the RT60 number):** the envelope is not a simple exponential.
Every capture shows a hold near the peak (roughly flat within -5 to -10dB) for ~0.1–0.25s before a
knee into a steeper decay — see `plots/NonLin_9.8s_0H.png`. This is the non-linear envelope shape
IMPLEMENTATION.md Phase 1 asked to look for, and it's why Phase 3's separate multiplicative
envelope-shaper stage (attack/hold/knee/decay) is the right architecture rather than relying on
the FDN's natural decay alone.

## H: a bass/treble TILT control (not HF damping) — pivots around ~1-4kHz, active from the first reflections

Spectral tilt at onset (`spectral_tilt_start_db`), non-Tighter files only, is consistent **across
different Decay settings** and tracks H alone (decay-independent — e.g. H=-3 gives 4.73dB tilt at
both 0.1s and 0.8s decay; H=0 gives 9.66-9.68dB across four different decay settings). That alone
rules out "H only damps the late feedback loop" — it's present from the very first energy in the
signal, not just a slow darkening over the tail.

The 2-band HF/LF ratio only tells half the story, though. Breaking onset energy into four bands
(controlling for decay — the -9/-7/-4/0 group is all 7.0-9.8s decay) shows *which* bands actually
move:

| H (dB) | 20-250Hz | 250-1000Hz | 1-4kHz | 4-20kHz |
|---|---|---|---|---|
| -9 | 17.78 | 24.12 | 25.92 | 20.96 |
| -7 | 16.45 | 23.02 | 25.60 | 21.66 |
| -4 | 13.82 | 22.37 | 25.97 | 23.43 |
| 0  | 9.35–9.38 | 18.07–18.08 | 22.50–22.58 | 25.39–25.43 |

As H goes from 0 to -9: **sub-bass rises +8.4dB, low-mid rises +6dB, the 1-4kHz midrange barely
moves (~3dB, not even monotonic through -9/-7/-4), and 4-20kHz treble falls -4.5dB.** Bass and
treble move in *opposite* directions around a pivot near 1-4kHz — this is a tilt/shelf EQ
signature, not a low-pass damping signature. A pure one-pole LPF in the FDN feedback path (as
IMPLEMENTATION.md Phase 3 sketches) cannot produce a bass boost, so it can't be the whole
mechanism on its own.

**Revised recommendation for Phase 3/5:** model H as a broadband tilt filter (complementary
low-shelf boost + high-shelf cut, or an equivalent single tilt-EQ structure) pivoting around
~1-4kHz, applied early enough to affect the first reflections (input/output stage, not buried
only in the feedback loop) — and additionally let it modulate the FDN feedback damping coefficient
so the effect keeps compounding over the tail (the original one-pole-damping idea isn't wrong, it's
just incomplete on its own). Internal convention stays H-in-dB matching the hardware's scale;
rename the UI parameter something like "Tone" or "Tilt" rather than "HF Damp" once locked in, since
"HF Damp" undersells the bass side of what it does.

## Tighter: compresses early-reflection timing and speeds diffusion buildup — not a decay or tone control

Every one of the 9 available pairs shows the same effect, cleanly and consistently:

| pair | echo-density half-rise, off → Tighter (s) |
|---|---|
| 0.1s -3H  | 0.1247 → 0.0998 |
| 0.8s -3H  | 0.1098 → 0.0898 |
| 2.2s 0H   | 0.1197 → 0.0848 |
| 4.8s 0H   | 0.1148 → 0.0798 |
| 7.0s -7H  | 0.3093 → 0.2694 |
| 7.0s 0H   | 0.1796 → 0.1347 |
| 9.8s -4H  | 0.3193 → 0.2594 |
| 9.8s -9H  | 0.3343 → 0.2694 |
| 9.8s 0H   | 0.1796 → 0.1148 |

Tighter always reduces the time it takes for the diffuse field to build up — by roughly
0.025–0.065s in every case, scaling somewhat with the base decay setting. Early-reflection tap
times shift earlier by a similar proportion with a near-identical relative spacing pattern between
taps (e.g. 0.1s -3H: taps at 18.4/20.9/37.5/40.0ms → 7.9/10.4/16.1/18.6ms with Tighter — compressed
in time, not reordered or re-gained). RT60 also drops slightly with Tighter (e.g. 9.8s 0H:
0.488→0.446s) as a secondary effect. This matches IMPLEMENTATION.md's hypothesis directly:
**Tighter is a diffusion-onset/definition control — it compresses the early-reflection tap
spacing and shortens time-to-dense-diffusion, with a mild knock-on effect on overall decay length,
not a tone or primary decay-time control.**

## Recommendation for Phase 3 parameter set

- **Decay**: continuous, curve-fit from the 6 measured RT60 points (non-literal-seconds mapping).
- **H → "HF Damp"**: FDN feedback-path damping coefficient, plus a fraction applied as
  input/output tilt so early reflections aren't unaffected.
- **Tighter → "Definition"/"Tighter"**: scales early-reflection tap spacing and diffusion-onset
  time; implement as a parametrized spacing multiplier per IMPLEMENTATION.md Phase 3, not a fixed
  tap set swap.

## Method notes (empirical-verification catch)

An early pass suspected a "trailing blip" artifact — a spike appearing after a long silence gap
near the end of several files — and built logic to detect and strip it. Plotting the raw sample
values in that region (not the Hilbert envelope) showed pure digital silence with no spike at all.
The apparent blip was an FFT edge-wraparound artifact of computing `scipy.signal.hilbert` over the
whole buffer (a loud onset followed by a tail that doesn't return to zero creates a large
implied discontinuity under the FFT's periodicity assumption, which leaks into a spurious envelope
value near the boundary). Fixed by zero-padding before the Hilbert transform and trimming after
(`analyze_irs.py::hilbert_envelope_db`). No files needed any blip-stripping — the stripping logic
was removed. Flagging this since it would have been a false "empirically verified" finding if not
checked against the raw samples directly.
