"""modal_peak_spacing()/modal_overlap()/modal_overlap_crossover_hz() - the measurable substitute
for a "Schroeder frequency" this project uses in place of the room-volume formula (there is no
room; see core/features.py's module comment above these functions and effects/nonlin/findings.md
for the derivation). Validated against a synthetic comb of exactly-known mode spacing, not just
"does it run"."""
import numpy as np

from core.features import modal_overlap, modal_overlap_crossover_hz, modal_peak_spacing

SR = 44100


def test_modal_peak_spacing_recovers_known_comb():
    dur_s = 0.5
    n = int(SR * dur_s)
    t = np.arange(n) / SR
    mode_freqs = np.arange(500, 1500, 50)  # exactly 50Hz apart
    x = sum(np.sin(2 * np.pi * f * t) for f in mode_freqs)

    spacing = modal_peak_spacing(x, SR, (0.0, dur_s), [(400, 1600)])
    assert spacing[(400, 1600)] is not None
    assert abs(spacing[(400, 1600)] - 50.0) < 1.0


def test_modal_peak_spacing_none_when_too_few_bins():
    x = np.zeros(100)
    spacing = modal_peak_spacing(x, SR, (0.0, 100 / SR), [(400, 1600)])
    assert spacing[(400, 1600)] is None


def test_modal_overlap_increases_as_spacing_narrows():
    wide = modal_overlap(1000.0, decay_time_s=0.3, mean_spacing_hz=50.0)
    narrow = modal_overlap(1000.0, decay_time_s=0.3, mean_spacing_hz=5.0)
    assert wide is not None and narrow is not None
    assert narrow > wide


def test_modal_overlap_decreases_with_decay_time():
    """Modal -3dB bandwidth is 2.2/T, so a LONGER decay time means NARROWER, more resolved modes
    and therefore LESS overlap at a fixed mode spacing - a long-ringing system sounds like
    separated tones, a short-ringing one sounds smeared/dense. Initially got this backwards in a
    first draft of this test; verified the direction against the formula before trusting it."""
    short = modal_overlap(1000.0, decay_time_s=0.05, mean_spacing_hz=20.0)
    long = modal_overlap(1000.0, decay_time_s=2.0, mean_spacing_hz=20.0)
    assert short is not None and long is not None
    assert short > long


def test_modal_overlap_none_on_missing_inputs():
    assert modal_overlap(1000.0, decay_time_s=0.3, mean_spacing_hz=None) is None
    assert modal_overlap(1000.0, decay_time_s=None, mean_spacing_hz=20.0) is None
    assert modal_overlap(1000.0, decay_time_s=0.0, mean_spacing_hz=20.0) is None


def test_modal_overlap_crossover_finds_dense_comb():
    # Very tightly-spaced modes (5Hz apart) combined with a SHORT decay time (wide modal
    # bandwidth, per the 2.2/T relation) should read as dense (overlap >= 3) in the checked bands.
    dur_s = 1.0
    n = int(SR * dur_s)
    t = np.arange(n) / SR
    mode_freqs = np.arange(1000, 3000, 5)
    x = sum(np.sin(2 * np.pi * f * t) for f in mode_freqs)
    crossover = modal_overlap_crossover_hz(x, SR, (0.0, dur_s), decay_time_s=0.05,
                                            bands=[(700, 1414), (1414, 2828), (2828, 5657)])
    assert crossover is not None
