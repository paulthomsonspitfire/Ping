#include "IRManager.h"

juce::File IRManager::getIRFolder()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
               .getChildFile ("P!NG")
               .getChildFile ("IRs");
}

juce::File IRManager::getSystemFactoryIRFolder()
{
    return juce::File ("/Library/Application Support")
               .getChildFile ("Ping")
               .getChildFile ("P!NG")
               .getChildFile ("Factory IRs");
}

void IRManager::scanFolder()
{
    irEntries.clear();

    const juce::StringArray extensions { "*.wav", "*.WAV", "*.aiff", "*.aif", "*.AIFF", "*.AIF" };

    // Helper: scan one directory for audio files, sort, deduplicate, and append entries.
    auto addFilesFromDir = [&] (const juce::File& dir,
                                 const juce::String& category,
                                 bool isFactory)
    {
        juce::Array<juce::File> found;
        for (const auto& ext : extensions)
            dir.findChildFiles (found, juce::File::findFiles, false, ext);
        found.sort();
        for (auto& f : found)
        {
            bool alreadyIn = false;
            for (const auto& e : irEntries)
                if (e.file == f) { alreadyIn = true; break; }
            if (! alreadyIn)
                irEntries.add ({ f, category, isFactory });
        }
    };

    // ── Pass 1: factory folder ────────────────────────────────────────────────
    // Files directly in the factory root get no category heading.
    // Immediate subdirectories become named category sections.
    auto factoryFolder = getSystemFactoryIRFolder();
    if (factoryFolder.isDirectory())
    {
        addFilesFromDir (factoryFolder, {}, true);

        juce::Array<juce::File> subDirs;
        factoryFolder.findChildFiles (subDirs, juce::File::findDirectories, false);
        subDirs.sort();
        for (const auto& sub : subDirs)
            addFilesFromDir (sub, sub.getFileName(), true);
    }

    // ── Pass 2: user folder (flat) ────────────────────────────────────────────
    auto userFolder = getIRFolder();
    if (userFolder.isDirectory())
        addFilesFromDir (userFolder, {}, false);
}

void IRManager::refresh()
{
    scanFolder();
}

// ── Legacy flat-array accessors ───────────────────────────────────────────────
// These iterate irEntries in order (factory then user), matching the old irFiles behaviour.

juce::StringArray IRManager::getDisplayNames() const
{
    juce::StringArray names;
    for (const auto& e : irEntries)
        names.add (e.file.getFileNameWithoutExtension());
    return names;
}

juce::Array<juce::File> IRManager::getIRFiles() const
{
    juce::Array<juce::File> files;
    for (const auto& e : irEntries)
        files.add (e.file);
    return files;
}

juce::File IRManager::getIRFileAt (int index) const
{
    if (juce::isPositiveAndBelow (index, irEntries.size()))
        return irEntries.getReference (index).file;
    return juce::File();
}

// ── 4-channel subset (synthesised IRs for IRSynthComponent) ──────────────────

juce::StringArray IRManager::getDisplayNames4Channel() const
{
    juce::StringArray names;
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    for (const auto& e : irEntries)
    {
        std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (e.file));
        if (r && r->numChannels == 4)
            names.add (e.file.getFileNameWithoutExtension());
    }
    return names;
}

juce::Array<juce::File> IRManager::getIRFiles4Channel() const
{
    juce::Array<juce::File> out;
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    for (const auto& e : irEntries)
    {
        std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (e.file));
        if (r && r->numChannels == 4)
            out.add (e.file);
    }
    return out;
}

juce::File IRManager::getIRFileAt4Channel (int index) const
{
    auto files = getIRFiles4Channel();
    if (juce::isPositiveAndBelow (index, files.size()))
        return files.getReference (index);
    return juce::File();
}
