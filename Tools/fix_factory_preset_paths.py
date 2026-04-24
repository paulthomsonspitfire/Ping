#!/usr/bin/env python3
"""
fix_factory_preset_paths.py

Surgical patcher for factory preset XMLs in Installer/factory_presets/.

Each preset is a JUCE binary preset file in the format:
    [4B magic 'VC2!'][4B little-endian XML byte length][XML payload][1B NUL]

The XML payload contains an `irFilePath="..."` attribute on the root
<Parameters> element. Presets that were saved by hand from the running
plugin (rather than generated fresh by Tools/generate_factory_irs)
record the user's local IR location (e.g.
/Users/paulstudio/Library/Audio/Impulse Responses/Ping/<Name>.wav)
instead of the system-wide install path.

That breaks playback after a release: when the .pkg is installed on a
clean machine the IR lands at /Library/Application Support/Ping/Factory
IRs/<Category>/<Name>.wav, but the preset still asks for the user path.
The plugin's load fallback finds the IR by filename stem in the IR
catalogue, which resolves to the freshly installed factory copy on most
machines — but on the original author's machine the user-path file
still exists (an OLD .wav), so the preset loads the stale IR. That's
the cause of the v2.9.1 "loaded preset sounds different from
recalculated IR" report.

This script rewrites the irFilePath attribute in each preset to point at
    /Library/Application Support/Ping/Factory IRs/<Category>/<IR>.wav
where <Category> is the folder that actually contains <IR>.wav under
Installer/factory_irs. NOTHING ELSE is touched.

Use --dry-run to preview.

NEVER touches WAV files, .ping sidecars, or any other content.
"""

import argparse
import struct
import sys
from pathlib import Path

SYSTEM_BASE = "/Library/Application Support/Ping/Factory IRs"
MAGIC = b"VC2!"


def build_ir_catalogue(ir_root: Path) -> dict:
    """Map IR stem (e.g. 'Cello Epic Hall') -> category folder name (e.g. 'Halls').

    Aux WAVs ('_direct', '_outrig', '_ambient') are ignored — only MAIN .wavs
    register a venue in the catalogue."""
    cat = {}
    for wav in ir_root.rglob("*.wav"):
        stem = wav.stem
        if any(stem.endswith(s) for s in ("_direct", "_outrig", "_ambient")):
            continue
        cat[stem] = wav.parent.name
    return cat


def parse_preset(data: bytes) -> tuple:
    """Return (xml_str, has_trailing_nul). Raises ValueError on invalid format."""
    if len(data) < 8 or data[:4] != MAGIC:
        raise ValueError("not a VC2! preset (missing magic)")
    declared_size = struct.unpack("<I", data[4:8])[0]
    payload = data[8:]
    has_nul = payload.endswith(b"\x00")
    if has_nul:
        xml_bytes = payload[:-1]
    else:
        xml_bytes = payload
    if len(xml_bytes) != declared_size:
        # Be lenient: trust the actual byte count up to the trailing NUL.
        # Some hand-written presets may have a slightly off size header.
        pass
    return xml_bytes.decode("utf-8"), has_nul


def write_preset(path: Path, xml_str: str) -> None:
    xml_bytes = xml_str.encode("utf-8")
    with path.open("wb") as f:
        f.write(MAGIC)
        f.write(struct.pack("<I", len(xml_bytes)))
        f.write(xml_bytes)
        f.write(b"\x00")


def find_ir_filepath(xml: str) -> tuple:
    """Find the irFilePath="..." attribute. Returns (start, end, value) or None."""
    needle = 'irFilePath="'
    i = xml.find(needle)
    if i < 0:
        return None
    val_start = i + len(needle)
    val_end = xml.find('"', val_start)
    if val_end < 0:
        return None
    return (val_start, val_end, xml[val_start:val_end])


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("preset_root", type=Path,
                        help="Directory containing factory preset .xml files (typically Installer/factory_presets)")
    parser.add_argument("--ir-root", type=Path, default=Path("Installer/factory_irs"),
                        help="Directory containing factory IR .wav files (default: Installer/factory_irs)")
    parser.add_argument("--dry-run", action="store_true",
                        help="Show planned changes without writing any files")
    args = parser.parse_args()

    if not args.preset_root.is_dir():
        print(f"error: preset root not found: {args.preset_root}", file=sys.stderr)
        return 1
    if not args.ir_root.is_dir():
        print(f"error: IR root not found: {args.ir_root}", file=sys.stderr)
        return 1

    catalogue = build_ir_catalogue(args.ir_root)
    if not catalogue:
        print(f"error: no IR .wav files found under {args.ir_root}", file=sys.stderr)
        return 1

    presets = sorted(args.preset_root.rglob("*.xml"))
    if not presets:
        print(f"no preset .xml files under {args.preset_root}")
        return 0

    print(f"=== Preset path patcher ===")
    print(f"  Preset root:   {args.preset_root}")
    print(f"  IR root:       {args.ir_root}")
    print(f"  Catalogue:     {len(catalogue)} IR(s)")
    print(f"  System base:   {SYSTEM_BASE}")
    print(f"  Mode:          {'DRY RUN (no files written)' if args.dry_run else 'WRITE'}")
    print()

    n_patched = 0
    n_clean = 0
    n_skipped = 0
    n_failed = 0

    for preset_path in presets:
        rel = preset_path.relative_to(args.preset_root)
        try:
            data = preset_path.read_bytes()
            xml, _has_nul = parse_preset(data)
        except Exception as e:
            print(f"  ERROR  {rel}: {e}")
            n_failed += 1
            continue

        found = find_ir_filepath(xml)
        if not found:
            print(f"  SKIP   {rel}: no irFilePath attribute")
            n_skipped += 1
            continue
        val_start, val_end, current_path = found

        ir_stem = Path(current_path).stem
        ir_cat = catalogue.get(ir_stem)
        if ir_cat is None:
            print(f"  WARN   {rel}: IR '{ir_stem}' not in catalogue — leaving path unchanged")
            print(f"           current: {current_path}")
            n_skipped += 1
            continue

        new_path = f"{SYSTEM_BASE}/{ir_cat}/{ir_stem}.wav"
        if new_path == current_path:
            n_clean += 1
            continue

        print(f"  PATCH  {rel}")
        print(f"           ir:  {ir_stem!r} → category '{ir_cat}'")
        print(f"           old: {current_path}")
        print(f"           new: {new_path}")

        new_xml = xml[:val_start] + new_path + xml[val_end:]
        if not args.dry_run:
            try:
                write_preset(preset_path, new_xml)
            except Exception as e:
                print(f"           ERROR writing: {e}")
                n_failed += 1
                continue
        n_patched += 1

    print()
    print(f"=== Summary ===")
    print(f"  Patched: {n_patched}")
    print(f"  Already clean: {n_clean}")
    print(f"  Skipped: {n_skipped}")
    print(f"  Failed: {n_failed}")
    if args.dry_run and n_patched > 0:
        print(f"\n  Dry run: re-run without --dry-run to apply.")
    return 1 if n_failed > 0 else 0


if __name__ == "__main__":
    sys.exit(main())
