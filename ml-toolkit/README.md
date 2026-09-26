# ml-toolkit

A reusable ML-assisted analysis/fit pipeline for recreating hardware effects units from real
captures. Each `effects/<name>/` module follows the same four-phase shape (capture inventory ->
analysis -> differentiable fit -> curve export), documented in that module's own `findings.md`.
`core/` holds everything effect-agnostic: measurement functions (`features.py`), the fit primitives
(`fit.py`, `dsp_primitives.py`), curve interpolation (`interp.py`), export (`export.py`), I/O
(`io.py`).

Shipped modules: `effects/ambience` (AMS RMX16 Ambience -> the Aura plugin).

`effects/nonlin` (AMS RMX16 NonLin -> the Inhalt plugin) shipped from here too, but has since moved
to the private `wj-audio-commercial` repo alongside the plugin it feeds (Inhalt is commercially
sold, not part of this public catalog) - history preserved via `git subtree split`, not deleted.
Several lessons below still cite `effects/nonlin/...` and `plugins/inhalt-nonlin/...` paths from
when it lived here; those paths now resolve inside `wj-audio-commercial`, not this repo, but the
lessons themselves remain accurate and worth keeping as cross-module record.

This file is the durable, cross-module record - lessons that cost real debugging time on one
module and apply to the next one too, not effect-specific findings (those stay in each module's
own `findings.md`).

## Before writing `model.py`: architecture questions to settle with real measurements

Both modules built so far picked a plugin architecture (which pieces are independent, which are
shared) early, then discovered - later, and each time from an ear-caught complaint rather than an
analysis-phase check - that a real hardware property the architecture assumed was constant
actually varied with the primary control knob. Settle these with `analyze.py`, not assumption,
before `model.py` gets written:

- **Does stereo decorrelation vary with the primary control?** NonLin's own two channels are
  97-98% correlated at a fixed ~2.5ms lag at short Time settings (essentially mono with a small
  hardware delay) and only modestly correlated (-0.18 to -0.24) at long Time - a genuinely
  Time-dependent width a fixed "two independent tanks" topology cannot reproduce on its own (see
  `effects/nonlin/findings.md` and `plugins/inhalt-nonlin/README.md`'s own "Stereo width" section
  for the full story). **`iacc()`'s own default +-1ms window cannot see a relationship like this at
  all** - it was invisible until `dominant_lag_correlation()` (a wider, ±50ms-by-default search)
  was added specifically because of this bug. Run `dominant_lag_correlation()` across the full
  control range before assuming a shared/independent stereo split holds everywhere; if it doesn't,
  plan for an explicit parameter (see `InhaltIRSynth.h`'s own `Params::stereoNarrowCorrelation` for
  a working pattern: a post-process blend toward a shared delayed-mono reference, not baked into
  the tank/diffuser topology itself) rather than discovering it after the fact.
