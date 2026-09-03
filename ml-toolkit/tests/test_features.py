"""Regression tests for the two empirical pitfalls documented in core/features.py's docstring -
turning past hard-won catches into permanent tests instead of tribal knowledge, per the plan."""
import numpy as np

from core.features import full_decay_rt60, hilbert_envelope_db, schroeder_rt60


def test_full_decay_rt60_recovers_gated_capture():
    """A short/gated exponential decay (true RT60 known by construction) where the narrow
    -5..-25dB Schroeder window is dominated by an early hold rather than the true decay trend -
    full_decay_rt60's wide -6..-70dB window should recover something close to the true RT60;
    schroeder_rt60 is expected to underestimate it, per intruder-gated-reverb's own finding
    (validation_report.md: schroeder badly underestimated real captures' decay)."""
    sr = 44100
    true_rt60 = 1.5
    tau = true_rt60 / (3 * np.log(10))  # x(t) = exp(-t/tau) has RT60 = -60/(20*log10(e)/tau) = 3*tau*ln(10)
    duration_s = 1.2  # shorter than the true RT60 - a "gated"/truncated capture
    n = int(sr * duration_s)
    t = np.arange(n) / sr

    # A short hold near the peak (~0-10dB flat) before the true exponential decay kicks in -
    # mirrors the "hold near the peak" shape intruder-gated-reverb/analysis/findings.md found in
    # every real capture, which is exactly what skews the narrow-window Schroeder fit.
    hold_s = 0.08
    envelope = np.where(t < hold_s, 1.0, np.exp(-(t - hold_s) / tau))
    rng = np.random.default_rng(0)
    x = envelope * rng.standard_normal(n)

    onset_idx = 0
    sch_rt60, _, _, _, _ = schroeder_rt60(x, sr, onset_idx)
    full_rt60, _, _ = full_decay_rt60(x, sr, onset_idx)

    assert full_rt60 is not None
    # full_decay_rt60 should land reasonably close to the true RT60 despite the truncated/gated
    # capture and the peak hold.
    assert abs(full_rt60 - true_rt60) / true_rt60 < 0.35

    if sch_rt60 is not None:
        # The whole point: the narrow-window fit should underestimate materially more than the
        # wide-window one does, on this specific gated shape.
        assert abs(sch_rt60 - true_rt60) > abs(full_rt60 - true_rt60)


def test_hilbert_envelope_no_wraparound_artifact():
    """A loud onset followed by a tail that decays to true digital silence (not back up to the
    onset level) creates a large implied discontinuity under the Hilbert transform's FFT
    periodicity assumption - without zero-padding, this leaks into a spurious envelope spike near
    the buffer's end (the false "trailing blip" intruder-gated-reverb/analysis/findings.md's
    "Method notes" section describes catching). hilbert_envelope_db pads before transforming;
    an unpadded version should show a materially larger artifact near the end."""
    sr = 44100
    n = 8000
    t = np.arange(n) / sr
    x = np.exp(-t / 0.05) * np.sin(2 * np.pi * 440 * t)  # fast decay to near-silence, no taper to zero at the very end
    x[-1] = x[-2]  # avoid an exact-zero final sample, which would trivially hide the artifact

    from scipy.signal import hilbert as scipy_hilbert

    def db(v, floor=1e-9):
        return 20.0 * np.log10(np.maximum(np.abs(v), floor))

    padded_db = hilbert_envelope_db(x, sr)
    unpadded_db = db(np.abs(scipy_hilbert(x)))

    tail_region = slice(int(n * 0.85), n)
    # The padded version's tail should stay near the noise floor; an unpadded transform's tail
    # spikes well above it due to the wraparound discontinuity.
    padded_tail_peak = padded_db[tail_region].max()
    unpadded_tail_peak = unpadded_db[tail_region].max()
    assert unpadded_tail_peak - padded_tail_peak > 6.0, (
        "expected the unpadded Hilbert transform to show a materially larger tail artifact than "
        "the zero-padded version - if this fails, the synthetic signal may not exercise the "
        "wraparound case; re-check against a signal known to trigger it."
    )
