#!/usr/bin/env python3
"""Phase 0: parse ir-captures/ filenames into a table, check which decay/H/tighter
combinations exist, and confirm sample rate/bit depth/length are consistent.

Filename pattern: NonLin_<decay>s_<H>H[_<n>]?[_Tighter]?.wav
"""
import json
import os
import re
import sys

import soundfile as sf

IR_DIR = os.path.join(os.path.dirname(__file__), "..", "ir-captures")
OUT_PATH = os.path.join(os.path.dirname(__file__), "inventory.json")

NAME_RE = re.compile(
    r"^NonLin_(?P<decay>[\d.]+)s_(?P<h>-?\d+)H(?:_(?P<variant>\d+))?(?:_(?P<tighter>Tighter))?\.wav$"
)


def parse_filename(name):
    m = NAME_RE.match(name)
    if not m:
        raise ValueError(f"filename doesn't match expected pattern: {name}")
    return {
        "filename": name,
        "decay_label_s": float(m.group("decay")),
        "h_db": int(m.group("h")),
        "variant": int(m.group("variant")) if m.group("variant") else None,
        "tighter": m.group("tighter") is not None,
    }


def main():
    files = sorted(f for f in os.listdir(IR_DIR) if f.endswith(".wav"))
    rows = []
    for f in files:
        row = parse_filename(f)
        info = sf.info(os.path.join(IR_DIR, f))
        row.update(
            {
                "samplerate": info.samplerate,
                "channels": info.channels,
                "subtype": info.subtype,
                "duration_s": round(info.frames / info.samplerate, 3),
                "frames": info.frames,
            }
        )
        rows.append(row)

    samplerates = {r["samplerate"] for r in rows}
    subtypes = {r["subtype"] for r in rows}
    channels = {r["channels"] for r in rows}

    print(f"{len(rows)} IR files parsed\n")
    header = f"{'filename':<32} {'decay':>6} {'H':>4} {'var':>4} {'tighter':>8} {'sr':>7} {'ch':>3} {'subtype':>10} {'dur_s':>7}"
    print(header)
    print("-" * len(header))
    for r in rows:
        print(
            f"{r['filename']:<32} {r['decay_label_s']:>6} {r['h_db']:>4} "
            f"{str(r['variant'] or ''):>4} {str(r['tighter']):>8} {r['samplerate']:>7} "
            f"{r['channels']:>3} {r['subtype']:>10} {r['duration_s']:>7}"
        )

    print(f"\nSample rates present: {samplerates}")
    print(f"Subtypes present: {subtypes}")
    print(f"Channel counts present: {channels}")

    decays = sorted({r["decay_label_s"] for r in rows})
    hs = sorted({r["h_db"] for r in rows})
    print(f"\nDecay labels: {decays}")
    print(f"H values: {hs}")

    print("\nCombination grid (decay x H), T=has non-Tighter, t=has Tighter, .=missing:")
    corner = "decay\\H"
    print(f"{corner:>8}" + "".join(f"{h:>6}" for h in hs))
    for d in decays:
        cells = []
        for h in hs:
            matches = [r for r in rows if r["decay_label_s"] == d and r["h_db"] == h]
            has_plain = any(not r["tighter"] for r in matches)
            has_tight = any(r["tighter"] for r in matches)
            cell = ("T" if has_plain else ".") + ("t" if has_tight else ".")
            cells.append(cell)
        print(f"{d:>8}" + "".join(f"{c:>6}" for c in cells))

    with open(OUT_PATH, "w") as fh:
        json.dump(
            {
                "files": rows,
                "samplerates": sorted(samplerates),
                "subtypes": sorted(subtypes),
                "channels": sorted(channels),
                "decay_labels": decays,
                "h_values": hs,
            },
            fh,
            indent=2,
        )
    print(f"\nWrote {OUT_PATH}")

    if len(samplerates) > 1 or len(subtypes) > 1 or len(channels) > 1:
        print("\nWARNING: inconsistent format across files - see above.", file=sys.stderr)


if __name__ == "__main__":
    main()
