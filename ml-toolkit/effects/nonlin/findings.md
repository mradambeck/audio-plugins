# Phase 2 findings: what Time and High actually do (AMS RMX16 NonLin)

Based on `effects/nonlin/analyze.py`'s output over the real 9-capture set
(`effects/nonlin/features.json`, `plots/*.png`), plus targeted ad hoc checks run directly against
specific capture pairs where `features.json`'s own fields didn't settle a question on their own.
Deliberately does not reference `plugins/intruder-gated-reverb`'s own analysis of this same
hardware program - every conclusion below is re-derived from these captures.

## Capture set (not a full grid)

9 captures, matching `~/Music/Logic/AMS Verbs/AMS RMX16 NonLin2`'s non-`_Tighter` files (confirmed
identical audio). A cross, not a grid:

| | H=0 | H=-3 | H=-4 | H=-7 | H=-9 |
|---|---|---|---|---|---|
| **T=0.1** | | X | | | |
| **T=0.8** | | X | | | |
| **T=2.2** | X | | | | |
| **T=4.8** | X | | | | |
| **T=7.0** | X | | | X | |
| **T=9.8** | X | | X | | X |

**T=0.1 and T=0.8 are effectively the same setting** - `gate_length_ms_at_20db` measured 107.1ms
and 109.2ms respectively (2ms apart), `build_up_ms` identical (16.3 vs 18.4ms), consistent with the
Time knob's own bottom-of-range compression already suspected before any real capture existed. Only
**five** distinct Time behaviours exist in this set.

## Time: sets the OVERALL envelope timing, and only that

`gate_length_ms_at_20db` per Time (H held at whatever the grid has for that Time - see the
timing-neutrality finding below for why mixing H settings here is valid):

| Time label | gate_length_ms_at_20db | build_up_ms | knee_time_ms |
|---|---|---|---|
| 0.1 | 107.1 | 16.3 | 125.1 |
| 0.8 | 109.2 | 18.4 | 127.1 |
| 2.2 | 159.1 | 18.4 | 137.1 |
| 4.8 | 219.8 | 20.3 | 204.9 |
| 7.0 | 287.8 | 21.4-35.4 | 190.0-287.8 |
| 9.8 | 306.7 | 19.4-35.4 | 212.9-300.7 |

Reproduces the earlier hand-measured table (102.5/102.5/149.3/216.6/278.1/300.4ms) within
`gate_envelope_params()`'s own tighter, RMS-envelope-based methodology - every value landed within
the 10ms tolerance `analyze.py` checks automatically, confirming the measurement code is sound, not
just the hardware behaviour. Same strong bottom-of-range compression as before: 0.1->0.8 moves the
gate length by only 2ms while 7.0->9.8 still moves it by 19ms.

## High: timing-NEUTRAL overall, but NOT damping-neutral - corrects the working hypothesis

