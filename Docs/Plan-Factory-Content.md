# Plan: Factory IRs and Presets — installer delivery and dual-location scanning

Ship a curated set of factory IRs and presets with the P!NG installer. Factory content lives permanently at `/Library/Application Support/Ping/P!NG/` (system-wide, read-only to the user). The plugin scans both that location and the existing per-user `~/` locations, merging results into a sectioned combo UI. Both IRs and presets support subcategory folders — factory content is organised by the developer; users can organise their own saves into subfolders of their choosing via a folder picker in the save UI. No per-user copying at launch.

---

## 1. File system layout

### Installed by the .pkg (system-wide, read-only)

```
/Library/Application Support/Ping/P!NG/
    Factory IRs/
        Halls/
            <name>.wav
            <name>.wav.ping    ← IR Synth sidecar (optional but preferred)
        Rooms/
            <name>.wav
            <name>.wav.ping
        Plates/
            ...
        (further subcategories as needed)
    Factory Presets/
        Spaces/
            <name>.xml
        Textures/
            <name>.xml
        ...
        (further subcategories as needed)
```

Subcategory folder names (one level deep) become the section headings shown in their respective combos. Both IRs and presets support this structure.

### User content

```
~/Documents/P!NG/IRs/               ← user IR location; flat (no user IR subcategories planned)
~/Library/Audio/Presets/Ping/P!NG/  ← user preset root
    Vocals/                          ← user-created subfolders (optional)
        <name>.xml
    Drums/
        <name>.xml
    <name>.xml                       ← presets saved with no folder sit at the root
```

User presets support the same one-level subfolder structure. Subfolders are created automatically when the user saves into a new folder via the save UI (see §5.5).

---

## 2. `IRManager` — new data model and dual-location scanning

### 2.1 New `IREntry` struct (`IRManager.h`)

Replace the bare `juce::Array<juce::File> irFiles` with a richer entry type:

```cpp
struct IREntry
{
    juce::File   file;
    juce::String category;   // subcategory folder name, e.g. "Halls"; empty for user IRs
    bool         isFactory = false;
};
```

The private member becomes `juce::Array<IREntry> irEntries`.

### 2.2 New static path method

```cpp
static juce::File getSystemFactoryIRFolder();
// → /Library/Application Support/Ping/P!NG/Factory IRs/
```

Uses `juce::File ("/Library/Application Support").getChildFile ("Ping/P!NG/Factory IRs")`.

### 2.3 Updated `scanFolder()`

Two passes:

**Pass 1 — factory folder (recursive one level):**
- If `getSystemFactoryIRFolder()` doesn't exist or isn't a directory, skip silently.
- Iterate immediate subdirectories (sorted alphabetically). For each subfolder, scan for `*.wav / *.aiff` (non-recursive) and add entries with `isFactory = true`, `category = subfolder.getFileName()`.
- Also scan any audio files directly in the factory root (no subfolder) with `isFactory = true`, `category = ""`.

**Pass 2 — user folder (flat, as now):**
- Same logic as the current implementation. `isFactory = false`, `category = ""`.

Factory entries come first in `irEntries`, sorted by category then by filename within each category. User entries follow, sorted by filename.

### 2.4 API changes

Existing methods that return flat `juce::File` arrays or `juce::StringArray` are **kept unchanged** for internal use (they just iterate `irEntries` extracting `.file`). No callers outside `PluginEditor` need to know about categories.

New methods added for combo population:

```cpp
const juce::Array<IREntry>& getEntries() const;   // full structured list
int  getNumFactoryEntries() const;
int  getNumUserEntries() const;
```

`getIRFileAt(int index)` continues to work by position over the flat `irEntries` array — this is now the authoritative index used throughout (replacing the old flat `irFiles` index, which was the same thing but less explicit).

The `getDisplayNames4Channel()` and `getIRFiles4Channel()` methods used by `IRSynthComponent` are unaffected — they just filter `irEntries` by channel count.

---

## 3. `PluginProcessor` — IR state persistence by filename

### 3.1 Replace `selectedIRIndex` (int) with `selectedIRFile` (juce::File)

**`PluginProcessor.h`:**

```cpp
// Remove:
int selectedIRIndex = -1;

// Add:
juce::File selectedIRFile;   // empty = no file IR selected (synth IR or nothing)
```

Update accessors:

```cpp
// Remove:
int  getSelectedIRIndex() const { return selectedIRIndex; }
void setSelectedIRIndex (int index) { selectedIRIndex = index; }

// Add:
juce::File getSelectedIRFile() const  { return selectedIRFile; }
void       setSelectedIRFile (const juce::File& f) { selectedIRFile = f; }
```

