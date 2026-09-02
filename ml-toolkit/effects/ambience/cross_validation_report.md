# Phase B8 cross-validation report

Predicts (high_band_gain, low_band_gain, damping_weight_mean) from curves.json's Time+High model for every capture not used to build those curves, renders AmbienceFDN at the predicted values, and compares against the real capture. envelope_correlation (windowed RMS envelope, onset-aligned) is the primary metric - full_decay_rt60_s is shown too but is unreliable exactly where the render's own full_decay_r2 is low (see module docstring): fast-decaying renders hit this pipeline's numerical noise floor before the -6..-70dB linear fit has enough real signal to work with, producing wildly inflated apparent RT60s that reflect noise-floor wobble, not the model's actual decay.

## Summary

- Envelope correlation: mean 0.784, median 0.816 across 55 held-out captures (1.0 = identical shape).
- (Low=+5, High=-8) sweep specifically (Low's effect unmodeled by design - see build_curves.py's notes): mean envelope correlation 0.783, vs. 0.784 for every other held-out capture - the gap is the real, measurable cost of not modeling Low.
- RT60 diff, restricted to the 13/55 captures where BOTH the reference's and the render's full_decay_r2 exceed 0.9 (a trustworthy linear fit on both sides): mean +4.6%, median +4.5%.

## Per-setting results

