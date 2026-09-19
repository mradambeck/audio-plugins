# ml-toolkit

A reusable ML-assisted analysis/fit pipeline for recreating hardware effects units from real
captures. Each `effects/<name>/` module follows the same four-phase shape (capture inventory ->
analysis -> differentiable fit -> curve export), documented in that module's own `findings.md`.
`core/` holds everything effect-agnostic: measurement functions (`features.py`), the fit primitives
(`fit.py`, `dsp_primitives.py`), curve interpolation (`interp.py`), export (`export.py`), I/O
(`io.py`).

Shipped modules: `effects/ambience` (AMS RMX16 Ambience -> the Aura plugin), `effects/nonlin`
(AMS RMX16 NonLin -> the Inhalt plugin).

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
