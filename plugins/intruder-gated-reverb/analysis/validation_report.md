# Phase 6 validation report

Plugin renders (via IntruderRenderIR) vs. the 19 reference captures in `ir-captures/`, mapping each reference's Decay to its own measured RT60, H directly to Tilt, and Tighter to 0/100%.

Metrics: envelope correlation and log-spectral distance / per-band EQ balance are reused from `../../common/tools/compare_wavs.py`. Crest factor (compression/dynamics) and spectral flatness (harmonic/resonant character) are new, added per Adam's request not to validate on amplitude alone.

## Summary (averaged across all 19 settings)

- Envelope correlation: 0.821 (1.0 = identical shape)
- Log-spectral distance: 3.82 dB (lower = closer EQ match)
- Crest factor diff (plugin - reference): -0.52 dB (negative = plugin sounds more compressed/squashed than the real hardware; positive = plugin is peakier/less dense than the reference)
- Spectral flatness diff (plugin - reference): -16.06 dB (negative = plugin tail is more tonal/comb-filtered than the reference's diffuse character; near 0 = matched)

## Status

Tuning history and known limitations - read this before assuming a metric above looks like a regression you introduced.

- **`tankSustainMultiplier` (in `IntruderFDNEngine::updateFeedbackGains()`) is the dominant lever on tail resonance/flatness.** Sweeping it from 4.0x down to 1.3x improved average spectral flatness by ~19dB - more than either adding delay-line modulation (Wobble) or doubling the line count did on their own. The remaining -16.06dB average flatness gap to the reference hardware is a known limit, not an unexplained regression: an FDN tank is fundamentally more comb-filtered/resonant than a real echo-dense mechanical/analog reverb, and closing the rest of that gap would need a much denser network (more lines, or a different topology) than this plugin currently uses.
- **2026-08-28: removed the discrete early-reflection tap system entirely** (`EarlyReflectionTap`/`erBuffer` previously summed a fixed 9-tap pattern, matching findings.md's measured ER taps, directly into the wet output alongside the tank). It measurement-accurately matched the real hardware's onset taps, but on a direct listen it read as audible slapback echoes of the source rather than reverb - removed rather than turned down. Re-running this validation script after the removal produced numbers identical to the pre-removal run to the displayed precision, on every one of the 19 settings - these aggregate metrics are computed over the full multi-second decay, so a contribution confined to the first ~130ms is too small a fraction of total signal energy to move them. The removal is real and verified, just not by these particular numbers: a direct 5ms-windowed RMS comparison of the onset region (0-180ms) shows the old render's sharp isolated bump at 35-50ms (tap energy stacked on the tank) is gone in the new render, which now rises and falls as one smooth curve; everything past ~55ms is unchanged, consistent with the tank's own generation being untouched by this change.

## Per-setting results

| file | schroeder RT60 (s) | full-decay RT60 (s, used as Decay) | env corr | LSD (dB) | EQ low/mid/high (dB) | crest diff (dB) | flatness diff (dB) |
|---|---|---|---|---|---|---|---|
| NonLin_0.1s_-3H.wav | 0.1918 | 0.3352 | 0.896 | 4.41 | +4.1 / +2.5 / +4.8 | -0.51 | -4.98 |
| NonLin_0.1s_-3H_1_Tighter.wav | 0.1907 | 0.3242 | 0.932 | 9.67 | +9.8 / +8.1 / +11.4 | 0.02 | 0.45 |
| NonLin_0.1s_-3H_Tighter.wav | 0.1908 | 0.3286 | 0.927 | 9.67 | +9.9 / +8.1 / +11.2 | -0.1 | 0.1 |
| NonLin_0.8s_-3H.wav | 0.1918 | 0.3443 | 0.888 | 5.00 | +4.9 / +3.3 / +5.7 | -0.95 | -6.49 |
| NonLin_0.8s_-3H_1_Tighter.wav | 0.1912 | 0.3257 | 0.937 | 6.96 | +6.8 / +5.6 / +8.3 | 0.23 | -3.23 |
| NonLin_2.2s_0H.wav | 0.2285 | 0.4098 | 0.864 | 2.60 | +3.1 / -0.1 / +2.2 | -2.24 | -26.46 |
| NonLin_2.2s_0H_Tighter.wav | 0.2265 | 0.388 | 0.992 | 4.99 | +6.0 / +2.4 / +5.3 | -1.95 | -21.3 |
| NonLin_4.8s_0H.wav | 0.3209 | 0.4752 | 0.705 | 2.20 | +2.7 / -1.0 / +0.7 | -3.42 | -35.64 |
| NonLin_4.8s_0H_Tighter.wav | 0.3152 | 0.3669 | 0.877 | 2.25 | +1.7 / -0.7 / +1.4 | -2.22 | -22.08 |
| NonLin_7.0s_-7H.wav | 0.4205 | 0.4682 | 0.692 | 1.83 | -0.3 / -2.1 / +0.2 | -0.52 | -23.28 |
| NonLin_7.0s_-7H_Tighter.wav | 0.4118 | 0.3994 | 0.841 | 2.14 | +0.0 / -1.5 / +1.4 | 0.36 | -20.27 |
| NonLin_7.0s_0H.wav | 0.432 | 0.4231 | 0.647 | 2.70 | -1.7 / -3.8 / -2.1 | -1.02 | -27.72 |
| NonLin_7.0s_0H_Tighter.wav | 0.4134 | 0.3463 | 0.821 | 3.30 | -2.8 / -4.0 / -2.0 | 0.26 | -13.1 |
| NonLin_9.8s_-4H.wav | 0.4899 | 0.4583 | 0.684 | 2.17 | -0.1 / -2.8 / -1.5 | -0.08 | -22.08 |
| NonLin_9.8s_-4H_Tighter.wav | 0.4504 | 0.3634 | 0.839 | 2.60 | -0.9 / -2.8 / -0.8 | 1.3 | -3.36 |
| NonLin_9.8s_-9H.wav | 0.4923 | 0.4908 | 0.702 | 1.93 | -1.4 / -2.4 / +0.4 | 0.26 | -21.82 |
| NonLin_9.8s_-9H_Tighter.wav | 0.4551 | 0.4262 | 0.880 | 2.05 | -0.6 / -1.3 / +2.2 | 1.47 | -18.31 |
| NonLin_9.8s_0H.wav | 0.4879 | 0.4311 | 0.635 | 2.71 | -1.7 / -3.8 / -2.2 | -1.13 | -24.77 |
| NonLin_9.8s_0H_Tighter.wav | 0.4455 | 0.349 | 0.832 | 3.35 | -3.0 / -4.0 / -2.0 | 0.38 | -10.81 |
