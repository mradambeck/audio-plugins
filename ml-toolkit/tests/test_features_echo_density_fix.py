"""echo_density()'s threshold_ref="global" fix, kept opt-in (default stays "window", the buggy
original behaviour) because effects/ambience's features.json/findings.md/cross_validation_report.md
are keyed to the default. See the function's own docstring for the bug."""
import numpy as np

from core.features import echo_density

SR = 44100


def test_window_threshold_default_is_unchanged():
    """Regression guard: the default threshold_ref must stay "window" so Ambience's numbers don't
    silently move if this function is touched again later."""
    import inspect

    assert inspect.signature(echo_density).parameters["threshold_ref"].default == "window"


def test_global_threshold_is_stable_across_a_loud_then_quiet_signal():
    """The bug: a "window" threshold re-normalizes to each window's own peak, so a LOUD sparse
    window and a QUIET sparse window can report similar counts even though the second one has far
    fewer samples that would count as "a real echo" against the signal's true, global peak. Build
    a signal with one huge early spike (setting a high global peak) and then quiet, evenly sparse
    low-level impulses - "global" thresholding should report far fewer late-window peaks than
    "window" thresholding does for the same signal, because the small late impulses no longer
    clear a threshold set from the whole signal's loud onset."""
    n = SR // 2
    x = np.zeros(n)
    x[100] = 1.0  # the one loud, global-peak-setting spike
    # quiet, evenly spaced small impulses far below the global peak but well above only their
    # own tiny local window peak
    quiet_positions = np.arange(20000, n, 400)
    x[quiet_positions] = 0.02

    onset = 0
    _, counts_window = echo_density(x, SR, onset, threshold_ref="window")
    _, counts_global = echo_density(x, SR, onset, threshold_ref="global")

    # Restrict to the quiet region (skip the loud spike's own window).
    late_window = counts_window[counts_window.size // 4 :]
    late_global = counts_global[counts_global.size // 4 :]

    assert late_window.sum() > 0  # the bug: each quiet window's own local peak clears its own threshold
    assert late_global.sum() == 0  # the fix: none of the quiet impulses clear the GLOBAL threshold