This is the one place this phase overturned an assumption carried in from before any real capture
existed (the project plan's "one brief assumption to test" note, based on Intruder's own H
behaviour on the same hardware unit's other program).

**Overall gate length is exactly H-independent.** At Time=7.0, H=-7 and H=0 both measured
`gate_length_ms_at_20db` = **287.78ms**, to two decimal places. At Time=9.8, H=-4, H=-9, and H=0
all measured **306.73ms**, also identical to two decimal places. These are genuinely different
recordings (cross-correlation 0.76-0.94 between the H-varying pairs at the same Time, and different
lengths - not near-duplicate captures), so an exact match at this precision is strong, direct
confirmation: **H does not move when the gate closes**, at least not at a resolution this
measurement can see.

**But the INTERNAL shape between onset and that fixed endpoint does depend on H.**
`plateau_droop_db_per_s` at Time=7.0: H=-7 -> **-52.6dB/s**, H=0 -> **-4.3dB/s** - roughly 12x
different. At Time=9.8: H=-4 -> -45.1dB/s, H=-9 -> -43.6dB/s, H=0 -> -10.3dB/s - again a ~4x
difference between H=0 and the negative H values, with the negative-H effect itself saturating
quickly (H=-4 and H=-9 are nearly identical, unlike a linear continuation). **Revised
interpretation**: High redistributes how energy is lost between the "plateau" and "post-knee fall"
segments while conserving the total time to reach -20dB - not the "no damping effect, purely tonal"
reading the pre-capture timing-neutrality argument suggested. `fall_rate_db_per_s` moves much less
across the same H range (-141 to -187dB/s, no consistent H-linked pattern) - the redistribution is
concentrated in the plateau segment specifically, not spread evenly across the whole decay.

**Model implication**: `plateau_droop_db_per_s` should be fit as a function of BOTH Time and High
(not Time-only, as `effects/nonlin/model.py`'s docstring originally proposed pending this check) -
see `build_curves.py`'s own H-timing-neutrality check, which correctly found the *overall* timing
(`t_knee_ms`) pools cleanly across H, while droop specifically should not be pooled the same way.

## High: broadband tilt, onset breakdown re-measured on this exact capture set

5-band onset energy (20-120 / 120-500 / 500-2k / 2k-6k / 6k-16kHz, dB, mean-subtracted per
capture), Time=9.8, H swept:

| High | 20-120Hz | 120-500Hz | 500-2kHz | 2-6kHz | 6-16kHz |
|---|---|---|---|---|---|
| 0 | -12.17 | -5.38 | 1.54 | 5.90 | 10.11 |
| -4 | -9.99 | -3.23 | 3.32 | 5.33 | 4.57 |
| -9 | -7.68 | -0.98 | 4.50 | 3.51 | 0.65 |

H=0 -> H=-9: bass rises **+4.49dB**, treble falls **-9.46dB** - matches the pre-capture estimate
(+4.2/-9.1dB) closely, confirming that estimate (made on this same underlying audio before the
toolkit existed) was sound. `core.features.tilt_fit()` on the full LTAS (H=-9 vs H=0) found a
**+2.25dB gain, -1.58dB/octave slope, zero-crossing at ~4044Hz** - notably higher than the ~1-2kHz
pivot assumed earlier. The discrepancy is expected: `tilt_fit` here fits the WHOLE-capture LTAS
(dominated by the long quiet tail, where the gate's own fall may itself carry frequency-dependent
content), not just the onset. **Use ~1.5-2kHz for the onset-tilt pivot** (matching the 500Hz-2kHz
band already showing only a small +2.96dB rise while 2-6kHz starts falling) since that is what
model.py's input-stage shelf actually needs to match; treat 4044Hz as describing the *tail's* tilt
character instead, a separate, not-yet-modelled question.

## Diffusion: the plateau begins BEFORE the tank is fully dense - a real, if soft, architecture caveat

`normalized_echo_density` at the moment the plateau nominally begins (`ned_at_plateau_start`)
ranges from **0.30 to 0.59** across all 9 captures - well below 1.0 (fully diffuse/Gaussian-like).
The whole-capture mean NED, by contrast, ranges 0.77-1.43, some settings comfortably above 1.0.
**Reading**: the tank has NOT finished building density by the time the amplitude envelope's own
"build-up" phase completes; density continues to increase gradually through the early plateau
region rather than being already-complete when the plateau nominally starts. This is short of the
plan's stated failure condition ("if NED is still climbing through the WHOLE plateau, gate-on-FDN is
the wrong topology") - NED does reach and exceed 1.0 well before the gate closes in every capture -
but it's not the clean "instantly dense, then gated" picture either. **Model implication**: don't
expect a fixed, ~3ms build-up to fully explain the onset; the tank's own density ramp (governed by
feedback gain/line count, not the explicit gate parameters) contributes to the perceived
"build-up" shape too, and calibration should check the RENDERED NED trajectory against this same
curve, not just the amplitude envelope, once real fitting is done.

## Modal overlap crossover: not resolvable with this capture set/method

`modal_overlap_crossover_hz` returned `None` for all 9 captures - the peak-spacing measurement
inside the plateau window never found modal overlap M>=3 in any checked octave band. Likely cause:
these captures are short (0.3-1.0s total) and the "quasi-stationary plateau window" available for
an FFT is correspondingly short, giving too little frequency resolution to resolve genuine spectral
peaks distinct from noise - `core.features.modal_peak_spacing()`'s own `mask.sum() < 8` guard is
probably firing for most bands. **Not treated as "the response has no modal structure"** - it's a
measurement-resolution gap, documented rather than papered over with a fabricated crossover
frequency. Revisit with a longer analysis window (the longest captures, T=9.8, are the best
candidates) if this measurement is needed later.

## Resonant peaks: not trustworthy from a whole-capture LTAS

`resonant_peaks()` (via `long_term_average_spectrum` over the WHOLE capture) returned peaks
sitting around -58 to -67dB - deep in what is almost certainly noise-floor ripple, since the
whole-capture average is dominated by the long quiet tail after the gate closes, not the loud
early portion where any real resonant character would actually be audible. **Not reported as real
tonal peaks.** A trustworthy version of this measurement would need to run
`long_term_average_spectrum` over just the loud (build-up + plateau) window, not the whole capture
- a refinement for later, not attempted here to avoid inventing peaks the current method can't
actually support.

## Stereo: genuinely decorrelated, confirmed on the real capture set

| Time | High | correlation | IACC | mid/side dB |
|---|---|---|---|---|
| 0.1 | -3 | -0.005 | 0.006 | 0.05 |
| 0.8 | -3 | -0.005 | 0.006 | 0.05 |
| 2.2 | 0 | 0.002 | 0.021 | -0.01 |
| 4.8 | 0 | 0.001 | 0.039 | -0.01 |
| 7.0 | -7 | -0.001 | 0.012 | 0.00 |
| 7.0 | 0 | 0.001 | 0.037 | -0.01 |
| 9.8 | -4 | -0.001 | 0.016 | 0.01 |
| 9.8 | -9 | -0.000 | 0.009 | 0.00 |
| 9.8 | 0 | 0.007 | 0.036 | -0.06 |

Every setting: correlation ~0.00, IACC well under 0.04 (the stronger test - see the project plan on
why zero-lag correlation alone isn't sufficient), mid/side within 0.06dB of 0dB. Confirms the two
channels are genuinely independent networks, not a delayed-but-correlated stereo trick, across the
WHOLE real capture set (not just the two files spot-checked before the toolkit existed).
`InhaltIRSynth`'s two fully independent 8-line tanks (already built and unit-tested) are the right
design; nothing here calls that into question.

## Summary for the model

- Time drives the gate's overall timing (build-up scale, knee position, final gate length) -
  additive/Time-only, and the H-timing-neutrality check in `build_curves.py` confirms pooling
  across H is safe for this specific measurement.
- High drives an onset tilt (~1.5-2kHz pivot, +4.5dB low/-9.5dB high at the H range's negative
  end) AND the plateau's droop rate - NOT timing-neutral for damping, only for the overall gate
  length. Needs `plateau_droop_db_per_s` fit against both Time and High, or at minimum against High
  at each Time setting separately (only 2-3 Time settings have an H sweep, so a full 2D surface
  isn't supportable - see `build_curves.py`'s own notes for the honest reading of what's fittable).
- Stereo decorrelation is total and Time/High-independent - no curve needed, the two-tank
  architecture with fixed disjoint delay sets already reproduces this by construction.
- Diffusion builds gradually through the plateau rather than being instantaneous - worth checking
  against the RENDERED model's own NED trajectory during calibration, not just its amplitude
  envelope.
- Modal-overlap crossover and whole-capture resonant peaks are both flagged as not resolvable with
  this capture set/method - real gaps, not filled with invented numbers.
