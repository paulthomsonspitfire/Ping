#!/usr/bin/env python3
"""
trim_factory_irs.py — Trim trailing silence from P!NG factory IR .wav files.

Reads every .wav file under the target directory (recursively), removes silent
tail audio below −80 dB of peak, adds a 200 ms safety tail (minimum 300 ms
total length), and writes the result back in place with the same bit depth,
sample rate and channel count.

Matches the logic in PingProcessor::loadIRFromBuffer (the universal silence
trim added in v2.3.3) so that loading factory IRs from disk gives the same
effective length as freshly synthesised IRs — critical for NUPC convolution
performance at small buffer sizes (128–256 samples).

Usage:
    python3 Tools/trim_factory_irs.py [directory]

    directory defaults to  Installer/factory_irs/
    Pass /Library/Application\ Support/Ping/P\!NG/Factory\ IRs/ to fix
    already-installed copies on the user's machine.

No external dependencies — pure Python 3 stdlib only (struct, wave).
Handles mono, stereo and 4-channel WAV files at any standard bit depth
(16, 24, or 32-bit integer PCM).
"""

import os
import struct
import sys
import math
import shutil
import tempfile

THRESHOLD_DB  = -80.0          # trim below this level relative to peak
SAFETY_TAIL_S =  0.200         # 200 ms safety margin after last significant sample
MIN_LEN_S     =  0.300         # never trim shorter than 300 ms
DRY_RUN       = False          # set True to report changes without writing


# ---------------------------------------------------------------------------
# WAV I/O helpers (pure stdlib — handles 4-channel 24-bit)
# ---------------------------------------------------------------------------

def _read_wav(path: str):
    """Return (samples_float [ch][n], sample_rate, bit_depth, n_channels)."""
    with open(path, "rb") as f:
        data = f.read()

    # RIFF header
    if data[:4] != b"RIFF" or data[8:12] != b"WAVE":
        raise ValueError(f"{path}: not a RIFF/WAVE file")

    pos = 12
    fmt_data = None
    audio_data = None

    while pos < len(data) - 8:
        chunk_id   = data[pos:pos+4]
        chunk_size = struct.unpack_from("<I", data, pos+4)[0]
        chunk_body = data[pos+8 : pos+8+chunk_size]
        pos += 8 + chunk_size + (chunk_size & 1)  # word-align

        if chunk_id == b"fmt ":
            fmt_data = chunk_body
        elif chunk_id == b"data":
            audio_data = chunk_body

    if fmt_data is None or audio_data is None:
        raise ValueError(f"{path}: missing fmt or data chunk")

    audio_fmt  = struct.unpack_from("<H", fmt_data, 0)[0]
    n_channels = struct.unpack_from("<H", fmt_data, 2)[0]
    sample_rate = struct.unpack_from("<I", fmt_data, 4)[0]
    bit_depth  = struct.unpack_from("<H", fmt_data, 14)[0]

    # WAVE_FORMAT_EXTENSIBLE (0xFFFE) — standard for multichannel / 24-bit.
    # Sub-format GUID starts at byte 24; first 2 bytes give the actual format code.
    WAVE_FORMAT_EXTENSIBLE = 0xFFFE
    if audio_fmt == WAVE_FORMAT_EXTENSIBLE:
        if len(fmt_data) < 26:
            raise ValueError(f"{path}: EXTENSIBLE fmt chunk too short ({len(fmt_data)} bytes)")
        sub_fmt = struct.unpack_from("<H", fmt_data, 24)[0]
        audio_fmt = sub_fmt  # treat as the underlying format (1=PCM, 3=float)

    if audio_fmt not in (1, 3):  # 1=PCM, 3=IEEE float
        raise ValueError(f"{path}: unsupported audio format {audio_fmt} (only PCM/float)")

    bytes_per_sample = bit_depth // 8
    n_frames = len(audio_data) // (n_channels * bytes_per_sample)

    # Decode to float in [-1, 1]
    samples = [[0.0] * n_frames for _ in range(n_channels)]

    if audio_fmt == 3 and bit_depth == 32:
        for i in range(n_frames):
            for ch in range(n_channels):
                off = (i * n_channels + ch) * 4
                samples[ch][i] = struct.unpack_from("<f", audio_data, off)[0]
    elif bit_depth == 16:
        scale = 1.0 / 32768.0
        for i in range(n_frames):
            for ch in range(n_channels):
                off = (i * n_channels + ch) * 2
                v = struct.unpack_from("<h", audio_data, off)[0]
                samples[ch][i] = v * scale
    elif bit_depth == 24:
        scale = 1.0 / 8388608.0
        for i in range(n_frames):
            for ch in range(n_channels):
                off = (i * n_channels + ch) * 3
                b0, b1, b2 = audio_data[off], audio_data[off+1], audio_data[off+2]
                v = b0 | (b1 << 8) | (b2 << 16)
                if v >= 0x800000:
                    v -= 0x1000000
                samples[ch][i] = v * scale
    elif bit_depth == 32 and audio_fmt == 1:
        scale = 1.0 / 2147483648.0
        for i in range(n_frames):
            for ch in range(n_channels):
                off = (i * n_channels + ch) * 4
                v = struct.unpack_from("<i", audio_data, off)[0]
                samples[ch][i] = v * scale
    else:
        raise ValueError(f"{path}: unsupported bit depth {bit_depth}")

    return samples, sample_rate, bit_depth, n_channels, data, fmt_data


