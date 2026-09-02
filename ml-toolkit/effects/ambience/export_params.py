#!/usr/bin/env python3
"""Phase B9: exports curves.json's Time+High curves via core/export.py, producing
exported/ambience_params.json and exported/AmbienceReferenceData.h, both carrying a provenance
manifest. Stops here per the plan - nothing gets copied into a plugin repo until Phase C, since
which plugin repo consumes it depends on the separate product-identity decision (now resolved:
"Aura", a new standalone plugin - see the plan file).
"""
import json
import os

from core.export import ExportManifest, write_cpp_header, write_export
from core.interp import Curve1D

HERE = os.path.dirname(__file__)
CAPTURES_DIR = os.path.join(HERE, "captures")
CURVES_PATH = os.path.join(HERE, "curves.json")
EXPORT_DIR = os.path.join(HERE, "exported")
REPO_ROOT = os.path.abspath(os.path.join(HERE, "..", "..", ".."))

FIT_CONFIG = {
    "fit_sample_rate": 11025.0,
    "fit_duration_s": 3.5,
    "iters": 600,
    "lr": 0.02,
    "tilt_regularization_weight": 1.0,
}


def main() -> None:
    raw = json.load(open(CURVES_PATH))
    curves = {
        name: Curve1D([p[0] for p in payload["points"]], [p[1] for p in payload["points"]])
        for name, payload in raw.items()
        if name != "_notes"
    }

    capture_paths = [
        os.path.join(CAPTURES_DIR, f) for f in sorted(os.listdir(CAPTURES_DIR)) if f.endswith(".wav")
    ]
    manifest = ExportManifest.build(REPO_ROOT, capture_paths, FIT_CONFIG)

    os.makedirs(EXPORT_DIR, exist_ok=True)
    json_path = os.path.join(EXPORT_DIR, "ambience_params.json")
    header_path = os.path.join(EXPORT_DIR, "AmbienceReferenceData.h")

    write_export(json_path, curves, manifest)
    write_cpp_header(header_path, curves, manifest, effect_name="aura")

    print(f"Wrote {json_path}")
    print(f"Wrote {header_path}")
    print(f"Manifest: {manifest.as_dict()}")
    print()
    print("NOTE: tilt_low_gain/tilt_high_gain are fixed at 1.0 (not exported as curves) and Low's "
          "knob has no curve at all - see curves.json's _notes and findings.md. Both are documented "
          "gaps for Phase C, not silently dropped.")


if __name__ == "__main__":
    main()
