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
VERSION="2.6.1"
PKG_ID="com.ping.audio.ping"
PKG_NAME="P!NG-Installer"

cleanup() {
    [[ -n "$STAGING_DIR" && -d "$STAGING_DIR" ]] && rm -rf "$STAGING_DIR"
}
trap cleanup EXIT

# Check build exists (JUCE may output to Release/ or directly under Ping_artefacts)
if [[ -d "$BUILD_DIR/Ping_artefacts/Release/AU/P!NG.component" ]]; then
    AU_PLUGIN="$BUILD_DIR/Ping_artefacts/Release/AU/P!NG.component"
    VST3_PLUGIN="$BUILD_DIR/Ping_artefacts/Release/VST3/P!NG.vst3"
elif [[ -d "$BUILD_DIR/Ping_artefacts/AU/P!NG.component" ]]; then
    AU_PLUGIN="$BUILD_DIR/Ping_artefacts/AU/P!NG.component"
    VST3_PLUGIN="$BUILD_DIR/Ping_artefacts/VST3/P!NG.vst3"
else
    echo "Error: AU plugin not found. Build first: cmake --build build"
    exit 1
fi
if [[ ! -d "$VST3_PLUGIN" ]]; then
    echo "Error: VST3 plugin not found at $VST3_PLUGIN"
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

# Factory content — copied from Installer/factory_irs/ and Installer/factory_presets/
# Subfolder names inside factory_irs/ become the category headings shown in the plugin UI.
# Safe to build without content: the if-guards skip the copy when the source folders are empty.
FACTORY_DEST="$PAYLOAD/Library/Application Support/Ping"
mkdir -p "$FACTORY_DEST/Factory IRs"
mkdir -p "$FACTORY_DEST/Factory Presets"

if compgen -G "$SCRIPT_DIR/factory_irs/*" > /dev/null 2>&1; then
    # Trim trailing silence from factory IRs before packaging.
    # This ensures the NUPC background FFT thread is not overloaded at small buffer sizes.
    # The trim script runs in-place on the staging copy so the source files are never modified.
    cp -R "$SCRIPT_DIR/factory_irs/"* "$FACTORY_DEST/Factory IRs/"
    # Remove .gitkeep if it ended up in the payload
    rm -f "$FACTORY_DEST/Factory IRs/.gitkeep"
    echo "Factory IRs: trimming silence..."
    python3 "$PROJECT_ROOT/Tools/trim_factory_irs.py" "$FACTORY_DEST/Factory IRs"
    echo "Factory IRs: copied and trimmed"
else
    echo "Factory IRs: none found (skipping)"
fi

if compgen -G "$SCRIPT_DIR/factory_presets/*" > /dev/null 2>&1; then
    cp -R "$SCRIPT_DIR/factory_presets/"* "$FACTORY_DEST/Factory Presets/"
    rm -f "$FACTORY_DEST/Factory Presets/.gitkeep"
    echo "Factory Presets: copied"
else
    echo "Factory Presets: none found (skipping)"
fi

mkdir -p "$OUTPUT_DIR"
PKG_OUTPUT="$OUTPUT_DIR/P!NG-Audio-Plug-In-${VERSION}.pkg"

# --- Postinstall script ---------------------------------------------------
# Runs as root after the payload is installed. Migrates any system-wide
# licence file from the old P!NG-nested path to the new flat path.
# Per-user licence migration is handled by the plugin at runtime.
SCRIPTS_DIR="$STAGING_DIR/scripts"
mkdir -p "$SCRIPTS_DIR"
cat > "$SCRIPTS_DIR/postinstall" << 'POSTINSTALL'
#!/bin/bash
OLD="/Library/Application Support/Audio/Ping/P!NG/licence.xml"
NEW_DIR="/Library/Application Support/Audio/Ping"
NEW="$NEW_DIR/licence.xml"

if [[ -f "$OLD" && ! -f "$NEW" ]]; then
    mkdir -p "$NEW_DIR"
    cp "$OLD" "$NEW"
    rm -f "$OLD"
    # Remove old P!NG directory if now empty
    rmdir "/Library/Application Support/Audio/Ping/P!NG" 2>/dev/null || true
    echo "P!NG: migrated system licence to $NEW"
fi
exit 0
POSTINSTALL
chmod +x "$SCRIPTS_DIR/postinstall"

# Build the package (will prompt for admin password when user runs it)
pkgbuild --root "$PAYLOAD" \
         --scripts "$SCRIPTS_DIR" \
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
