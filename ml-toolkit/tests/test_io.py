import numpy as np
import soundfile as sf

from core.io import Capture, FilenameSchema, load_audio, load_manifest


def test_filename_schema_parses_and_transforms():
    schema = FilenameSchema(
        pattern=r"^Ambience_(?P<time>[\d.]+)s_(?P<low>[+-]\d+)L(?P<high>[+-]\d+)H\.wav$",
        transforms={"time": float, "low": int, "high": int},
    )
    parsed = schema.parse("Ambience_1.8s_+5L-2H.wav")
    assert parsed == {"time": 1.8, "low": 5, "high": -2}


def test_filename_schema_no_match_returns_none():
    schema = FilenameSchema(pattern=r"^Ambience_(?P<time>[\d.]+)s\.wav$")
    assert schema.parse("NotAmbience.wav") is None


def test_filename_schema_optional_group():
    schema = FilenameSchema(
        pattern=r"^NonLin_(?P<h>-?\d+)H(?:_(?P<tighter>Tighter))?\.wav$",
        transforms={"h": int, "tighter": lambda v: v is not None},
    )
    assert schema.parse("NonLin_-3H.wav") == {"h": -3, "tighter": False}
    assert schema.parse("NonLin_-3H_Tighter.wav") == {"h": -3, "tighter": True}


def test_load_manifest_skips_non_matching_files(tmp_path):
    schema = FilenameSchema(pattern=r"^Ambience_(?P<time>[\d.]+)s\.wav$", transforms={"time": float})
    sr = 8000
    x = np.zeros(sr // 10, dtype=np.float32)
    sf.write(tmp_path / "Ambience_1.8s.wav", x, sr)
    sf.write(tmp_path / "Ambience_2.2s.wav", x, sr)
    (tmp_path / "notes.txt").write_text("not audio")

    captures = load_manifest(str(tmp_path), schema)
    assert len(captures) == 2
    assert all(isinstance(c, Capture) for c in captures)
    assert {c.params["time"] for c in captures} == {1.8, 2.2}
    assert all(c.kind == "ir" for c in captures)


def test_load_audio_mixes_to_mono():
    import tempfile
    import os

    sr = 8000
    n = 100
    stereo = np.stack([np.ones(n), np.zeros(n)], axis=1).astype(np.float32)
    with tempfile.TemporaryDirectory() as d:
        path = os.path.join(d, "test.wav")
        sf.write(path, stereo, sr)
        x, loaded_sr = load_audio(path)
        assert loaded_sr == sr
        assert x.ndim == 1
        assert np.allclose(x, 0.5, atol=1e-3)