- **Does per-band knee/attack timing actually match a single shared value?** NonLin's own knee
  time and build-up time were both assumed shared across frequency ("the knee lands at the same
  time in every band") until checked directly - real hardware's own per-band knee time varies by
  tens to over a hundred ms, and build-up time even more. See `effects/nonlin/build_measured_
  gate_curves.py`'s own `_build_per_band_knee_time_curves`/`_KNEE_TIME_PER_BAND_CPP_MEASURED_TARGET`
  comments for the measurement technique (a render-minus-reference cancellation, needed because a
  narrow analysis filter smears a per-band timing measurement with its own group delay/rise time -
  check whether the discrepancy is real signal or a filter artifact by comparing the CURRENT
  render, which has one known-true shared value, against the real capture the same way).
- **Does per-band decay rate match a single shared value?** Same class of question, answered the
  same way for NonLin's plateau droop/fall rate - see that module's own multi-band gate work.
  Worth checking directly for any new module with a gated or frequency-dependent decay character,
  not assumed fine because one broadband rate "looks reasonable" in aggregate.

None of these are exotic checks - they're all a few lines against `core/features.py` functions
that already exist. The cost that actually mattered was not running them until an ear-caught
complaint forced the question.

## Stereo measurement: use `dominant_lag_correlation`/`correlation_at_lag`, not just `iacc()`

`interchannel_correlation()` (zero-lag) and `iacc()` (max over +-1ms) were the only two stereo
correlation checks either module had until NonLin's own stereo-width bug. Both are blind to a real
correlation sitting outside their own search window - which is exactly what NonLin had. Prefer:

- **`dominant_lag_correlation(l, r, sr, max_lag_ms=50.0)`** for open-ended analysis (Phase 2,
  `analyze.py`) - finds whichever lag has the strongest correlation across a wide window and
  reports both the lag and the signed value.
- **`correlation_at_lag(l, r, sr, lag_ms)`** for comparing a render against a reference on a KNOWN
  lag - evaluate both signals at the REFERENCE's own dominant lag, not each side's own independent
  search. `dominant_lag_correlation`'s own argmax can lock onto an unrelated, coincidentally-
  stronger peak elsewhere in a render that has other structure (confirmed on two real NonLin
  renders at extreme negative-High settings) - fixing the lag from the reference and evaluating
  both sides there avoids that failure mode entirely. See `plugins/inhalt-nonlin/analysis/
  validate.py`'s own `compare_one()` for the pattern.

## After any synthesis architecture change, re-run the FULL `validate.py` - not just the metric you were targeting

Parameters that look independent are coupled through the shared measurement pipeline: every
per-band/per-capture number in `validate.py` comes from fitting the SAME swept-breakpoint model to
the SAME rendered envelope, so a change anywhere in that envelope's shape can move the fit's read
on something that had nothing to do with the change.

Two concrete NonLin cases, both real and each found only by running the full suite rather than
spot-checking the target metric:

- **Promoting `buildUpMs` to per-band was implemented in isolation, passed its own diagnostic
  (confirmed real, non-artifact per-band signal), and still measurably broke things**: per-band
  plateau droop error went 66->87dB/s and sign mismatches 13->16, spread across bands that were
  previously fine. Changing the attack shape shifted where the swept-breakpoint fit located each
  band's own "plateau," invalidating droop/knee curves that were calibrated assuming the OLD shared
  attack shape - a coupling effect an isolated before/after check on `buildUpMs` itself could never
  surface. Reverted; see `plugins/inhalt-nonlin/README.md`'s own writeup for the full story.
- **Adding the stereo-narrowing fix measurably worsened per-band decay metrics specifically at the
  Time settings where narrowing was strongest** (mean |droop error| 240 vs. 39 at other settings) -
  not a decay regression at all, but comb filtering: `validate.py`'s per-band gate metrics are
  computed from a MONO downmix (`(L+R)/2`), and correlating two channels with a fixed delay
  introduces real, expected frequency-dependent nulls/peaks in that sum (first null at roughly
  `1/(2*lag)` - ~200Hz for NonLin's own 2.49ms). Confirmed as expected comb filtering rather than a
  broken fit by checking `knee_r2` stayed clean (~0.99) at the affected settings, not just noting
  the metric moved.

The practice this argues for: after ANY synthesis architecture change - a new per-band split, a
promoted parameter, a new correlation/width stage, a crossover redesign - run the complete
`validate.py` suite and read every section, not just the one the change targeted. A metric getting
worse doesn't necessarily mean the change was wrong (see `write_report()`'s own "this aggregate
table is not the final word on anything" standing rule), but it does mean something needs to be
understood and disclosed, not silently accepted or silently ignored as "unrelated."

## Hand-measured overrides and constants go stale silently - tag what they depend on

Several curve-building functions (`_NATURAL_DROOP_PER_BAND_CPP_MEASURED`,
`_KNEE_TIME_PER_BAND_CPP_MEASURED_TARGET`, `_STEREO_NARROW_INPUT_OUTPUT_CPP_MEASURED`, and others)
hardcode a table measured once via a direct C++ render sweep, because the relationship they
capture isn't closed-form (a narrow analysis filter's own smearing, a mixing formula whose
equal-power assumption doesn't quite hold in practice, etc.). That pattern is sound and worth
reusing - the failure mode found this session was a hand-verified override that stayed in the code
UNCHANGED after the architecture it was measured against changed underneath it.

Specifically: `_TARGET_DROOP_ROBUST_OVERRIDE`'s `(2.2, "low")` entry was a real, carefully-verified
fix for genuine mode-beating - under the FIRST, uncompensated crossover. Once the crossover was
redesigned (fixing the render-side mode-beating as a side effect), the override's own justification
no longer held, but nothing re-checked it - it silently kept overriding a now-perfectly-fittable
real measurement with a stale, wrong number, and directly caused a real "way more low end than the
IR's" complaint months of work later. `knee_r2` on the un-overridden data was 0.96+ the whole time;
the override was never revisited to check whether it was still needed.

The fix, worth applying to every such table going forward: comment each hand-measured override
with what it's contingent on (which crossover design, which gate formula, which architecture
version) as precisely as `_KNEE_TIME_PER_BAND_CPP_MEASURED_TARGET`'s own comment already does -
and when ANY of those dependencies change, explicitly re-check every override that names it as a
reason to exist, not just the one the current change is obviously about. A quick `knee_r2` (or
equivalent fit-quality) check on the un-overridden data is a fast way to tell whether an old
override's own justification still holds.

## Level/loudness matching: verify with both steady-state AND transient material

Unit-energy normalization (`normaliseToUnitEnergy` / `IRLibrary::normaliseToUnitEnergy`) guarantees
that two IRs produce the same output RMS for STEADY-STATE input (continuous noise) - that's what
"equal energy" means. It does NOT guarantee equal PERCEIVED loudness for percussive/transient
input if the two IRs' envelope shapes differ (a front-loaded, fast-decaying envelope needs a
higher peak to reach the same total energy as a more evenly-spread one, so a transient hit sounds
different even when both convolve a stationary noise floor identically).

