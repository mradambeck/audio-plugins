# Phase B3 findings: what Time, Low, and High actually do

Based on `effects/ambience/analyze.py`'s output (`features.json`, `plots/*.png`) over all 65
captures, plus targeted ad hoc checks (onset 4-band energy breakdown, stereo correlation) run
directly against specific capture pairs where `features.json`'s existing fields didn't settle a
question on their own. All numeric claims below were checked against the underlying plot or raw
signal, not just the summary number - see "Method notes" for one place that mattered.

## Capture design (not a full grid)

65 captures, 8 Time settings (0.1-5.5s) x a sparse set of 15 distinct (Low, High) pairs, not a
full 8x11x7 grid. Three (Low, High) points are swept across all 8 Time settings and form the main
reference set: **(-2,-2)**, **(0,0)**, **(5,-8)**. Separately, **Low is swept from -8 to 0 with
High fixed at 0** (6 points, 6 of the 8 Time settings), and **High is swept from -8 to 0 with Low
fixed at 0** (4 points, 6 of the 8 Time settings) - these two subsets are what make it possible to
isolate each knob's individual effect below. The remaining points (positive Low paired with
negative High, and a few one-off corners) don't isolate a single knob but were useful for the
Low/High interaction check.

## Time: close to literal seconds (unlike Intruder's compressed "Decay" scale)

At (Low=0, High=0), the Time label vs. `full_decay_rt60_s`:

| label (s) | full_decay_rt60 (s) | schroeder_rt60 (s) | full_decay_r2 | echo_density_half_rise (s) |
|---|---|---|---|---|
| 0.1 | 0.484 | 1.084 | 0.898 | 0.010 |
| 0.5 | 0.549 | 0.656 | 0.992 | 0.479 |
| 1.1 | 0.805 | 0.940 | 0.993 | 0.185 |
| 1.8 | 1.476 | 1.514 | 0.997 | 0.115 |
| 2.3 | 2.290 | 2.158 | 0.998 | 0.105 |
| 3.0 | 3.361 | 2.939 | 0.997 | 0.105 |
| 4.5 | 4.315 | 4.052 | 0.999 | 0.100 |
| 5.5 | 5.305 | 5.127 | 0.999 | 0.100 |

From label 1.1s upward this is **very close to a literal 1:1 mapping** (unlike Intruder's Decay,
which spanned only 0.19-0.49s of real RT60 across a 0.1-9.8 label range - a genuinely different
hardware behavior between these two RMX16 programs, not something to assume transfers). Below
~1.1s there's a soft floor/compression: the 0.1s and 0.5s labels land close together (0.48s and
0.55s) rather than at 0.1/0.5s themselves, and `full_decay_r2` is visibly worse at 0.1s (0.898 vs.
>0.99 everywhere else) - a genuine model-fit-quality difference, not just a small numeric outlier.

**Confirmed by eye, and this is why the R2 is worse at 0.1s specifically**: at the shortest Time
setting, the envelope isn't a single decay at all. `plots/Ambience_0.1s_0L0H.png` shows two
isolated, near-full-amplitude bounces near t=0.03s and t=0.11s against an otherwise-quiet (-50 to
-60dB) background, then a distinctly denser, brighter "wash" that starts around t=0.19-0.2s
(visible as a sharp step up in both the echo-density and spectral-tilt panels) before decaying
away by ~0.4s. **`plots/Ambience_3.0s_0L0H.png` shows none of this** - dense, smooth decay from
the very first sample, stable spectral tilt throughout, echo density ramping up smoothly over
~0.5-0.8s and staying flat. This is consistent with a single fixed-topology FDN whose individual
echoes are simply audible/resolvable at very short decay times (before enough of them accumulate
to sound dense) rather than evidence of two structurally different stages - **no separate early-
reflection-tap layer looks necessary for this model**, unlike Intruder (which needed one, then
removed it later for sounding like slapback anyway - see intruder-gated-reverb's
`validation_report.md` "Status" section).

## High: a broadband tilt, exactly like Intruder's H - AND it shortens decay

Onset 4-band energy (20-250 / 250-1000 / 1-4k / 4-20k Hz, dB), High swept -8..0 with Low=0 fixed,
Time=2.3s:

| High (dB) | 20-250Hz | 250-1000Hz | 1-4kHz | 4-20kHz | full_decay_rt60 (s) | tilt_start (dB) |
|---|---|---|---|---|---|---|
| -8 | 3.13 | 9.38 | 12.83 | 12.26 | 1.950 | 1.42 |
| -4 | -0.35 | 6.07 | 11.01 | 13.40 | 2.034 | 4.83 |
| 0 | -4.34 | 2.14 | 7.89 | 14.86 | 2.290 | 9.41 |

As High goes from 0 to -8: bass rises +7.47dB, low-mid rises +7.24dB, upper-mid rises +4.94dB, and
treble *falls* -2.60dB - bass and treble move in opposite directions, the same tilt-EQ signature
Intruder's H showed on the same physical hardware unit's other program. It's present at onset
(not just a slow tail darkening), and it also measurably shortens the decay as it decreases
(2.290s -> 1.950s from High=0 to High=-8) - a coupled tone+decay effect, again matching Intruder's
H. Recommendation: model High the same way `common/dsp/TiltFilter.h`/this toolkit's
`shelf_transfer_function` already does for Intruder - a broadband low/high shelf pivoting
somewhere in the low-mid, applied early enough to shape onset, plus feeding into the feedback
path's decay-affecting stage.