`irFromSynth` and `rawSynthBuffer` are unchanged.

### 3.2 `getStateInformation`

```cpp
// Remove:
xml->setAttribute ("irIndex", selectedIRIndex);

// Add:
if (selectedIRFile != juce::File())
    xml->setAttribute ("irFilePath", selectedIRFile.getFullPathName());
```

The synth IR buffer serialisation (the `<irSynthBuffer>` child element) is unchanged.

### 3.3 `setStateInformation`

```cpp
// Remove:
selectedIRIndex = xml->getIntAttribute ("irIndex", -1);

// Add — read new attribute:
juce::String savedPath = xml->getStringAttribute ("irFilePath", "");
if (savedPath.isNotEmpty())
    selectedIRFile = juce::File (savedPath);
else
    selectedIRFile = juce::File();
```

**Backward compatibility:** If `irFilePath` is absent but `irIndex` is present (old session), attempt to resolve by index position from the current `irManager.getEntries()` list:

```cpp
if (savedPath.isEmpty())
{
    int oldIndex = xml->getIntAttribute ("irIndex", -1);
    if (oldIndex >= 0)
    {
        auto f = irManager.getIRFileAt (oldIndex);
        if (f.existsAsFile())
            selectedIRFile = f;
    }
}
```

This fallback can be removed in a future version once all sessions have been re-saved.

### 3.4 IR loading in `setStateInformation`

The loading block after parsing the XML currently checks `selectedIRIndex >= 0`. Change to:

```cpp
if (selectedIRFile != juce::File() && selectedIRFile.existsAsFile())
    loadIRFromFile (selectedIRFile);
```

If the file no longer exists (e.g. user deleted it), fail silently — same behaviour as before.

### 3.5 `isIRFromSynth()` flag in `loadIRFromBuffer`

Unchanged. `selectedIRFile` is explicitly set to `juce::File()` (empty) inside `loadIRFromBuffer` when `fromSynth = true` (line 1641 area), same as `selectedIRIndex = -1` was before.

---

## 4. `PluginEditor` — sectioned IR combo

### 4.1 `refreshIRList()`

Rebuild the combo using `IRManager::getEntries()`. The structure is:

```
[ID 1]  Synthesized IR
─── Factory ───              addSectionHeading("Factory")
  ─ Halls ─                  addSectionHeading("  Halls")   (if any entries in this category)
  [ID 2]  Lyndhurst Hall
  [ID 3]  ...
  ─ Rooms ─                  addSectionHeading("  Rooms")
  [ID 4]  Small Room
─── Your IRs ───             addSectionHeading("Your IRs")  (only if user has any files)
  [ID 5]  My Custom IR
```

Section headings consume no IDs in JUCE's `ComboBox` — IDs remain `1 = Synth`, `2, 3, 4, … = entries` in the order returned by `getEntries()`. The index into `getEntries()` is therefore `selectedId - 2`.

Implementation:

```cpp
void PingEditor::refreshIRList()
{
    irCombo.clear();
    irCombo.addItem ("Synthesized IR", 1);

    const auto& entries = pingProcessor.getIRManager().getEntries();

    // --- Factory section ---
    bool factoryHeaderAdded = false;
    juce::String lastCategory;
    for (int i = 0; i < entries.size(); ++i)
    {
        const auto& e = entries[i];
        if (! e.isFactory) break;  // entries are factory-first

        if (! factoryHeaderAdded)
        {
            irCombo.addSectionHeading ("Factory");
            factoryHeaderAdded = true;
        }
        if (e.category != lastCategory)
        {
            if (e.category.isNotEmpty())
                irCombo.addSectionHeading ("  " + e.category);
            lastCategory = e.category;
        }
        irCombo.addItem (e.file.getFileNameWithoutExtension(), i + 2);
    }

    // --- User section ---
    bool userHeaderAdded = false;
    for (int i = 0; i < entries.size(); ++i)
    {
        const auto& e = entries[i];
        if (e.isFactory) continue;

        if (! userHeaderAdded)
        {
            irCombo.addSectionHeading ("Your IRs");
            userHeaderAdded = true;
        }
        irCombo.addItem (e.file.getFileNameWithoutExtension(), i + 2);
    }

    // --- Restore selection ---
    if (pingProcessor.isIRFromSynth())
    {
        irCombo.setSelectedId (1, juce::dontSendNotification);
    }
    else
    {
        juce::File selectedFile = pingProcessor.getSelectedIRFile();
        int foundId = -1;
        for (int i = 0; i < entries.size(); ++i)
        {
            if (entries[i].file == selectedFile)
                { foundId = i + 2; break; }
        }
        if (foundId >= 0)
            irCombo.setSelectedId (foundId, juce::dontSendNotification);
        else if (entries.size() > 0)
            irCombo.setSelectedId (2, juce::sendNotificationSync);  // first available
        loadSelectedIR();
    }
}
```