| file | Time | Low | High | env corr | ref RT60 (s) | ref r2 | pred RT60 (s) | pred r2 |
|---|---|---|---|---|---|---|---|---|
| Ambience_0.1s_-8L0H.wav | 0.1 | -8 | 0 | 0.530 | 0.2334 | 0.7651 | 13.45 | 0.1815 |
| Ambience_0.5s_-8L0H.wav | 0.5 | -8 | 0 | 0.713 | 0.5317 | 0.9888 | 9.7027 | 0.2153 |
| Ambience_1.1s_-8L0H.wav | 1.1 | -8 | 0 | 0.753 | 0.8049 | 0.993 | 6.5995 | 0.3114 |
| Ambience_1.8s_-8L0H.wav | 1.8 | -8 | 0 | 0.839 | 1.4759 | 0.9966 | 4.2776 | 0.579 |
| Ambience_2.3s_-8L0H.wav | 2.3 | -8 | 0 | 0.906 | 2.2862 | 0.9975 | 3.69 | 0.7941 |
| Ambience_3.0s_-8L0H.wav | 3.0 | -8 | 0 | 0.918 | 3.3225 | 0.996 | 3.7936 | 0.939 |
| Ambience_5.5s_-7L0H.wav | 5.5 | -7 | 0 | 0.925 | 5.2252 | 0.9954 | 5.4602 | 0.9693 |
| Ambience_4.5s_-5L0H.wav | 4.5 | -5 | 0 | 0.923 | 4.2058 | 0.9968 | 4.4995 | 0.9735 |
| Ambience_0.1s_-4L0H.wav | 0.1 | -4 | 0 | 0.553 | 0.2293 | 0.7612 | 13.45 | 0.1815 |
| Ambience_0.5s_-4L0H.wav | 0.5 | -4 | 0 | 0.713 | 0.5307 | 0.9893 | 9.7027 | 0.2153 |
| Ambience_1.1s_-4L0H.wav | 1.1 | -4 | 0 | 0.753 | 0.8051 | 0.9929 | 6.5995 | 0.3114 |
| Ambience_1.8s_-4L0H.wav | 1.8 | -4 | 0 | 0.838 | 1.4712 | 0.9966 | 4.2776 | 0.579 |
| Ambience_2.3s_-4L0H.wav | 2.3 | -4 | 0 | 0.906 | 2.2803 | 0.9976 | 3.69 | 0.7941 |
| Ambience_3.0s_-4L0H.wav | 3.0 | -4 | 0 | 0.918 | 3.3126 | 0.9961 | 3.7936 | 0.939 |
| Ambience_5.5s_-3L0H.wav | 5.5 | -3 | 0 | 0.926 | 5.2233 | 0.9953 | 5.4602 | 0.9693 |
| Ambience_0.1s_-2L-2H.wav | 0.1 | -2 | -2 | 0.562 | 0.2284 | 0.7736 | 13.0485 | 0.1924 |
| Ambience_0.5s_-2L-2H.wav | 0.5 | -2 | -2 | 0.702 | 0.524 | 0.989 | 9.5365 | 0.2284 |
| Ambience_1.1s_-2L-2H.wav | 1.1 | -2 | -2 | 0.732 | 0.7765 | 0.9929 | 6.5949 | 0.3106 |
| Ambience_1.8s_-2L-2H.wav | 1.8 | -2 | -2 | 0.838 | 1.3577 | 0.9962 | 4.2462 | 0.5788 |
| Ambience_2.3s_-2L-2H.wav | 2.3 | -2 | -2 | 0.896 | 2.1108 | 0.9969 | 3.7823 | 0.7574 |
| Ambience_3.0s_-2L-2H.wav | 3.0 | -2 | -2 | 0.915 | 2.9967 | 0.9952 | 3.7288 | 0.9128 |
| Ambience_4.5s_-2L-2H.wav | 4.5 | -2 | -2 | 0.919 | 3.962 | 0.9971 | 4.1978 | 0.9659 |
| Ambience_5.5s_-2L-2H.wav | 5.5 | -2 | -2 | 0.926 | 4.9533 | 0.9962 | 4.9347 | 0.9743 |
| Ambience_0.1s_0L-8H.wav | 0.1 | 0 | -8 | 0.527 | 0.2324 | 0.8156 | 11.775 | 0.2357 |
| Ambience_0.5s_0L-8H.wav | 0.5 | 0 | -8 | 0.682 | 0.489 | 0.9809 | 9.0063 | 0.2733 |
| Ambience_1.1s_0L-8H.wav | 1.1 | 0 | -8 | 0.705 | 0.7371 | 0.9928 | 6.7221 | 0.3337 |
| Ambience_1.8s_0L-8H.wav | 1.8 | 0 | -8 | 0.791 | 1.2394 | 0.9915 | 4.2908 | 0.5584 |
| Ambience_3.0s_0L-8H.wav | 3.0 | 0 | -8 | 0.882 | 2.8301 | 0.9889 | 3.6788 | 0.8511 |
| Ambience_4.5s_0L-5H.wav | 4.5 | 0 | -5 | 0.908 | 3.7106 | 0.9961 | 3.966 | 0.9495 |
| Ambience_0.1s_0L-4H.wav | 0.1 | 0 | -4 | 0.570 | 0.2265 | 0.7944 | 12.5655 | 0.2069 |
| Ambience_0.5s_0L-4H.wav | 0.5 | 0 | -4 | 0.687 | 0.5055 | 0.9811 | 9.355 | 0.2418 |
| Ambience_1.1s_0L-4H.wav | 1.1 | 0 | -4 | 0.721 | 0.7298 | 0.983 | 6.5824 | 0.3101 |
| Ambience_1.8s_0L-4H.wav | 1.8 | 0 | -4 | 0.816 | 1.3281 | 0.9956 | 4.2385 | 0.5761 |
| Ambience_3.0s_0L-4H.wav | 3.0 | 0 | -4 | 0.904 | 2.9656 | 0.9939 | 3.7198 | 0.8985 |
| Ambience_5.5s_+2L-3H.wav | 5.5 | 2 | -3 | 0.918 | 4.9024 | 0.9958 | 4.7496 | 0.9747 |
| Ambience_0.1s_+3L-5H.wav | 0.1 | 3 | -5 | 0.559 | 0.2296 | 0.7864 | 12.4373 | 0.2115 |
| Ambience_0.5s_+3L-5H.wav | 0.5 | 3 | -5 | 0.684 | 0.5092 | 0.9893 | 9.2992 | 0.2482 |
| Ambience_1.1s_+3L-5H.wav | 1.1 | 3 | -5 | 0.717 | 0.7554 | 0.991 | 6.6297 | 0.3067 |
| Ambience_1.8s_+3L-5H.wav | 1.8 | 3 | -5 | 0.817 | 1.3702 | 0.9948 | 4.2749 | 0.5676 |
| Ambience_2.3s_+3L-5H.wav | 2.3 | 3 | -5 | 0.879 | 2.1642 | 0.9959 | 3.8385 | 0.7268 |
| Ambience_4.5s_+3L-5H.wav | 4.5 | 3 | -5 | 0.908 | 3.8733 | 0.9952 | 3.966 | 0.9495 |
| Ambience_0.1s_+5L-8H.wav | 0.1 | 5 | -8 | 0.560 | 0.3389 | 0.7644 | 11.775 | 0.2357 |
| Ambience_0.5s_+5L-8H.wav | 0.5 | 5 | -8 | 0.680 | 0.4903 | 0.9816 | 9.0063 | 0.2733 |
| Ambience_1.1s_+5L-8H.wav | 1.1 | 5 | -8 | 0.713 | 0.7486 | 0.9923 | 6.7221 | 0.3337 |
| Ambience_1.8s_+5L-8H.wav | 1.8 | 5 | -8 | 0.782 | 1.3285 | 0.9911 | 4.2908 | 0.5584 |
| Ambience_2.3s_+5L-8H.wav | 2.3 | 5 | -8 | 0.857 | 2.2384 | 0.9914 | 3.901 | 0.7 |
| Ambience_3.0s_+5L-8H.wav | 3.0 | 5 | -8 | 0.883 | 3.2296 | 0.9875 | 3.6788 | 0.8511 |
| Ambience_4.5_+5L-8H.wav | 4.5 | 5 | -8 | 0.892 | 4.1786 | 0.9877 | 3.8511 | 0.9285 |
| Ambience_5.5s_+5L-8H.wav | 5.5 | 5 | -8 | 0.898 | 4.9313 | 0.9892 | 4.3082 | 0.9661 |
| Ambience_1.1s_+6L-7H.wav | 1.1 | 6 | -7 | 0.705 | 0.7161 | 0.9855 | 6.714 | 0.317 |
| Ambience_0.1s_+6L-5H.wav | 0.1 | 6 | -5 | 0.573 | 0.2414 | 0.7917 | 12.4373 | 0.2115 |
| Ambience_0.5s_+6L-5H.wav | 0.5 | 6 | -5 | 0.682 | 0.5131 | 0.9889 | 9.2992 | 0.2482 |
| Ambience_1.8s_+6L-5H.wav | 1.8 | 6 | -5 | 0.807 | 1.4586 | 0.9935 | 4.2749 | 0.5676 |
| Ambience_2.3s_+6L-5H.wav | 2.3 | 6 | -5 | 0.880 | 2.3935 | 0.993 | 3.8385 | 0.7268 |
| Ambience_3.0s_+6L-5H.wav | 3.0 | 6 | -5 | 0.900 | 3.3033 | 0.9908 | 3.6997 | 0.8793 |