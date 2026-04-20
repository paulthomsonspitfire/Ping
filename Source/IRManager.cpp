#include "IRManager.h"

namespace
{
    // Multi-mic IRs are saved as a set of sibling files with a shared base stem:
    //   Venue.wav           ← MAIN (has the .ping sidecar)
    //   Venue_direct.wav    ← DIRECT
    //   Venue_outrig.wav    ← OUTRIG
    //   Venue_ambient.wav   ← AMBIENT
    //
    // PingProcessor::loadIRFromFile auto-loads the three aux siblings whenever the
    // MAIN file is selected, so showing the aux files as separate combo entries is
    // redundant and confusing. We hide them here — but only when the MAIN base file
    // actually exists in the same directory. An orphaned aux file (no base sibling)
    // stays visible as a fallback so the user can still load it directly.
    const juce::StringArray kAuxSuffixes { "_direct", "_outrig", "_ambient" };

    bool endsWithAuxSuffix (const juce::String& stem, juce::String& baseStemOut)
    {
        for (const auto& suf : kAuxSuffixes)
        {
            if (stem.endsWithIgnoreCase (suf))
            {
                baseStemOut = stem.dropLastCharacters (suf.length());
                return true;
            }
        }
        return false;
    }

    bool hasBaseSiblingInSameDir (const juce::File& auxFile, const juce::String& baseStem)
    {
        auto parent = auxFile.getParentDirectory();
        if (! parent.isDirectory())
            return false;
        static const juce::StringArray exts { ".wav", ".WAV", ".aiff", ".aif", ".AIFF", ".AIF" };
        for (const auto& ext : exts)
            if (parent.getChildFile (baseStem + ext).existsAsFile())
                return true;
        return false;
    }
}

juce::File IRManager::getIRFolder()
{
    auto dir = juce::File::getSpecialLocation (juce::File::userHomeDirectory)
                   .getChildFile ("Library")
                   .getChildFile ("Audio")
                   .getChildFile ("Impulse Responses")
                   .getChildFile ("Ping");
    if (! dir.exists())
        dir.createDirectory();
    return dir;
}

juce::File IRManager::getSystemFactoryIRFolder()
{
    return juce::File ("/Library/Application Support")
               .getChildFile ("Ping")
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

    // ── Pass 2: user folder + one level of subcategory subfolders ──────────────
    auto userFolder = getIRFolder();
    if (userFolder.isDirectory())
    {
        addFilesFromDir (userFolder, {}, false);

        juce::Array<juce::File> userSubDirs;
        userFolder.findChildFiles (userSubDirs, juce::File::findDirectories, false);
        userSubDirs.sort();
        for (const auto& sub : userSubDirs)
            addFilesFromDir (sub, sub.getFileName(), false);
    }

    // ── Pass 3: drop aux multi-mic siblings when the MAIN base file is present ─
    for (int i = irEntries.size() - 1; i >= 0; --i)
    {
        const auto& e = irEntries.getReference (i);
        juce::String baseStem;
        if (endsWithAuxSuffix (e.file.getFileNameWithoutExtension(), baseStem)
            && hasBaseSiblingInSameDir (e.file, baseStem))
        {
            irEntries.remove (i);
        }
    }
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

juce::Array<IRManager::IREntry> IRManager::getEntries4Channel() const
{
    juce::Array<IREntry> out;
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    for (const auto& e : irEntries)
    {
        std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (e.file));
        if (r && r->numChannels == 4)
            out.add (e);
    }
    return out;
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
