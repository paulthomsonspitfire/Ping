#!/usr/bin/env python3
"""P!NG Serial Generator — single and batch modes.
Uses the cryptography library to sign with the Ed25519 private key.
The private_key.bin file is ONLY read, never written or modified.
"""

import os
import sys
import tkinter as tk
from tkinter import ttk, scrolledtext, messagebox

try:
    from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey
except ImportError:
    root = tk.Tk()
    root.withdraw()
    messagebox.showerror(
        "Missing dependency",
        "Install cryptography:\n  pip3 install cryptography",
    )
    sys.exit(1)

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
BASE32_ALPHABET = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567"

DEFAULT_TIER   = "pro"
DEFAULT_EXPIRY = "2027-12-31"


# ---------------------------------------------------------------------------
# Core crypto — reads private_key.bin (never writes it)
# ---------------------------------------------------------------------------

def find_private_key_path():
    """Look for private_key.bin next to this script or one level up inside a .app bundle."""
    candidates = [
        SCRIPT_DIR,
        os.path.dirname(os.path.dirname(os.path.dirname(SCRIPT_DIR))),
    ]
    for d in candidates:
        p = os.path.join(d, "private_key.bin")
        if os.path.isfile(p):
            return p
    return None


def normalise_name(name):
    result = []
    last_was_space = True
    for c in name:
        if c.isspace():
            if not last_was_space:
                result.append(" ")
            last_was_space = True
        else:
            result.append(c.lower())
            last_was_space = False
    return "".join(result).rstrip()


def base32_encode(data):
    result = []
    buffer = 0
    bits_left = 0
    for byte in data:
        buffer = (buffer << 8) | byte
        bits_left += 8
        while bits_left >= 5:
            bits_left -= 5
            result.append(BASE32_ALPHABET[(buffer >> bits_left) & 0x1F])
    if bits_left > 0:
        buffer <<= 5 - bits_left
        result.append(BASE32_ALPHABET[buffer & 0x1F])
    return "".join(result)


def format_serial(raw):
    return "-".join(raw[i : i + 5] for i in range(0, len(raw), 5))


def generate_serial(name, tier=DEFAULT_TIER, expiry=DEFAULT_EXPIRY):
    """Sign name|tier|expiry with the private key and return a formatted serial.
    Returns (serial_string, None) on success or (None, error_message) on failure.
    private_key.bin is opened read-only and never modified.
    """
    key_path = find_private_key_path()
    if not key_path:
        return None, (
            "private_key.bin not found.\n"
            "Make sure it lives in the same folder as this script (Tools/)."
        )
    with open(key_path, "rb") as f:
        key_bytes = f.read()
    if len(key_bytes) != 64:
        return None, "private_key.bin has wrong size — it may be corrupted."
    seed = key_bytes[:32]
    private_key = Ed25519PrivateKey.from_private_bytes(seed)
    payload = (normalise_name(name) + "|" + tier + "|" + expiry).encode("utf-8")
    sig = private_key.sign(payload)
    signed = bytes(sig) + payload
    raw = base32_encode(signed)
    return format_serial(raw), None


# ---------------------------------------------------------------------------
# GUI
# ---------------------------------------------------------------------------

root = tk.Tk()
root.title("P!NG Serial Generator")
root.geometry("620x420")
root.resizable(True, True)

notebook = ttk.Notebook(root)
notebook.pack(fill=tk.BOTH, expand=True, padx=10, pady=10)


# ── Single tab ──────────────────────────────────────────────────────────────

single_frame = ttk.Frame(notebook, padding=12)
notebook.add(single_frame, text="  Single  ")

ttk.Label(single_frame, text="Customer name:").pack(anchor=tk.W)
name_entry = ttk.Entry(single_frame, width=55)
name_entry.pack(fill=tk.X, pady=(0, 8))
name_entry.focus()

ttk.Label(
    single_frame,
    text=f"Tier: {DEFAULT_TIER}  |  Expiry: {DEFAULT_EXPIRY}",
    font=("", 10),
).pack(anchor=tk.W)

single_generate_btn = ttk.Button(single_frame, text="Generate Serial")
single_generate_btn.pack(pady=8)

single_output = scrolledtext.ScrolledText(
    single_frame, height=10, width=60, font=("Menlo", 11)
)
single_output.pack(fill=tk.BOTH, expand=True, pady=8)


