#include "IRManager.h"

juce::File IRManager::getIRFolder()
{
    auto docs = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);
    return docs.getChildFile ("P!NG").getChildFile ("IRs");
}

void IRManager::scanFolder()
{
    irFiles.clear();
    auto folder = getIRFolder();
    if (! folder.exists() || ! folder.isDirectory())
        return;

    juce::StringArray extensions;
    extensions.add ("*.wav");
    extensions.add ("*.WAV");
    extensions.add ("*.aiff");
    extensions.add ("*.aif");
    extensions.add ("*.AIFF");
    extensions.add ("*.AIF");

    for (const auto& ext : extensions)
    {
        juce::Array<juce::File> found;
        folder.findChildFiles (found, juce::File::findFiles, false, ext);
        for (auto& f : found)
        {
            if (! irFiles.contains (f))
                irFiles.add (f);
        }
    }

    irFiles.sort();
}

void IRManager::refresh()
{
    scanFolder();
}

juce::StringArray IRManager::getDisplayNames() const
{
    juce::StringArray names;
    for (const auto& f : irFiles)
        names.add (f.getFileNameWithoutExtension());
    return names;
}

juce::Array<juce::File> IRManager::getIRFiles() const
{
    return irFiles;
}

juce::File IRManager::getIRFileAt (int index) const
{
    if (juce::isPositiveAndBelow (index, irFiles.size()))
        return irFiles.getReference (index);
    return juce::File();
}