### 4.2 `loadSelectedIR()`

```cpp
void PingEditor::loadSelectedIR()
{
    int idx = irCombo.getSelectedId() - 2;  // -1 = Synth
    if (idx < 0)
    {
        pingProcessor.reloadSynthIR();
        return;
    }
    const auto& entries = pingProcessor.getIRManager().getEntries();
    if (! juce::isPositiveAndBelow (idx, entries.size()))
        return;
    auto file = entries[idx].file;
    pingProcessor.setSelectedIRFile (file);
    pingProcessor.loadIRFromFile (file);
    updateWaveform();
}
```

### 4.3 `comboBoxChanged()`

The `combo == &irCombo` branch:

```cpp
int idx = irCombo.getSelectedId() - 2;
if (idx >= 0)
{
    const auto& entries = pingProcessor.getIRManager().getEntries();
    if (juce::isPositiveAndBelow (idx, entries.size()))
        pingProcessor.setSelectedIRFile (entries[idx].file);
}
else
{
    pingProcessor.setSelectedIRFile (juce::File());  // synth selected
}
```

Remove all references to `setSelectedIRIndex` / `getSelectedIRIndex` in `PluginEditor.cpp`.

### 4.4 Constructor (lines 529–532)

Replace the `savedIdx`-based restore with a file-based lookup — this is now handled inside `refreshIRList()` itself, so the separate block at lines 529–531 (`int savedIdx = …`, `irCombo.setSelectedId (savedIdx + 2, …)`) is deleted. Only `loadSelectedIR()` remains on line 532.

---

## 5. `PresetManager` — dual-location scanning with subcategories

### 5.1 New `PresetEntry` struct (`PresetManager.h`)

Mirrors `IREntry` in `IRManager`. Replace `getPresetNames()` returning a flat `juce::StringArray` with a structured entry list:

```cpp
struct PresetEntry
{
    juce::File   file;
    juce::String category;    // subfolder name, e.g. "Spaces"; empty = root level
    bool         isFactory = false;
};
```

New method:

```cpp
static juce::Array<PresetEntry> getEntries();
```

The existing `getPresetNames()` and `getPresetFile()` are updated to use this internally (see §5.3 and §5.4).

### 5.2 New static path method

```cpp
static juce::File getSystemFactoryPresetFolder();
// → /Library/Application Support/Ping/P!NG/Factory Presets/
```

### 5.3 `getEntries()` implementation

Two passes, same pattern as `IRManager::scanFolder()`:

**Pass 1 — factory folder (one level of subfolders):**
- If `getSystemFactoryPresetFolder()` doesn't exist, skip silently.
- Sort and iterate immediate subdirectories. For each, scan `*.xml` non-recursively; `isFactory = true`, `category = subfolder.getFileName()`.
- Also scan any `.xml` directly in the factory root; `isFactory = true`, `category = ""`.

**Pass 2 — user folder (one level of subfolders):**
- If `getPresetDirectory()` doesn't exist, skip silently.
- Sort and iterate immediate subdirectories. For each, scan `*.xml` non-recursively; `isFactory = false`, `category = subfolder.getFileName()`.
- Also scan any `.xml` directly in the user root; `isFactory = false`, `category = ""`.

Entries ordered: factory (by category, then filename) then user (by category, then filename). Within each category, files sorted alphabetically.

### 5.4 Updated `getPresetFile(const juce::String& displayName)`

The display name shown in the combo is just the filename stem — no prefix encoding. Resolution goes through `getEntries()`:

```cpp
juce::File PresetManager::getPresetFile (const juce::String& name)
{
    // Search entries for a matching stem
    for (const auto& e : getEntries())
        if (e.file.getFileNameWithoutExtension() == name)
            return e.file;
    // Fallback: treat as unsaved user root preset
    return getPresetDirectory().getChildFile (name + ".xml");
}
```

Note: if a factory and user preset share the same display name, this returns whichever appears first in `getEntries()` (factory first). In practice this is only used for *loading* — saving always uses the explicit target path from the save UI (see §5.5).

