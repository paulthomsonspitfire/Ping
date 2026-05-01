#!/usr/bin/env python3
"""Surgically inject ``srcRad="Generic instrument"`` into every factory
preset XML that does not already carry an explicit ``srcRad`` attribute.

Why this exists
---------------
v2.14 rebaked all factory IRs against the new 3D engine using "Generic
instrument" as the source-radiation default, and updated every
``Installer/factory_irs/*.ping`` sidecar accordingly. However, the
``Installer/factory_presets/**/*.xml`` files — which are what the user
actually loads through the plugin's preset menu — were missed. They are
JUCE binary state blobs (header + XML payload + NUL terminator) and the
embedded ``<irSynthParams .../>`` element had no ``srcRad`` attribute, so
``PluginProcessor::irSynthParamsFromXml`` falls back to "Cardioid
(legacy)" on load. This script fixes that without touching anything else
in the file (parameter values, channel automation, line endings, etc.).

JUCE binary state format
------------------------
JUCE's ``copyXmlToBinary`` / ``getXmlFromBinary`` round-trip writes:

    4 bytes   ASCII magic 'VC2!'
    4 bytes   little-endian uint32 = XML byte length (excluding NUL)
    N bytes   XML payload (UTF-8)
    1 byte    \\x00 terminator

Editing strategy
----------------
1. Verify the magic + read the declared length and confirm
   ``len(file) == 8 + N + 1`` and the trailing byte is NUL.
2. Slice out the XML payload byte-for-byte (preserves whitespace,
   attribute order, casing — everything).
3. Locate the unique self-closing ``<irSynthParams ... />`` element. If
   it already has ``srcRad="..."`` keep that value (manually-tweaked
   preset). Otherwise inject ``srcRad="Generic instrument"`` immediately
   before the ``/>`` closer.
4. Re-emit the file with a recomputed length header.

Run from repo root:
    python3 Tools/add_srcrad_to_factory_presets.py
"""
from __future__ import annotations

import os
import re
import struct
import sys

ROOT = os.path.join(os.path.dirname(__file__), os.pardir, "Installer", "factory_presets")
ROOT = os.path.normpath(ROOT)

# Canonical preset name as registered in IRSynthEngine.cpp (lowercase 'i').
# byPreset() lookup is case-insensitive (iEqual), but using the canonical
# spelling here matches what the plugin would write back via
# getStateInformation, so a save/reload round-trip is a true no-op.
SRC_RAD_VALUE = "Generic instrument"

JUCE_MAGIC = b"VC2!"

# The factory presets all use a single self-closing <irSynthParams .../>.
# Match non-greedily up to the first unescaped ' />' or '/>' so we never
# capture across element boundaries.
IRSYNTH_RE = re.compile(rb"<irSynthParams\b[^>]*?/>", re.DOTALL)
HAS_SRCRAD_RE = re.compile(rb'\bsrcRad\s*=\s*"')


def edit_payload(payload: bytes) -> tuple[bytes, str]:
    """Return (new_payload, status). status is one of:
       'edited', 'already-has-srcRad', 'no-irSynthParams'."""
    m = IRSYNTH_RE.search(payload)
    if not m:
        return payload, "no-irSynthParams"
    elem = m.group(0)
    if HAS_SRCRAD_RE.search(elem):
        return payload, "already-has-srcRad"

    # Inject before the trailing '/>' (preserve any whitespace before it).
    # The element ends with either ' />' or '/>'. We always prepend a
    # single space + the new attribute, regardless of which form, so the
    # resulting attribute list stays well-formed and human-readable.
    assert elem.endswith(b"/>"), f"Unexpected element ending: {elem[-10:]!r}"
    inject = f' srcRad="{SRC_RAD_VALUE}"'.encode("utf-8")
    new_elem = elem[:-2].rstrip() + inject + b"/>"
    new_payload = payload[: m.start()] + new_elem + payload[m.end() :]
    return new_payload, "edited"


def process_file(path: str) -> str:
    with open(path, "rb") as f:
        raw = f.read()

    if raw[:4] != JUCE_MAGIC:
        return f"SKIP (no JUCE magic): {path}"

    declared = struct.unpack("<I", raw[4:8])[0]
    expected_total = 8 + declared + 1  # +1 for NUL terminator
    if len(raw) != expected_total or raw[-1] != 0:
        return (
            f"SKIP (size/terminator mismatch — declared={declared}, "
            f"actual_total={len(raw)}, last_byte={raw[-1]:#x}): {path}"
        )

    payload = raw[8 : 8 + declared]
    new_payload, status = edit_payload(payload)

    if status != "edited":
        return f"{status}: {path}"

    new_total = 8 + len(new_payload) + 1
    new_header = JUCE_MAGIC + struct.pack("<I", len(new_payload))
    new_raw = new_header + new_payload + b"\x00"
    assert len(new_raw) == new_total

    with open(path, "wb") as f:
        f.write(new_raw)

    return f"edited: {path}  (+{len(new_payload) - declared} bytes)"


def main() -> int:
    if not os.path.isdir(ROOT):
        print(f"ERROR: factory_presets directory not found: {ROOT}", file=sys.stderr)
        return 2

    targets: list[str] = []
    for d, _, files in os.walk(ROOT):
        for f in sorted(files):
            if f.endswith(".xml"):
                targets.append(os.path.join(d, f))
    targets.sort()

    counts = {"edited": 0, "already-has-srcRad": 0, "no-irSynthParams": 0, "skip": 0}
    for path in targets:
        result = process_file(path)
        print(f"  {result}")
        if result.startswith("edited:"):
            counts["edited"] += 1
        elif result.startswith("already-has-srcRad"):
            counts["already-has-srcRad"] += 1
        elif result.startswith("no-irSynthParams"):
            counts["no-irSynthParams"] += 1
        else:
            counts["skip"] += 1

    print()
    print(f"Total files scanned: {len(targets)}")
    for k, v in counts.items():
        print(f"  {k}: {v}")
    return 0 if counts["skip"] == 0 and counts["no-irSynthParams"] == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
