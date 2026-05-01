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
VERSION="2.14.1"
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

# ── Pre-flight factory-IR inventory (v2.9.0) ──────────────────────────────
# Prints a summary of what will be packaged and flags any factory IRs whose
# .wav is older than its .ping sidecar (likely-stale after a sidecar edit
# without a rebake). Purely informational — warnings are non-blocking. The
# TTY gate at the end of this block lets you abort before any work is done;
# when run non-interactively (CI) the script auto-continues.
#
# This script NEVER regenerates .wav or .ping files. To rebake .wavs from
# sidecars, run Tools/rebake_factory_irs explicitly beforehand.
if [[ -d "$SCRIPT_DIR/factory_irs" ]]; then
    echo ""
    echo "=== Factory IR inventory ==="
    ping_count=0
    wav_count=0
    stale_count=0
    missing_wav=0
    stale_list=()
    missing_list=()

    while IFS= read -r -d '' ping; do
        ping_count=$((ping_count + 1))
        wav="${ping%.ping}.wav"
        rel="${wav#$PROJECT_ROOT/}"
        if [[ -f "$wav" ]]; then
            wav_count=$((wav_count + 1))
            # "$wav" -ot "$ping" → wav is older than ping
            if [[ "$wav" -ot "$ping" ]]; then
                stale_count=$((stale_count + 1))
                stale_list+=("$rel")
            fi
        else
            missing_wav=$((missing_wav + 1))
            missing_list+=("$rel")
        fi
    done < <(find "$SCRIPT_DIR/factory_irs" -name '*.ping' -print0)

    echo "  Sidecars (.ping): $ping_count"
    echo "  MAIN .wavs:       $wav_count"

    if [[ $stale_count -gt 0 ]]; then
        echo ""
        echo "  ${stale_count} stale .wav(s) — older than their .ping sidecar:"
        for p in "${stale_list[@]}"; do echo "    $p"; done
    fi
    if [[ $missing_wav -gt 0 ]]; then
        echo ""
        echo "  ${missing_wav} missing .wav(s) — sidecar present, no MAIN .wav:"
        for p in "${missing_list[@]}"; do echo "    $p"; done
    fi
    if [[ $stale_count -gt 0 || $missing_wav -gt 0 ]]; then
        echo ""
        echo "  To regenerate .wavs from sidecars without touching the .pings, run:"
        echo "    ./build/rebake_factory_irs Installer/factory_irs"
        echo ""
    fi

    # Preset path safety (v2.9.2): scan factory preset XMLs for irFilePath
    # values that don't point at the system-wide install location. Presets
    # saved by hand from the running plugin record the user's local
    # /Users/<name>/Library/... path; if shipped that way, the installer
    # will work on the author's machine but on a clean install the plugin
    # will load the wrong (or no) IR. The patcher fix is:
    #   python3 Tools/fix_factory_preset_paths.py Installer/factory_presets
    # Warnings only — does not block the build.
    if [[ -d "$SCRIPT_DIR/factory_presets" ]]; then
        echo ""
        echo "=== Factory preset path inventory ==="
        preset_count=0
        bad_path_count=0
        bad_path_list=()
        local_re='/Library/Application Support/Ping/Factory IRs/'
        while IFS= read -r -d '' xml; do
            preset_count=$((preset_count + 1))
            # Extract irFilePath value via grep on the binary file (XML payload is ASCII).
            ir_path=$(grep -ao 'irFilePath="[^"]*"' "$xml" 2>/dev/null | head -n1 | sed 's/^irFilePath="//; s/"$//')
            if [[ -z "$ir_path" ]]; then continue; fi
            if [[ "$ir_path" != "$local_re"* ]]; then
                bad_path_count=$((bad_path_count + 1))
                rel_xml="${xml#$PROJECT_ROOT/}"
                bad_path_list+=("$rel_xml -> $ir_path")
            fi
        done < <(find "$SCRIPT_DIR/factory_presets" -name '*.xml' -print0)

        echo "  Preset XMLs:      $preset_count"
        if [[ $bad_path_count -gt 0 ]]; then
            echo ""
            echo "  ${bad_path_count} preset(s) have irFilePath OUTSIDE the system install location"
            echo "  (this means the .pkg will install correctly but the plugin will load"
            echo "   the wrong IR on any machine that doesn't have the user-path file):"
            for entry in "${bad_path_list[@]}"; do echo "    $entry"; done
            echo ""
            echo "  To fix: python3 Tools/fix_factory_preset_paths.py Installer/factory_presets"
            echo ""
        fi
    fi

    # Interactive confirmation. Reads directly from /dev/tty (not stdin),
    # so the prompt still works when this script is invoked via
    # `cmake --build build --target installer` — cmake pipes the child's
    # stdin, but /dev/tty remains attached to the controlling terminal.
    # CI / no-TTY environments (where /dev/tty is unreadable) auto-continue
    # so pipelines aren't blocked.
    #
    # Opt-out: set PING_INSTALLER_SKIP_PROMPT=1 in the environment to
    # bypass the prompt even when a TTY is available. Use this from
    # automation / agent sessions where a human isn't available to
    # approve. Do NOT set it as a default in shell rc files.
    if [[ -r /dev/tty && "${PING_INSTALLER_SKIP_PROMPT:-0}" != "1" ]]; then
        read -r -p "Continue with installer build? [y/N] " reply </dev/tty
        case "$reply" in
            [Yy]*) ;;
            *) echo "Aborted."; exit 1 ;;
        esac
    elif [[ "${PING_INSTALLER_SKIP_PROMPT:-0}" == "1" ]]; then
        echo "(PING_INSTALLER_SKIP_PROMPT=1 — skipping confirmation prompt.)"
    fi
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
