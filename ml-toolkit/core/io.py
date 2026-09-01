"""Capture manifest loading: declarative filename parsing + audio loading, generic across effects.

Filename parsing is schema-driven (a regex + per-group transforms) rather than a bespoke parser
per effect - see intruder-gated-reverb/analysis/inventory.py's hardcoded NAME_RE for the pattern
this generalizes. Different effects' capture filenames (e.g. Ambience's "Ambience_1.8s_+5L-2H.wav"
vs Intruder's "NonLin_9.8s_-4H_Tighter.wav") differ enough in shape that hand-writing a new parser
per effect doesn't scale.
"""
from __future__ import annotations

import os
import re
from dataclasses import dataclass, field
from typing import Callable, Optional

import numpy as np
import soundfile as sf


@dataclass
class FilenameSchema:
    """A compiled regex over capture filenames, plus a value transform per named group.

    `pattern` must use named groups (`(?P<name>...)`) for every parameter encoded in the
    filename. `transforms` maps a subset of those group names to a function applied to the
    raw match text (a string, or None if an optional group didn't match - e.g. a flag suffix
    like Intruder's "_Tighter"). A group without an entry in `transforms` is kept as the raw
    string (or None).
    """

    pattern: str
    transforms: dict[str, Callable[[Optional[str]], object]] = field(default_factory=dict)

    def __post_init__(self) -> None:
        self._compiled = re.compile(self.pattern)

    def parse(self, filename: str) -> Optional[dict]:
        """Returns the parsed parameter dict, or None if filename doesn't match the pattern."""
        match = self._compiled.match(filename)
        if match is None:
            return None
        raw = match.groupdict()
        return {
            name: (self.transforms[name](value) if name in self.transforms else value)
            for name, value in raw.items()
        }


@dataclass
class Capture:
    path: str
    params: dict
    kind: str = "ir"  # "ir" (single impulse response) or "paired" (dry/wet recording)
    dry_path: Optional[str] = None  # only set when kind == "paired"


def load_audio(path: str) -> tuple[np.ndarray, int]:
    """Loads a WAV file, mixes to mono float64. Returns (samples, sample_rate)."""
    data, sr = sf.read(path, always_2d=True)
    return data.mean(axis=1).astype(np.float64), sr


def load_manifest(capture_dir: str, schema: FilenameSchema, kind: str = "ir") -> list[Capture]:
    """Scans capture_dir for .wav files matching schema, returns one Capture per match.

    Files that don't match the schema are skipped with a printed warning rather than raising -
    a capture folder may legitimately contain notes/a README/non-audio files alongside the WAVs.
    """
    captures = []
    for name in sorted(os.listdir(capture_dir)):
        if not name.lower().endswith(".wav"):
            continue
        params = schema.parse(name)
        if params is None:
            print(f"warning: {name!r} did not match the capture filename schema, skipping")
            continue
        captures.append(Capture(path=os.path.join(capture_dir, name), params=params, kind=kind))
    return captures


def print_inventory(captures: list[Capture]) -> None:
    """Prints per-parameter coverage (distinct values seen) and basic consistency checks
    (sample rate, channel count) - generalizes inventory.py's grid-coverage printout to work
    over any schema's parameter set."""
    if not captures:
        print("no captures")
        return

    print(f"{len(captures)} captures")

    all_keys = sorted({k for c in captures for k in c.params})
    for key in all_keys:
        values = sorted({c.params[key] for c in captures if c.params.get(key) is not None}, key=str)
        print(f"  {key}: {len(values)} distinct value(s): {values}")

    sample_rates = set()
    channel_counts = set()
    for c in captures:
        info = sf.info(c.path)
        sample_rates.add(info.samplerate)
        channel_counts.add(info.channels)
    if len(sample_rates) > 1:
        print(f"  WARNING: inconsistent sample rates across captures: {sample_rates}")
    else:
        print(f"  sample rate: {sample_rates.pop()} Hz (consistent)")
    if len(channel_counts) > 1:
        print(f"  note: inconsistent channel counts across captures: {channel_counts}")
    else:
        print(f"  channels: {channel_counts.pop()} (consistent)")