### 5.5 Save UI — folder picker in `PluginEditor`

The current save UI is a text field for the preset name plus a Save button. Add a small **folder dropdown** to the left of the name field:

```
[ (no folder) ▾ ]  [ preset name          ]  [Save]
```

**Folder dropdown contents:**
```
  (no folder)          ← saves to user root
  ── existing folders ──
  Vocals
  Drums
  ...
  ──────────────────
  New folder...        ← special item, triggers input dialog
```

The dropdown is populated by `refreshFolderList()`, called whenever `refreshPresetList()` runs. It lists all immediate subfolder names found in the user preset directory (not factory subfolders — users can't save to the factory location).

When the user selects **"New folder..."**, show an async input dialog:

```cpp
juce::AlertWindow::showInputBoxAsync (
    "New folder", "Enter a folder name:", "", nullptr,
    [this] (const juce::String& name) {
        if (name.isNotEmpty())
        {
            // Add to dropdown and select it
            addFolderToDropdown (name);
            folderCombo.setText (name, juce::dontSendNotification);
        }
    });
```

**Save action** uses both the folder combo and the name field to build the target path:

```cpp
juce::String folderName = folderCombo.getText().trim();   // "" if "(no folder)" selected
juce::String presetName = presetNameField.getText().trim();
juce::File targetDir = folderName.isEmpty()
    ? PresetManager::getPresetDirectory()
    : PresetManager::getPresetDirectory().getChildFile (folderName);
targetDir.createDirectory();   // no-op if already exists
juce::File targetFile = targetDir.getChildFile (presetName + ".xml");
// ... write XML, then refreshPresetList()
```

Overwrite prompt: if `targetFile.existsAsFile()`, show a confirmation dialog before writing (existing behaviour for name collisions, extended to cover subfolder collisions too).

### 5.6 Preset combo in `PluginEditor` — `refreshPresetList()`

Build the combo with section headings mirroring the IR combo structure:

```
─── Factory ───           addSectionHeading
  ─ Spaces ─              addSectionHeading (subcategory)
  [ID n]  Big Hall
  ─ Textures ─
  [ID n]  Bloom Wash
─── Your Presets ───      addSectionHeading (only if user has any)
  ─ Vocals ─              addSectionHeading (user subcategory, if any)
  [ID n]  Dark Plate
  [ID n]  My Preset       ← root-level user preset (no subfolder)
```

IDs are assigned sequentially across all entries from `getEntries()`, same approach as the IR combo. Section headings consume no IDs.

---

## 6. `build_installer.sh` — payload additions

Add factory content to the staging payload alongside the plugin binaries:

```bash
# Factory content directories (source lives in Installer/factory_irs/ and factory_presets/)
FACTORY_APPSUPP="$PAYLOAD/Library/Application Support/Ping/P!NG"
mkdir -p "$FACTORY_APPSUPP/Factory IRs"
mkdir -p "$FACTORY_APPSUPP/Factory Presets"

if [[ -d "$SCRIPT_DIR/factory_irs" ]]; then
    cp -R "$SCRIPT_DIR/factory_irs/"* "$FACTORY_APPSUPP/Factory IRs/"
fi
if [[ -d "$SCRIPT_DIR/factory_presets" ]]; then
    cp -R "$SCRIPT_DIR/factory_presets/"* "$FACTORY_APPSUPP/Factory Presets/"
fi
```

The `if` guards mean the installer builds cleanly even before factory content is authored.

Permissions: `pkgbuild` with `--install-location "/"` preserves the filesystem permissions of the staging directory. `/Library/Application Support/` defaults to `755` (world-readable), which is correct — users can read factory files but not write to them.

---

## 7. Repo structure

```
Installer/
    build_installer.sh       ← updated (factory copy step added)
    factory_irs/             ← exists; populate with audio files + sidecars
        .gitkeep             ← keeps the folder tracked by git when empty
        Halls/               ← create subcategory folders as needed
            <name>.wav
            <name>.wav.ping
        Rooms/
            <name>.wav
            <name>.wav.ping
        ...
    factory_presets/         ← exists; populate with preset XML files
        .gitkeep
        Spaces/              ← create subcategory folders as needed
            <name>.xml
        Textures/
            <name>.xml
        ...
```

**Subcategory folders** inside both `factory_irs/` and `factory_presets/` are created by you as you author content — the folder name becomes the section heading shown in the plugin's combo. There is no fixed or required set of categories; add, rename, or remove them freely.

**To add more factory content in a future release:** drop new files into the appropriate folder (or create a new subfolder for a new category), bump the version in `CMakeLists.txt` and `build_installer.sh`, then rebuild the installer. The copy step in `build_installer.sh` picks up everything present automatically. No code changes needed.

`.gitattributes` should mark `.wav` and `.aiff` files as binary (`*.wav binary`, `*.aiff binary`) if not already.

---

## 8. Backward compatibility

| Scenario | Behaviour |
|---|---|
| Session saved with old `irIndex` int, no `irFilePath` | Falls back to index lookup; warns in debug build if index out of range |
| Factory IR file removed in a future update | `selectedIRFile.existsAsFile()` fails; loads nothing, combo shows no selection |
| User has no `~/Documents/P!NG/IRs/` folder | User section absent from combo (no heading shown); factory section unaffected |
| No factory folder installed (dev build, no .pkg run) | Factory section absent from combo; user section shown as before |
| Reinstall / update with new factory content | New files appear automatically — no user action needed |
| Multi-user Mac | Each user's `~/` content is independent; factory content is shared |

---

## 9. Implementation order

Steps 1–3 are tightly coupled (data model, processor state, and editor UI all touch the same index-vs-file change) and should be done in one pass:

1. **`IRManager`** — add `IREntry` struct, `getSystemFactoryIRFolder()`, update `scanFolder()`, add `getEntries()`.
2. **`PluginProcessor`** — replace `selectedIRIndex` with `selectedIRFile`; update `getStateInformation` / `setStateInformation`; add backward-compat fallback.
3. **`PluginEditor` IR side** — rewrite `refreshIRList()` and `loadSelectedIR()`; remove `setSelectedIRIndex` / `getSelectedIRIndex` references; delete the redundant constructor block at lines 529–531.
4. **`PresetManager`** — add `PresetEntry` struct, `getSystemFactoryPresetFolder()`, `getEntries()`; update `getPresetFile()`; update `refreshPresetList()` in `PluginEditor` for section headings.
5. **`PluginEditor` preset save UI** — add folder dropdown, `refreshFolderList()`, updated save action with subfolder creation, overwrite prompt.
6. **`build_installer.sh`** — already updated; create any initial subcategory folders inside `factory_irs/` and `factory_presets/` as content is authored.
7. **Populate factory content** and do an end-to-end test (see §10).

---

## 10. Test checklist

**IR combo**
- [ ] Fresh install: factory IRs appear under "Factory" with subcategory headings; user section absent if `~/Documents/P!NG/IRs/` is empty
- [ ] User adds an IR file to `~/Documents/P!NG/IRs/`; after refresh it appears under "Your IRs"
- [ ] Select a factory IR, save session, reopen: correct IR reloads (file path restored)
- [ ] Select a user IR, save session, reopen: correct IR reloads
- [ ] Session saved with old format (`irIndex` int): loads correctly via backward-compat path
- [ ] Delete a factory IR file from disk, reopen session that referenced it: loads nothing, no crash
- [ ] Reinstall with updated factory content: new files appear; user IR files untouched
- [ ] `IRSynthComponent` IR combo (`getDisplayNames4Channel`) still works correctly

**Preset combo**
- [ ] Fresh install: factory presets appear under "Factory" with subcategory headings; user section absent if no user presets exist
- [ ] Factory preset loads correctly
- [ ] User preset with same display name as a factory preset coexists in separate section
- [ ] Reinstall with updated factory presets: new factory presets appear; user presets untouched

**Preset save UI**
- [ ] Folder dropdown shows "(no folder)" plus any existing user subfolder names
- [ ] Saving with "(no folder)" selected writes to `~/Library/Audio/Presets/Ping/P!NG/<name>.xml`
- [ ] Saving with an existing folder selected writes to the correct subfolder
- [ ] Selecting "New folder..." prompts for a name; typing and confirming adds it to the dropdown and selects it
- [ ] Saving after creating a new folder creates the subfolder and writes the file
- [ ] Overwrite prompt appears if a file already exists at the target path (including subfolder)
- [ ] After saving, preset appears in the correct section/subsection of the preset combo

**Multi-user and edge cases**
- [ ] Multi-user Mac: user A's presets and IRs don't appear for user B; factory content visible to both
- [ ] No factory folder installed (dev build without .pkg): both combos degrade gracefully, user content still works
- [ ] Factory folder present but empty: no "Factory" section heading shown

**Regression**
- [ ] IR_01–IR_11 and DSP_01–DSP_11 tests all pass (no engine changes)
