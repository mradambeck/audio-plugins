#!/usr/bin/env python3
"""Phase 4 (final step): exports curves.json via core/export.py, producing
exported/nonlin_params.json and exported/InhaltReferenceData.h, both carrying a provenance
manifest. Stops here per Aura's own precedent - nothing gets copied into plugins/inhalt-nonlin/
until the C++ side is being built, and the copy itself is manual (see AGENTS.md's note that no
script automates it, same known rough edge as Aura's export->plugin copy).

FIT_CONFIG below must be kept in sync with fit_nonlin.py's actual constants by hand (mirrors
effects/ambience/export_params.py's own FIT_CONFIG, restated rather than imported, so the
manifest's fit_config_hash reflects what was ACTUALLY run, not just whatever fit_nonlin.py
currently says - re-copy these values here deliberately after any fit_nonlin.py change, the same
way Aura's own restated dict works).
"""
from __future__ import annotations

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
    "fit_sample_rate": 44100.0,
    "fit_duration_s": 1.5,
    "iters": 600,
    "lr": 0.02,
    "mid_side_weight": 1.0,
    "decorrelation_weight": 0.5,
}


def main() -> None:
    raw = json.load(open(CURVES_PATH))

    curves = {}
    constants = {}
    for name, payload in raw.items():
        if name == "_notes":
            continue
        points = payload.get("points", [])
        if len(points) >= 2:
            curves[name] = Curve1D([p[0] for p in points], [p[1] for p in points])
        elif len(points) == 1:
            constants[name] = {"value": points[0][1], "reason": "only one data point - cannot build a curve"}
        else:
            constants[name] = {
                "value": payload.get("default"),
                "reason": "no usable data points - see curves.json's _notes for why",
            }

    capture_paths = [
        os.path.join(CAPTURES_DIR, f) for f in sorted(os.listdir(CAPTURES_DIR)) if f.endswith(".wav")
    ] if os.path.isdir(CAPTURES_DIR) else []
    manifest = ExportManifest.build(REPO_ROOT, capture_paths, FIT_CONFIG)

    os.makedirs(EXPORT_DIR, exist_ok=True)
    json_path = os.path.join(EXPORT_DIR, "nonlin_params.json")
    header_path = os.path.join(EXPORT_DIR, "InhaltReferenceData.h")

    write_export(json_path, curves, manifest)
    # Constants that couldn't become a curve are appended to the JSON export (not the C++ header -
    # core/export.py's write_cpp_header only knows how to emit FittedPoint arrays) so the record
    # of what was and wasn't exportable survives even after curves.json itself is regenerated.
    with open(json_path) as fh:
        payload = json.load(fh)
    payload["constants"] = constants
    with open(json_path, "w") as fh:
        json.dump(payload, fh, indent=2)

    write_cpp_header(header_path, curves, manifest, effect_name="nonlin")

    print(f"Wrote {json_path}")
    print(f"Wrote {header_path}")
    print(f"Manifest: {manifest.as_dict()}")
    if constants:
        print("\nNOTE: the following did not become curves and need a hand-authored constant or a "
              "by-ear default in the plugin (documented here, not silently dropped):")
        for name, info in constants.items():
            print(f"  {name}: value={info['value']!r} - {info['reason']}")


if __name__ == "__main__":
    main()
