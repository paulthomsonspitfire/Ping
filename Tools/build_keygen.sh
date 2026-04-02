#!/bin/bash
# Rebuild keygen for older macOS (avoids "macOS 26 required" on older systems)
cd "$(dirname "$0")"
g++ -std=c++17 -mmacosx-version-min=10.13 -o keygen keygen.cpp -lsodium -I/opt/homebrew/opt/libsodium/include -L/opt/homebrew/opt/libsodium/lib 2>/dev/null || \
g++ -std=c++17 -mmacosx-version-min=10.13 -o keygen keygen.cpp -lsodium
echo "Built keygen"
