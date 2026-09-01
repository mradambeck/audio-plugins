"""Filename schema for the 65 AMS RMX16 "Ambience" IR captures.

Filenames look like "Ambience_1.8s_+5L-2H.wav" - the hardware's own Time/Low/High knob settings,
per ml-toolkit-plan.md's background note. One file in the actual delivered set
(Ambience_4.5_+5L-8H.wav) is missing the trailing "s" on its time label - almost certainly a
one-off naming slip during capture, not a second convention - so the time group's "s" suffix is
optional here rather than being treated as a second, deliberate filename shape.
"""
from core.io import FilenameSchema

AMBIENCE_SCHEMA = FilenameSchema(
    pattern=r"^Ambience_(?P<time>[\d.]+)s?_(?P<low>[+-]?\d+)L(?P<high>[+-]?\d+)H\.wav$",
    transforms={
        "time": float,
        "low": int,
        "high": int,
    },
)
