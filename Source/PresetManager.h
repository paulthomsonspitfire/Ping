#pragma once

#include <JuceHeader.h>

/** Manages factory and user presets.
 *
 *  Factory presets live at:
 *    /Library/Application Support/Ping/P!NG/Factory Presets/
 *
 *  User presets live at:
 *    ~/Library/Audio/Presets/Ping/P!NG/
 *
 *  Both locations support one level of subcategory subfolders which become
 *  section headings in the preset combo.
 */
class PresetManager
{
public:
    struct PresetEntry
    {
        juce::File   file;
        juce::String category;    // subfolder name, e.g. "Spaces"; empty = root level
        bool         isFactory = false;
    };

    /** User preset root: ~/Library/Audio/Presets/Ping/P!NG/ (created if absent). */
    static juce::File getPresetDirectory();

    /** Factory preset root: /Library/Application Support/Ping/P!NG/Factory Presets/ */
    static juce::File getSystemFactoryPresetFolder();

    /** Returns all presets — factory first (sorted by category then filename),
     *  then user (sorted by category then filename). */
    static juce::Array<PresetEntry> getEntries();

    /** Flat list of display names (all entries). Kept for any existing callers. */
    static juce::StringArray getPresetNames();

    /** Resolve a display name (filename stem) to a File.
     *  Searches getEntries() first; falls back to a new file in the user root. */
    static juce::File getPresetFile (const juce::String& name);
};
