"""load_audio_channels() - added alongside load_audio() without changing it (see io.py's
docstring: effects/ambience's whole pipeline is keyed to load_audio()'s exact current mono-mix
behaviour)."""
import numpy as np
import soundfile as sf

from core.io import load_audio, load_audio_channels


def test_load_audio_channels_keeps_channels_separate(tmp_path):
    sr = 8000
    n = sr // 10
    l = np.linspace(-0.5, 0.5, n, dtype=np.float32)
    r = np.linspace(0.5, -0.5, n, dtype=np.float32)
    path = tmp_path / "stereo.wav"
    sf.write(path, np.stack([l, r], axis=1), sr)

    data, sr_out = load_audio_channels(str(path))
    assert data.shape == (2, n)
    assert sr_out == sr
    assert data.dtype == np.float64
    np.testing.assert_allclose(data[0], l, atol=1e-4)
    np.testing.assert_allclose(data[1], r, atol=1e-4)


def test_load_audio_channels_mono_file_gives_single_channel(tmp_path):
    sr = 8000
    n = sr // 10
    x = np.linspace(-0.5, 0.5, n, dtype=np.float32)
    path = tmp_path / "mono.wav"
    sf.write(path, x, sr)

    data, sr_out = load_audio_channels(str(path))
    assert data.shape == (1, n)


def test_load_audio_is_unchanged_by_the_new_function(tmp_path):
    """load_audio()'s own behaviour must stay bit-identical to before - effects/ambience's entire
    pipeline is keyed to it. This is a regression guard, not a test of load_audio_channels()."""
    sr = 8000
    n = sr // 10
    l = np.linspace(-0.5, 0.5, n, dtype=np.float32)
    r = np.linspace(0.5, -0.5, n, dtype=np.float32)
    path = tmp_path / "stereo.wav"
    sf.write(path, np.stack([l, r], axis=1), sr)

    mono, sr_out = load_audio(str(path))
    channels, _ = load_audio_channels(str(path))
    np.testing.assert_allclose(mono, channels.mean(axis=0))