Checked directly for NonLin after a real "convolution sounds much louder" complaint: white-noise
input matched to within ~0.3dB (confirming normalization itself was correct), but a percussive
test transient measured the real capture's output 0.7-6.0dB louder across all 9 settings (mean
+2.3dB) - the gap tracked the still-open per-band decay-rate mismatch, not a normalization bug.
When comparing a synthesized IR's level against a reference (a new module's own Phase 6/`validate.py`
work, or an ad hoc "does this sound right" check), test with BOTH a stationary/noise signal and a
short percussive transient - they can and did disagree meaningfully here, and only the transient
case matched what was actually being heard.

## Every module's own `validate.py` needs a permanent regression guard, not a one-time check

Twice now, a real defect sat fully visible in `validate.py`'s own per-capture output for a while
before anything flagged it in aggregate: NonLin's per-band decay sign mismatches (`band_errors`
carried the data from the start; nothing compared render vs. reference on it until a "huffy
resonance" complaint prompted `_band_sign_mismatches`), and stereo width (the measurement didn't
exist yet, but once added, the same "compute it, don't just eyeball the table" gap would have
repeated without `_stereo_sign_mismatches`/the `CONCERN_THRESHOLDS` entry for it).

The working pattern, now established in `plugins/inhalt-nonlin/analysis/validate.py` and worth
carrying into any future module's own `validate.py` from the start rather than retrofitting after
a complaint:

- An aggregate **magnitude** threshold per metric (`CONCERN_THRESHOLDS` / `BAND_CONCERN_
  THRESHOLDS`) - catches "off by a lot on average."
- A separate **sign-mismatch** check per relevant metric (`_band_sign_mismatches`,
  `_stereo_sign_mismatches`) - catches "backwards, not just imprecise," which a magnitude-only
  mean can hide entirely if positive and negative errors happen to cancel.
- Both computed in ONE shared function (`_compute_all_flags`) called from both the written report
  and the console summary - a real duplication bug (each computing its own copy) let a check exist
  in one but not the other; a single source of truth prevents that class of drift structurally.
- Every new check verified by bug-injection before trusting it: temporarily break the thing it's
  supposed to catch (here, force the fix's own parameter back to neutral), confirm the check fires,
  revert. A check that's never seen its own target bug is unverified, not just untested.
