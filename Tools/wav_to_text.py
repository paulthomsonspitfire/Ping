#!/usr/bin/env python3
"""
WAV to Text — Drop a WAV file and get a text file.

Usage:
  python wav_to_text.py <file.wav>
  python wav_to_text.py   (then drag and drop a file when prompted)

Output: <filename>.txt in the same folder with:
  - Header: sample rate, channels, bit depth, duration, sample count
  - Sample data: one frame per line (space-separated channels)

Uses only the standard library (wave, struct). No pip install needed.
"""

import sys
import wave
import struct
from pathlib import Path
from typing import Optional


def read_wav_metadata(wav_path: Path) -> dict:
    with wave.open(str(wav_path), "rb") as w:
        return {
            "channels": w.getnchannels(),
            "sample_width": w.getsampwidth(),
            "frame_rate": w.getframerate(),
            "n_frames": w.getnframes(),
            "compression": w.getcomptype(),
        }


def samples_to_text(wav_path: Path, out_path: Path, max_frames: Optional[int] = None) -> None:
    with wave.open(str(wav_path), "rb") as w:
        ch = w.getnchannels()
        sw = w.getsampwidth()
        sr = w.getframerate()
        n_frames = w.getnframes()

    duration_sec = n_frames / sr if sr else 0

    # Format for unpack: little-endian
    if sw == 1:  # 8-bit unsigned
        fmt = "<" + "B" * ch
        scale = 1.0 / 128.0 - 1.0  # map 0..255 to roughly -1..1
    elif sw == 2:  # 16-bit signed
        fmt = "<" + "h" * ch
        scale = 1.0 / 32768.0
    elif sw == 3:  # 24-bit signed (no struct codec; parse manually)
        fmt = None
        scale = 1.0 / 8388608.0
    elif sw == 4:  # 32-bit (assume signed)
        fmt = "<" + "i" * ch
        scale = 1.0 / 2147483648.0
    else:
        raise ValueError(f"Unsupported sample width: {sw} bytes")

    def unpack_frame(frame_bytes: bytes) -> list:
        if sw == 3:
            out = []
            for c in range(ch):
                b0, b1, b2 = frame_bytes[c * 3 : (c + 1) * 3]
                v = b0 | (b1 << 8) | (b2 << 16)
                if v >= 0x800000:
                    v -= 0x1000000
                out.append(v)
            return out
        return list(struct.unpack(fmt, frame_bytes))

    frame_size = sw * ch
    write_frames = n_frames if max_frames is None else min(n_frames, max_frames)
    truncated = write_frames < n_frames

    lines = [
        f"# WAV: {wav_path.name}",
        f"# Sample rate: {sr} Hz",
        f"# Channels: {ch}",
        f"# Bit depth: {sw * 8}",
        f"# Duration: {duration_sec:.4f} s",
        f"# Total frames: {n_frames}",
        f"# Frames in this file: {write_frames}" + (" (truncated)" if truncated else ""),
        "# --- samples (one frame per line, normalized to approx -1..1) ---",
    ]

    with wave.open(str(wav_path), "rb") as w:
        with open(out_path, "w") as out:
            out.write("\n".join(lines) + "\n")
            read = 0
            while read < write_frames:
                to_read = min(4096, write_frames - read)
                raw = w.readframes(to_read)
                count = len(raw) // frame_size
                for i in range(count):
                    frame = raw[i * frame_size : (i + 1) * frame_size]
                    vals = unpack_frame(frame)
                    if sw == 1:
                        normalized = [((v / 128.0) - 1.0) for v in vals]
                    else:
                        normalized = [v * scale for v in vals]
                    out.write(" ".join(f"{x:.6f}" for x in normalized) + "\n")
                read += count

    print(f"Wrote {out_path} ({write_frames} frames)")


def main() -> None:
    if len(sys.argv) >= 2:
        wav_path = Path(sys.argv[1])
    else:
        print("Drag and drop a WAV file (or paste path), then press Enter:")
        try:
            line = input().strip().strip("'\"")
        except EOFError:
            print("No input.")
            sys.exit(1)
        if not line:
            print("No file given.")
            sys.exit(1)
        wav_path = Path(line)

    if not wav_path.exists():
        print(f"File not found: {wav_path}")
        sys.exit(1)
    if wav_path.suffix.lower() not in (".wav", ".wave"):
        print("Not a WAV file (expected .wav). Continuing anyway.")

    out_path = wav_path.with_suffix(wav_path.suffix + ".txt")

    # Optional: cap frames so the text file doesn't get huge (e.g. 10 s)
    max_frames = None
    if len(sys.argv) >= 3 and sys.argv[2] == "--limit":
        try:
            max_sec = float(sys.argv[3]) if len(sys.argv) > 3 else 10.0
            with wave.open(str(wav_path), "rb") as w:
                sr = w.getframerate()
            max_frames = int(max_sec * sr)
        except (IndexError, ValueError):
            pass

    try:
        samples_to_text(wav_path, out_path, max_frames=max_frames)
    except wave.Error as e:
        print(f"Invalid or unsupported WAV: {e}")
        sys.exit(1)
    except ValueError as e:
        print(f"Error: {e}")
        sys.exit(1)


if __name__ == "__main__":
    main()