def _write_wav(path: str, samples, sample_rate: int, bit_depth: int,
               n_channels: int, original_file_bytes: bytes, original_fmt: bytes):
    """Write trimmed samples back, preserving the original fmt chunk exactly,
    the original chunk order, and the original file permissions."""
    n_frames = len(samples[0])
    bytes_per_sample = bit_depth // 8

    # Encode samples back to PCM bytes
    pcm = bytearray(n_frames * n_channels * bytes_per_sample)
    if bit_depth == 16:
        scale = 32767.0
        for i in range(n_frames):
            for ch in range(n_channels):
                v = int(max(-32768, min(32767, round(samples[ch][i] * scale))))
                struct.pack_into("<h", pcm, (i * n_channels + ch) * 2, v)
    elif bit_depth == 24:
        scale = 8388607.0
        for i in range(n_frames):
            for ch in range(n_channels):
                v = int(max(-8388608, min(8388607, round(samples[ch][i] * scale))))
                if v < 0:
                    v += 0x1000000
                off = (i * n_channels + ch) * 3
                pcm[off]   = v & 0xFF
                pcm[off+1] = (v >> 8) & 0xFF
                pcm[off+2] = (v >> 16) & 0xFF
    elif bit_depth == 32:
        scale = 2147483647.0
        for i in range(n_frames):
            for ch in range(n_channels):
                v = int(max(-2147483648, min(2147483647, round(samples[ch][i] * scale))))
                struct.pack_into("<i", pcm, (i * n_channels + ch) * 4, v)

    new_data_chunk = b"data" + struct.pack("<I", len(pcm)) + bytes(pcm)

    # Reconstruct the RIFF body preserving the original chunk order exactly.
    # Only the data chunk content is replaced; all other chunks are kept verbatim.
    # This preserves chunk ordering (e.g. JUNK → fmt → data stays that way) and
    # avoids accidentally reordering chunks that some readers care about.
    body = bytearray()
    pos = 12
    while pos < len(original_file_bytes) - 8:
        chunk_id   = original_file_bytes[pos:pos+4]
        chunk_size = struct.unpack_from("<I", original_file_bytes, pos+4)[0]
        chunk_end  = pos + 8 + chunk_size + (chunk_size & 1)
        if chunk_id == b"data":
            body += new_data_chunk
            if chunk_size & 1:            # preserve pad byte alignment
                body += b"\x00"
        else:
            body += original_file_bytes[pos:chunk_end]
        pos = chunk_end

    riff = b"RIFF" + struct.pack("<I", 4 + len(body)) + b"WAVE" + bytes(body)

    # Preserve original file permissions — tempfile.mkstemp creates files as
    # mode 0600 (owner-only), which would prevent the audio plugin (running as
    # the user, not root) from reading system-library files owned by root.
    orig_mode = os.stat(path).st_mode

    # Atomic write: write to temp file, fix permissions, then rename
    dir_ = os.path.dirname(path)
    fd, tmp = tempfile.mkstemp(dir=dir_, suffix=".wav.tmp")
    try:
        with os.fdopen(fd, "wb") as f:
            f.write(riff)
        os.chmod(tmp, orig_mode)   # must happen before os.replace
        os.replace(tmp, path)
    except Exception:
        try:
            os.unlink(tmp)
        except OSError:
            pass
        raise


# ---------------------------------------------------------------------------
# Trim logic — mirrors PingProcessor::loadIRFromBuffer universal trim
# ---------------------------------------------------------------------------

