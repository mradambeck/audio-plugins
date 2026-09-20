"""Filename schema for the AMS RMX16 "NonLin" IR captures.

Filenames look like "NonLin_9.8s_-4H.wav" - the hardware's own Time/High knob settings, matching
the convention Adam has already used for this same hardware program's captures (e.g. the earlier,
now-untracked plugins/intruder-gated-reverb/ir-captures/). Deliberately NOT reusing anything from
that plugin's analysis code - see effects/nonlin/findings.md and the top-level project instruction
to start fresh on this program's DSP.

The "s" suffix on time is optional, mirroring effects/ambience/capture_schema.py's tolerance for a
one-off missing suffix - keep that same leniency here rather than assuming NonLin's captures will
never have the same slip.
"""
from core.io import FilenameSchema

NONLIN_SCHEMA = FilenameSchema(
    pattern=r"^NonLin_(?P<time>[\d.]+)s?_(?P<high>[+-]?\d+)H\.wav$",
    transforms={
        "time": float,
        "high": int,
    },
)