## Low: no onset-tone effect at all, and no measurable decay effect *unless High is very negative*

Low swept -8..0 with High=0 fixed, Time=2.3s - onset 4-band energy is flat within measurement
noise (<0.3dB across the whole sweep) and `full_decay_rt60`/`echo_density_half_rise` are equally
flat:

| Low (dB) | 20-250Hz | 250-1000Hz | 1-4kHz | 4-20kHz | full_decay_rt60 (s) | echo_half_rise (s) |
|---|---|---|---|---|---|---|
| -8 | -4.21 | 2.28 | 8.03 | 14.99 | 2.305 | 0.1048 |
| -4 | -4.36 | 2.13 | 7.88 | 14.84 | 2.291 | 0.1048 |
| 0 | -4.34 | 2.14 | 7.89 | 14.86 | 2.290 | 0.1048 |

Also checked stereo image directly (not just the mono downmix `core.features` analyzes) in case
Low is a width/decorrelation control instead of a tone control - ruled out, L-R correlation and
side/mid energy are equally flat across the same sweep (0.2728/0.2722/0.2721 correlation,
-2.43/-2.43/-2.42dB side/mid).

**But Low is not simply inert.** The captured grid has no positive-Low point at High=0 (the only
positive-Low captures are paired with negative High), so the above only rules out an effect on the
*negative* side at High=0. Comparing the one available matched-Time pair where Low differs with
High held at a *very negative* value instead (High=-8, Time=2.3s):

| Low (dB) | High (dB) | 20-250Hz | full_decay_rt60 (s) | tilt_start (dB) |
|---|---|---|---|---|
| 0 | -8 | 3.13 | 1.950 | 1.42 |
| +5 | -8 | 3.15 | 2.217 | 1.42 |

Onset tone is still identical (3.13 vs. 3.15dB bass, tilt_start exactly 1.42 both times) - but
`full_decay_rt60` is +0.267s (+13.7%) longer at Low=+5 than Low=0, with High held fixed. So **Low
never affects onset tone, and only measurably affects decay time when High has already pushed a
lot of energy into the low band** (recall High=-8 raises bass by +7.5dB over High=0). This reads
as Low being a **low-band-specific feedback/decay parameter** (something like a per-band feedback
gain or damping applied inside the tank, not a static input/output shelf) whose audible effect is
naturally small when there isn't much low-frequency energy circulating to act on - not evidence
that Low "does nothing," just that its effect is conditional on High rather than independently
additive. This single comparison is suggestive, not conclusive (the sparse grid doesn't have a
second High level with a Low sweep to confirm the trend is monotonic) - worth treating as a
working hypothesis to cross-check once fitting is underway (B8), not a fully closed question.

**Revised recommendation for B4 (supersedes the plan's original "two independent shelves"
guess):** model High as a broadband input/output tilt (reuse `shelf_transfer_function` as
originally planned). Model Low differently - as a low-band-specific multiplier on the per-line
feedback gain (applied *inside* the feedback loop, not at the input/output stage) - which the same
`shelf_transfer_function` primitive can implement by using it as a frequency-dependent feedback
multiplier instead of an output EQ, so no new `core/dsp_primitives.py` code is needed, only a
different composition in `model.py`.

## Method notes (empirical-verification catches)

- The initial read of `echo_density_peak_count`/`echo_density_half_rise_time_s` for
  `Ambience_0.1s_0L0H.wav` (peak count 183, half-rise 0.01s) looked like echo density peaks
  *immediately* - contradicted by the plot, which clearly shows the real density buildup starting
  around t=0.19-0.2s. The 183-peak window turned out to be one of the two isolated early bounces:
  `echo_density()`'s per-window threshold (15% of *that window's own* peak) counts a loud
  transient's own ringing as many separate "echoes" when it dominates a window, inflating the
  count far above the genuinely dense region's ~130-150. This only distorted the half-rise metric
  for very short, sparse-onset captures like this one - the longer captures' half-rise numbers
  (used for the Low/High comparisons above) look consistent with their plots. Not fixed in
  `core/features.py` yet since it didn't affect any conclusion above; flagging here so it isn't
  mistaken for a real "instant echo buildup" finding if reused later, and so a future session
  knows to fix `echo_density()`'s thresholding (e.g. a threshold relative to the whole capture's
  peak, not each window's own) before leaning on echo-density-half-rise for a short-Time capture.
- Confirmed the Low/High findings against the raw audio and stereo channels directly (not just
  `features.json`'s summary numbers) before concluding Low has no onset effect - given how
  surprising "a knob that measurably does nothing" is, checked mono tone (4-band energy), decay
  time, echo density, *and* stereo correlation/side-mid energy before ruling it out, rather than
  stopping at the first metric that came back flat.