def do_single_generate():
    name = name_entry.get().strip()
    if not name:
        messagebox.showwarning("Missing name", "Please enter a customer name.")
        return
    single_generate_btn.config(state=tk.DISABLED)
    single_output.delete(1.0, tk.END)
    single_output.insert(tk.END, "Generating…\n")
    root.update()
    try:
        serial, err = generate_serial(name)
        single_output.delete(1.0, tk.END)
        if err:
            single_output.insert(tk.END, err)
            messagebox.showerror("Error", err)
        else:
            single_output.insert(
                tk.END,
                f"\nCustomer : {name}\n"
                f"Tier     : {DEFAULT_TIER}\n"
                f"Expiry   : {DEFAULT_EXPIRY}\n"
                f"Serial   : {serial}\n",
            )
    except Exception as e:
        single_output.delete(1.0, tk.END)
        single_output.insert(tk.END, str(e))
        messagebox.showerror("Error", str(e))
    finally:
        single_generate_btn.config(state=tk.NORMAL)


single_generate_btn.config(command=do_single_generate)
root.bind("<Return>", lambda e: do_single_generate() if notebook.index("current") == 0 else None)


# ── Batch tab ───────────────────────────────────────────────────────────────

batch_frame = ttk.Frame(notebook, padding=12)
notebook.add(batch_frame, text="  Batch  ")

# Info bar
info_lbl = ttk.Label(
    batch_frame,
    text=f"Tier: {DEFAULT_TIER}  |  Expiry: {DEFAULT_EXPIRY}  |  One name per line",
    font=("", 10),
)
info_lbl.pack(anchor=tk.W, pady=(0, 6))

# Two-column layout: input left, output right
columns = ttk.Frame(batch_frame)
columns.pack(fill=tk.BOTH, expand=True)
columns.columnconfigure(0, weight=1)
columns.columnconfigure(1, weight=1)
columns.rowconfigure(1, weight=1)

ttk.Label(columns, text="Paste names (one per line):").grid(
    row=0, column=0, sticky="w", padx=(0, 4)
)
ttk.Label(columns, text="Results:").grid(row=0, column=1, sticky="w", padx=(4, 0))

batch_input = scrolledtext.ScrolledText(
    columns, height=16, font=("Menlo", 11), wrap=tk.WORD
)
batch_input.grid(row=1, column=0, sticky="nsew", padx=(0, 4), pady=(2, 0))

batch_output = scrolledtext.ScrolledText(
    columns, height=16, font=("Menlo", 11), wrap=tk.WORD, state=tk.DISABLED
)
batch_output.grid(row=1, column=1, sticky="nsew", padx=(4, 0), pady=(2, 0))

# Buttons
btn_row = ttk.Frame(batch_frame)
btn_row.pack(pady=(8, 0), anchor=tk.W)

batch_generate_btn = ttk.Button(btn_row, text="Generate All")
batch_generate_btn.pack(side=tk.LEFT, padx=(0, 8))

batch_copy_btn = ttk.Button(btn_row, text="Copy Results", state=tk.DISABLED)
batch_copy_btn.pack(side=tk.LEFT, padx=(0, 8))

batch_status = ttk.Label(btn_row, text="", foreground="grey")
batch_status.pack(side=tk.LEFT)


def do_batch_generate():
    raw_text = batch_input.get(1.0, tk.END)
    names = [line.strip() for line in raw_text.splitlines() if line.strip()]
    if not names:
        messagebox.showwarning("No names", "Paste at least one name into the left panel.")
        return

    batch_generate_btn.config(state=tk.DISABLED)
    batch_copy_btn.config(state=tk.DISABLED)
    batch_status.config(text=f"Generating 0 / {len(names)}…")
    batch_output.config(state=tk.NORMAL)
    batch_output.delete(1.0, tk.END)
    root.update()

    results = []
    errors  = []

    for i, name in enumerate(names, 1):
        batch_status.config(text=f"Generating {i} / {len(names)}…")
        root.update()
        serial, err = generate_serial(name)
        if err:
            errors.append(f"{name}: ERROR — {err}")
            results.append(f"{name}: ERROR")
        else:
            results.append(f"{name}: {serial}")

    output_text = "\n".join(results)
    batch_output.delete(1.0, tk.END)
    batch_output.insert(tk.END, output_text)
    batch_output.config(state=tk.DISABLED)

    batch_generate_btn.config(state=tk.NORMAL)
    batch_copy_btn.config(state=tk.NORMAL)

    if errors:
        batch_status.config(
            text=f"Done — {len(results) - len(errors)} OK, {len(errors)} error(s)",
            foreground="red",
        )
        messagebox.showerror("Some errors", "\n".join(errors))
    else:
        batch_status.config(
            text=f"Done — {len(results)} serial{'s' if len(results) != 1 else ''} generated ✓",
            foreground="green",
        )


def do_batch_copy():
    text = batch_output.get(1.0, tk.END).strip()
    if text:
        root.clipboard_clear()
        root.clipboard_append(text)
        batch_status.config(text="Copied to clipboard ✓", foreground="green")


batch_generate_btn.config(command=do_batch_generate)
batch_copy_btn.config(command=do_batch_copy)


# ---------------------------------------------------------------------------
root.mainloop()
