#!/usr/bin/env python3
"""
add_srcrad_to_factory_sidecars.py

Surgical bulk edit: add `srcRad="Generic Instrument"` to every .ping sidecar
under the given root that doesn't already have a `srcRad=` attribute.

The edit is a single attribute insertion just before the closing `/>` of the
`<irSynthParams ... />` element. Every other byte of the file is preserved
exactly. Idempotent — re-running on already-updated files is a no-op.

Usage:
    python3 add_srcrad_to_factory_sidecars.py <root_dir> [--dry-run]
"""
import re
import sys
import pathlib

ATTR = ' srcRad="Generic Instrument"'
PRESET_LABEL = 'Generic Instrument'

def main():
    args = sys.argv[1:]
    dry_run = '--dry-run' in args
    args = [a for a in args if a != '--dry-run']
    if len(args) != 1:
        print(__doc__)
        sys.exit(1)
    root = pathlib.Path(args[0])
    if not root.is_dir():
        print(f'not a directory: {root}', file=sys.stderr)
        sys.exit(1)

    pattern = re.compile(r'<irSynthParams\b[\s\S]*?/>')

    updated = 0
    skipped_already = 0
    failed_no_match = 0

    for p in sorted(root.rglob('*.ping')):
        # Read in binary so we preserve the file's existing line endings
        # exactly (the factory sidecars are CRLF — switching them to LF
        # would create a meaningless 100% diff). The regex still matches
        # against the decoded UTF-8 view; we patch the bytes by index.
        raw = p.read_bytes()
        txt = raw.decode('utf-8')
        if 'srcRad=' in txt:
            print(f'  skip (already has srcRad): {p.relative_to(root)}')
            skipped_already += 1
            continue
        m = pattern.search(txt)
        if not m:
            print(f'  WARN no <irSynthParams .../> in {p.relative_to(root)}')
            failed_no_match += 1
            continue
        elem = m.group(0)
        # Strip "/>" then any trailing whitespace (incl. \r in CRLF land),
        # then re-append ' srcRad="..."/>' so the new attribute lands on
        # the same line as the previous final attribute with one space gap.
        # Whitespace inside the element body (the indented attribute lines)
        # is left untouched.
        new_elem = elem[:-2].rstrip() + ATTR + '/>'
        # Patch the decoded string, then re-encode as UTF-8. CRLF in the
        # surrounding (unmodified) bytes is preserved because we only
        # replaced the byte range matched by the regex, which contains no
        # line endings (the element body is single-line-wrapped, but each
        # attribute-wrap line break is INSIDE the element, not at its
        # boundary; the close `/>` sits on the last attribute line so we
        # only touch text within that one line).
        new_txt = txt[:m.start()] + new_elem + txt[m.end():]
        new_raw = new_txt.encode('utf-8')
        if dry_run:
            print(f'  would update: {p.relative_to(root)}')
        else:
            p.write_bytes(new_raw)
            print(f'  updated:      {p.relative_to(root)}')
        updated += 1

    print()
    verb = 'Would update' if dry_run else 'Updated'
    print(f'{verb}: {updated}')
    print(f'Skipped (already had srcRad): {skipped_already}')
    if failed_no_match:
        print(f'WARN no match in: {failed_no_match}')
    if dry_run:
        print('\n(dry-run: no files written)')

if __name__ == '__main__':
    main()
