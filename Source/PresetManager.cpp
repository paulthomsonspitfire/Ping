#include "PresetManager.h"

juce::File PresetManager::getPresetDirectory()
{
    auto dir = juce::File::getSpecialLocation (juce::File::userHomeDirectory)
                   .getChildFile ("Library")
                   .getChildFile ("Audio")
                   .getChildFile ("Presets")
                   .getChildFile ("Ping");
    if (! dir.exists())
        dir.createDirectory();
    return dir;
}

juce::File PresetManager::getSystemFactoryPresetFolder()
{
    return juce::File ("/Library/Application Support")
               .getChildFile ("Ping")
               .getChildFile ("Factory Presets");
}

juce::Array<PresetManager::PresetEntry> PresetManager::getEntries()
{
    juce::Array<PresetEntry> entries;

    // Helper: scan *.xml files in a directory, sort, and add to entries
    auto addFilesFromDir = [&] (const juce::File& dir,
                                const juce::String& category,
                                bool isFactory)
    {
        auto files = dir.findChildFiles (juce::File::findFiles, false, "*.xml");
        files.sort();
        for (auto& f : files)
            entries.add ({ f, category, isFactory });
    };

    // ── Pass 1: factory folder ────────────────────────────────────────────────
    auto factoryRoot = getSystemFactoryPresetFolder();
    if (factoryRoot.isDirectory())
    {
        // Files directly in the factory root (no subcategory)
        addFilesFromDir (factoryRoot, {}, true);

        // One level of subcategory subfolders, sorted alphabetically
        auto subDirs = factoryRoot.findChildFiles (juce::File::findDirectories, false);
        subDirs.sort();
        for (auto& sub : subDirs)
            addFilesFromDir (sub, sub.getFileName(), true);
    }

    // ── Pass 2: user folder ───────────────────────────────────────────────────
    auto userRoot = getPresetDirectory();
    if (userRoot.isDirectory())
    {
        // Files directly in the user root (no subcategory)
        addFilesFromDir (userRoot, {}, false);

        // One level of subcategory subfolders, sorted alphabetically
        auto subDirs = userRoot.findChildFiles (juce::File::findDirectories, false);
        subDirs.sort();
        for (auto& sub : subDirs)
            addFilesFromDir (sub, sub.getFileName(), false);
    }

    return entries;
}

juce::StringArray PresetManager::getPresetNames()
{
    juce::StringArray names;
    for (const auto& e : getEntries())
        names.add (e.file.getFileNameWithoutExtension());
    return names;
}

juce::File PresetManager::getPresetFile (const juce::String& name)
{
    // Search all entries (factory + user) for a matching filename stem
    for (const auto& e : getEntries())
        if (e.file.getFileNameWithoutExtension() == name)
            return e.file;

    // Fallback: unsaved or new preset — target the user root
    return getPresetDirectory().getChildFile (name + ".xml");
}
