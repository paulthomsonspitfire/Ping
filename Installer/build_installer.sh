#!/bin/bash
# Build P!NG Mac installer (.pkg)
# Run from project root: ./Installer/build_installer.sh
# Or: cmake --build build --target installer

set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build"
STAGING_DIR=""
OUTPUT_DIR="$PROJECT_ROOT/Installer/output"
VERSION="1.0.0"
PKG_ID="com.ping.audio.ping"
PKG_NAME="P!NG-Installer"

cleanup() {
    [[ -n "$STAGING_DIR" && -d "$STAGING_DIR" ]] && rm -rf "$STAGING_DIR"
}
trap cleanup EXIT

# Check build exists
AU_PLUGIN="$BUILD_DIR/Ping_artefacts/Release/AU/P!NG.component"
VST3_PLUGIN="$BUILD_DIR/Ping_artefacts/Release/VST3/P!NG.vst3"

if [[ ! -d "$AU_PLUGIN" ]]; then
    echo "Error: AU plugin not found. Build first: cmake --build build"
    exit 1
fi
if [[ ! -d "$VST3_PLUGIN" ]]; then
    echo "Error: VST3 plugin not found. Build first: cmake --build build"
    exit 1
fi

echo "Building P!NG installer..."
STAGING_DIR=$(mktemp -d)
PAYLOAD="$STAGING_DIR/payload"

# Create payload structure matching install locations
mkdir -p "$PAYLOAD/Library/Audio/Plug-Ins/Components"
mkdir -p "$PAYLOAD/Library/Audio/Plug-Ins/VST3"

cp -R "$AU_PLUGIN" "$PAYLOAD/Library/Audio/Plug-Ins/Components/"
cp -R "$VST3_PLUGIN" "$PAYLOAD/Library/Audio/Plug-Ins/VST3/"

mkdir -p "$OUTPUT_DIR"
PKG_OUTPUT="$OUTPUT_DIR/P!NG-Audio-Plug-In-${VERSION}.pkg"

# Build the package (will prompt for admin password when user runs it)
pkgbuild --root "$PAYLOAD" \
         --identifier "$PKG_ID" \
         --version "$VERSION" \
         --install-location "/" \
         "$PKG_OUTPUT"

echo ""
echo "Installer created: $PKG_OUTPUT"
echo ""
echo "Your friends can double-click the .pkg to install. macOS will prompt"
echo "for their admin password and install to /Library/Audio/Plug-Ins/"
echo ""