def trim_ir(samples, sample_rate: int):
    """
    Apply −80 dB silence trim.
    Returns (trimmed_samples, original_n_frames, new_n_frames).
    """
    n_frames = len(samples[0])
    n_channels = len(samples)

    # Peak across all channels
    peak = 1e-10
    for ch in range(n_channels):
        for s in samples[ch]:
            v = abs(s)
            if v > peak:
                peak = v

    threshold = peak * 1e-4  # −80 dB

    # Last sample above threshold across all channels
    last_significant = 0
    for ch in range(n_channels):
        ch_samples = samples[ch]
        for i in range(n_frames - 1, -1, -1):
            if abs(ch_samples[i]) > threshold:
                if i > last_significant:
                    last_significant = i
                break

    safety_tail = int(SAFETY_TAIL_S * sample_rate)
    min_len     = int(MIN_LEN_S     * sample_rate)

    new_len = min(last_significant + safety_tail + 1, n_frames)
    new_len = max(new_len, min_len)

    if new_len >= n_frames:
        return samples, n_frames, n_frames  # nothing to trim

    trimmed = [ch_samples[:new_len] for ch_samples in samples]
    return trimmed, n_frames, new_len


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def process_directory(root: str):
    wav_files = []
    for dirpath, _, filenames in os.walk(root):
        for fn in filenames:
            if fn.lower().endswith((".wav", ".aif", ".aiff")):
                wav_files.append(os.path.join(dirpath, fn))

    if not wav_files:
        print(f"No .wav/.aif files found under {root}")
        return

    total_saved_s = 0.0
    trimmed_count = 0
    skipped_count = 0

    for path in sorted(wav_files):
        try:
            samples, sr, bd, nch, raw_bytes, fmt_bytes = _read_wav(path)
        except Exception as e:
            print(f"  SKIP  {os.path.basename(path)}: {e}")
            skipped_count += 1
            continue

        trimmed, orig_n, new_n = trim_ir(samples, sr)

        saved_s = (orig_n - new_n) / sr
        total_saved_s += saved_s

        orig_s = orig_n / sr
        new_s  = new_n  / sr

        rel = os.path.relpath(path, root)
        if new_n == orig_n:
            print(f"  OK    {rel}  ({orig_s:.1f} s — nothing to trim)")
        else:
            print(f"  TRIM  {rel}  {orig_s:.1f} s → {new_s:.1f} s  (saved {saved_s:.1f} s)")
            trimmed_count += 1
            if not DRY_RUN:
                _write_wav(path, trimmed, sr, bd, nch, raw_bytes, fmt_bytes)

    print()
    print(f"Done: {trimmed_count} file(s) trimmed, {skipped_count} skipped")
    print(f"Total silence removed: {total_saved_s:.1f} s")
    if DRY_RUN:
        print("(DRY RUN — no files were modified)")


if __name__ == "__main__":
    if len(sys.argv) > 1:
        targets = sys.argv[1:]
    else:
        # When run with no arguments, trim BOTH common P!NG IR locations:
        #   1. Installer/factory_irs/  (repo source — keeps installer content lean)
        #   2. ~/Documents/P!NG/IRs/  (user-saved IRs — fixes long files saved before trim was added)
        # Run with an explicit path argument to target a specific directory, e.g.:
        #   python3 Tools/trim_factory_irs.py "/Library/Application Support/Ping/P!NG/Factory IRs"
        script_dir = os.path.dirname(os.path.abspath(__file__))
        repo_factory = os.path.normpath(os.path.join(script_dir, "..", "Installer", "factory_irs"))
        user_irs     = os.path.expanduser("~/Documents/P!NG/IRs")
        targets = [d for d in [repo_factory, user_irs] if os.path.isdir(d)]
        if not targets:
            print("No IR directories found. Pass a path explicitly:", file=sys.stderr)
            print("  python3 Tools/trim_factory_irs.py <directory>", file=sys.stderr)
            sys.exit(1)

    print(f"Threshold: {THRESHOLD_DB} dB  |  Safety tail: {SAFETY_TAIL_S*1000:.0f} ms  |  Min length: {MIN_LEN_S*1000:.0f} ms")
    print()
    for target in targets:
        if not os.path.isdir(target):
            print(f"Warning: directory not found, skipping: {target}", file=sys.stderr)
            continue
        print(f"=== {target} ===")
        process_directory(target)
        print()
