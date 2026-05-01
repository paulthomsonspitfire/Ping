#!/usr/bin/env python3
"""Surgically flip ``spkDirFull="1"`` to ``spkDirFull="0"`` across every
factory ``.ping`` sidecar and factory preset XML.

Why this exists
---------------
v2.11 introduced source-radiation models. Several factory presets had
been authored before that with the legacy 2D cardioid speaker model AND
``spk_directivity_full=true`` (full speaker directivity, no fade-to-omni
on higher-order reflections) to compensate for the lack of a real source
radiation pattern. After v2.14 switched everything to "Generic
instrument", that compensation is no longer wanted — full speaker
directivity over-bakes high-frequency directional bias into every
reflection bounce and double-counts what the source radiation now
provides.

The fix is to flip the boolean back to its `IRSynthParams` default
(``false``). The string-length is preserved (``"1"`` → ``"0"``) so we do
not need to recompute the JUCE binary state length header for preset
XMLs — pure in-place byte substitution.

Files touched
-------------
- ``Installer/factory_irs/**/*.ping``      — plain-text XML sidecars
- ``Installer/factory_presets/**/*.xml``   — JUCE binary state blobs
  (``'VC2!'`` magic + LE u32 length + XML payload + NUL terminator).
  Because the only change is one ASCII character flip, the length
  header stays valid as-is.

Files where ``spkDirFull`` is missing entirely are skipped — they
already default to ``false`` via ``IRSynthParams::spk_directivity_full``
and via the loader fallback in
``PluginProcessor::irSynthParamsFromXml`` and
``Tools/rebake_factory_irs.cpp::paramsFromPingAttrs``.

Idempotent — re-running on already-flipped files is a no-op (the
search pattern won't match).

Run from repo root:
    python3 Tools/reset_spkdirfull_in_factory.py
"""
from __future__ import annotations

import os
import re
import sys

# Match exactly the on-state. Both .ping and the embedded XML inside
# JUCE preset blobs use the same attribute spelling. Whitespace around
# `=` is theoretically possible in XML but the writer
# (PluginProcessor.cpp) never emits it, so a strict pattern is fine and
# doubles as a safety check.
PATTERN = re.compile(rb'spkDirFull="1"')
REPLACEMENT = b'spkDirFull="0"'


def process_binary_file(path: str) -> str:
    """Edit a file as raw bytes, preserving byte length and everything
    that isn't the literal pattern. Works for both plain-text .ping XML
    (preserves CRLF/LF line endings) and JUCE binary preset blobs
    (preserves the 8-byte 'VC2!' header, since the new value is the
    same length so the declared XML length stays correct)."""
    with open(path, "rb") as f:
        raw = f.read()

    new, n_subs = PATTERN.subn(REPLACEMENT, raw)
    if n_subs == 0:
        return f"unchanged: {path}"
    if n_subs > 1:
        # Defensive: if a file ever contains the substring twice
        # (shouldn't happen with the current writer), bail loudly so we
        # can investigate manually rather than silently corrupting it.
        return f"SKIP (multiple matches, n={n_subs}): {path}"

    assert len(new) == len(raw), "byte length must be preserved"
    with open(path, "wb") as f:
        f.write(new)
    return f"flipped: {path}"


def main() -> int:
    repo_root = os.path.normpath(os.path.join(os.path.dirname(__file__), os.pardir))

    targets: list[str] = []
    for sub, ext in [("Installer/factory_irs", ".ping"),
                     ("Installer/factory_presets", ".xml")]:
        root = os.path.join(repo_root, sub)
        if not os.path.isdir(root):
            print(f"WARN: {root} not found", file=sys.stderr)
            continue
        for d, _, files in os.walk(root):
            for f in sorted(files):
                if f.endswith(ext):
                    targets.append(os.path.join(d, f))
    targets.sort()

    counts = {"flipped": 0, "unchanged": 0, "skip": 0}
    for path in targets:
        result = process_binary_file(path)
        if result.startswith("flipped:"):
            print(f"  {result}")
            counts["flipped"] += 1
        elif result.startswith("unchanged:"):
            counts["unchanged"] += 1
        else:
            print(f"  {result}")
            counts["skip"] += 1

    print()
    print(f"Total files scanned: {len(targets)}")
    print(f"  flipped  : {counts['flipped']}")
    print(f"  unchanged: {counts['unchanged']} (no spkDirFull=\"1\" present)")
    print(f"  skipped  : {counts['skip']}")
    return 0 if counts["skip"] == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
