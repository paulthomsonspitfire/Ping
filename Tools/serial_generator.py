#!/usr/bin/env python3
"""Simple GUI to generate P!NG serial numbers. Uses cryptography lib (often pre-installed)."""

import os
import sys
import tkinter as tk
from tkinter import ttk, scrolledtext, messagebox

try:
    from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey
except ImportError:
    import tkinter as tk
    from tkinter import messagebox
    root = tk.Tk()
    root.withdraw()
    messagebox.showerror(
        "Missing dependency",
        "Install cryptography:\n  pip3 install cryptography",
    )
    sys.exit(1)

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
BASE32_ALPHABET = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567"


def find_private_key_path():
    """Look for private_key.bin in script dir or Tools (parent of .app)."""
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


def generate_serial(name, tier="pro", expiry="2027-12-31"):
    key_path = find_private_key_path()
    if not key_path:
        return None, "private_key.bin not found. Run ./keygen --generate-keys in Tools, then keep private_key.bin there."
    with open(key_path, "rb") as f:
        key_bytes = f.read()
    if len(key_bytes) != 64:
        return None, "private_key.bin has wrong size. Run ./keygen --generate-keys again."
    seed = key_bytes[:32]
    private_key = Ed25519PrivateKey.from_private_bytes(seed)
    payload = (normalise_name(name) + "|" + tier + "|" + expiry).encode("utf-8")
    sig = private_key.sign(payload)
    signed = bytes(sig) + payload
    raw = base32_encode(signed)
    return format_serial(raw), None


def do_generate():
    name = name_entry.get().strip()
    if not name:
        messagebox.showwarning("Missing name", "Please enter a customer name.")
        return
    generate_btn.config(state=tk.DISABLED)
    output_text.delete(1.0, tk.END)
    output_text.insert(tk.END, "Generating...\n")
    root.update()
    try:
        serial, err = generate_serial(name)
        if err:
            output_text.delete(1.0, tk.END)
            output_text.insert(tk.END, err)
            messagebox.showerror("Error", err)
        else:
            output_text.delete(1.0, tk.END)
            output_text.insert(
                tk.END,
                f"\nCustomer : {name}\nTier     : pro\nExpiry   : 2027-12-31\nSerial   : {serial}\n\n",
            )
    except Exception as e:
        output_text.delete(1.0, tk.END)
        output_text.insert(tk.END, str(e))
        messagebox.showerror("Error", str(e))
    finally:
        generate_btn.config(state=tk.NORMAL)


root = tk.Tk()
root.title("P!NG Serial Generator")
root.geometry("480x300")
root.resizable(True, True)

frame = ttk.Frame(root, padding=12)
frame.pack(fill=tk.BOTH, expand=True)

ttk.Label(frame, text="Customer name:").pack(anchor=tk.W)
name_entry = ttk.Entry(frame, width=55)
name_entry.pack(fill=tk.X, pady=(0, 8))
name_entry.focus()

ttk.Label(frame, text="Tier: pro  |  Expiry: 2027-12-31", font=("", 10)).pack(anchor=tk.W)

generate_btn = ttk.Button(frame, text="Generate Serial", command=do_generate)
generate_btn.pack(pady=8)

output_text = scrolledtext.ScrolledText(frame, height=10, width=60, font=("Menlo", 11))
output_text.pack(fill=tk.BOTH, expand=True, pady=8)

root.mainloop()
