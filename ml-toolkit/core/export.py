"""Writes fitted parameter curves to a portable format the JUCE side can consume.

Two output forms:
  - write_export(): JSON, for inspection/tooling and as the canonical record of a fit.
  - write_cpp_header(): a self-contained C++ header of raw {x, y} reference-point arrays, styled
    directly on plugins/intruder-gated-reverb/Source/IntruderReferenceData.h's "single source of truth"
    convention (including its static_assert on sort order) - deliberately just data, with no
    dependency on common/dsp/FittedCurve1D.h or any plugin's ParameterMap. A plugin's own
    `<Name>ParameterMap.cpp` (Phase C) is the thin adapter that turns these raw points into
    FittedCurve1D instances, mirroring how IntruderParameterMap.cpp reads IntruderReferenceData.h.

Every export carries an ExportManifest (git commit, capture-set hash, fit date, toolkit version,
fit-config hash) so a fitted table is traceable back to what produced it - this repo's version
discipline (a single project(...VERSION...) source of truth per plugin, a CI version-guardrail)
has no equivalent yet for *fitted data*, and a "why does this sound different" question needs the
same kind of answer a `git blame` on a CMakeLists.txt version bump already gives.
"""
from __future__ import annotations

import hashlib
import json
import subprocess
from dataclasses import asdict, dataclass
from datetime import date, datetime, timezone
from importlib.metadata import PackageNotFoundError, version as _pkg_version

from core.interp import Curve1D


def get_git_commit(repo_root: str) -> str:
    """Returns the current HEAD commit hash of repo_root, or 'unknown' if that fails (e.g. no
    git available, or repo_root isn't inside a git checkout) - provenance should degrade
    gracefully rather than block an export."""
    try:
        out = subprocess.run(
            ["git", "rev-parse", "HEAD"], cwd=repo_root, capture_output=True, text=True, check=True
        )
        return out.stdout.strip()
    except Exception:
        return "unknown"


def hash_capture_set(capture_paths: list[str]) -> str:
    """A hash over each capture file's name + content, so a manifest can tell whether the exact
    set of captures a fit used has changed (added/removed/re-recorded) since it was produced."""
    hasher = hashlib.sha256()
    for path in sorted(capture_paths):
        hasher.update(path.encode("utf-8"))
        with open(path, "rb") as fh:
            hasher.update(fh.read())
    return hasher.hexdigest()[:16]


def hash_fit_config(config: dict) -> str:
    """Hash of whatever fit configuration (loss weights, iteration count, learning rate, ...) a
    caller wants stamped into the manifest - the exact contents are up to the caller."""
    encoded = json.dumps(config, sort_keys=True).encode("utf-8")
    return hashlib.sha256(encoded).hexdigest()[:16]


def _toolkit_version() -> str:
    try:
        return _pkg_version("ml-toolkit")
    except PackageNotFoundError:
        return "unknown"


@dataclass
class ExportManifest:
    git_commit: str
    capture_set_hash: str
    fit_config_hash: str
    fit_date: str
    toolkit_version: str

    @classmethod
    def build(cls, repo_root: str, capture_paths: list[str], fit_config: dict) -> "ExportManifest":
        return cls(
            git_commit=get_git_commit(repo_root),
            capture_set_hash=hash_capture_set(capture_paths),
            fit_config_hash=hash_fit_config(fit_config),
            fit_date=datetime.now(timezone.utc).date().isoformat(),
            toolkit_version=_toolkit_version(),
        )

    def as_dict(self) -> dict:
        return asdict(self)

    def as_comment_lines(self) -> list[str]:
        return [
            f"//   git commit:         {self.git_commit}",
            f"//   capture set hash:   {self.capture_set_hash}",
            f"//   fit config hash:    {self.fit_config_hash}",
            f"//   fit date:           {self.fit_date}",
            f"//   ml-toolkit version: {self.toolkit_version}",
        ]


def write_export(path: str, curves: dict[str, Curve1D], manifest: ExportManifest) -> None:
    """Writes curves + manifest as JSON."""
    payload = {
        "manifest": manifest.as_dict(),
        "curves": {name: curve.points() for name, curve in curves.items()},
    }
    with open(path, "w") as fh:
        json.dump(payload, fh, indent=2)


def read_export(path: str) -> tuple[dict[str, Curve1D], ExportManifest]:
    """Inverse of write_export() - used by its own round-trip test and by cross_validate.py."""
    with open(path) as fh:
        payload = json.load(fh)
    manifest = ExportManifest(**payload["manifest"])
    curves = {
        name: Curve1D([p[0] for p in points], [p[1] for p in points])
        for name, points in payload["curves"].items()
    }
    return curves, manifest


def _sanitize_identifier(name: str) -> str:
    return "".join(c if (c.isalnum() or c == "_") else "_" for c in name)


def write_cpp_header(path: str, curves: dict[str, Curve1D], manifest: ExportManifest, effect_name: str, namespace: str = "wildjag::dsp") -> None:
    """Generates a self-contained C++ header of raw {x, y} reference points, one
    `constexpr std::array<FittedPoint, N>` per curve, styled on IntruderReferenceData.h. Not
    hand-edited - regenerate via the effect's export_params.py instead."""
    lines: list[str] = []
    lines.append("#pragma once")
    lines.append("")
    lines.append(f"// AUTO-GENERATED by ml-toolkit/core/export.py ({date.today().isoformat()}) - do not hand-edit.")
    lines.append(f"// Regenerate via effects/{effect_name}/export_params.py. Single source of truth for")
    lines.append(f"// {effect_name}'s fitted-parameter reference points - if these are ever re-measured or")
    lines.append("// re-fit, update the captures/fit config and regenerate, not this file directly.")
    lines.append("//")
    lines.append("// Provenance:")
    lines.extend(manifest.as_comment_lines())
    lines.append("")
    lines.append("#include <array>")
    lines.append("")
    lines.append(f"namespace {namespace}")
    lines.append("{")
    lines.append("")
    lines.append("struct FittedPoint { float x; float y; };")
    lines.append("")

    for name, curve in curves.items():
        array_name = _sanitize_identifier(name) + "Points"
        n = len(curve.xs)
        lines.append(f"// {name}")
        lines.append(f"static constexpr std::array<FittedPoint, {n}> {array_name} {{ {{")
        for x, y in curve.points():
            lines.append(f"    {{ {x!r}f, {y!r}f }},")
        lines.append("} };")
        lines.append(
            f"static_assert({array_name}.front().x <= {array_name}.back().x,\n"
            f'    "{array_name} must be sorted ascending by x - FittedCurve1D assumes this ordering");'
        )
        lines.append("")

    lines.append(f"}} // namespace {namespace}")
    lines.append("")

    with open(path, "w") as fh:
        fh.write("\n".join(lines))
