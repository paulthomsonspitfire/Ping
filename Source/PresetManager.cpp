#include "PresetManager.h"

juce::File PresetManager::getPresetDirectory()
{
    auto dir = juce::File::getSpecialLocation (juce::File::userHomeDirectory)
                   .getChildFile ("Library")
                   .getChildFile ("Audio")
                   .getChildFile ("Presets")
                   .getChildFile ("Ping")
                   .getChildFile ("P!NG");
    if (! dir.exists())
        dir.createDirectory();
    return dir;
}

juce::StringArray PresetManager::getPresetNames()
{
    juce::StringArray names;
    auto dir = getPresetDirectory();
    auto files = dir.findChildFiles (juce::File::findFiles, false, "*.xml");
    for (auto& f : files)
        names.add (f.getFileNameWithoutExtension());
    names.sortNatural();
    return names;
}

juce::File PresetManager::getPresetFile (const juce::String& name)
{
    return getPresetDirectory().getChildFile (name + ".xml");
}
