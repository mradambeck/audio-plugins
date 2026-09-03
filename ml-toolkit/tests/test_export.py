import os

from core.export import ExportManifest, read_export, write_cpp_header, write_export
from core.interp import Curve1D


def _sample_manifest():
    return ExportManifest(
        git_commit="abc123",
        capture_set_hash="deadbeef",
        fit_config_hash="cafef00d",
        fit_date="2026-09-01",
        toolkit_version="0.1.0",
    )


def test_write_and_read_export_round_trip(tmp_path):
    curves = {
        "timeToDecayGain": Curve1D([0.1, 1.8, 9.8], [0.5, 0.7, 0.95]),
        "lowToShelfDb": Curve1D([-5, 0, 5], [-4.0, 0.0, 3.5]),
    }
    manifest = _sample_manifest()
    path = str(tmp_path / "params.json")

    write_export(path, curves, manifest)
    loaded_curves, loaded_manifest = read_export(path)

    assert loaded_manifest == manifest
    assert set(loaded_curves.keys()) == set(curves.keys())
    for name, curve in curves.items():
        assert loaded_curves[name].points() == curve.points()


def test_write_cpp_header_produces_valid_looking_output(tmp_path):
    curves = {"timeToDecayGain": Curve1D([0.1, 1.8, 9.8], [0.5, 0.7, 0.95])}
    manifest = _sample_manifest()
    path = str(tmp_path / "AmbienceReferenceData.h")

    write_cpp_header(path, curves, manifest, effect_name="ambience")

    with open(path) as fh:
        content = fh.read()

    assert "#pragma once" in content
    assert "namespace wildjag::dsp" in content
    assert "struct FittedPoint { float x; float y; };" in content
    assert "timeToDecayGainPoints" in content
    assert "static_assert(timeToDecayGainPoints.front().x <= timeToDecayGainPoints.back().x" in content
    assert manifest.git_commit in content
    assert "0.1f" in content and "0.95f" in content
