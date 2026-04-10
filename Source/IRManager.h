#pragma once

#include <JuceHeader.h>

/**
 * Scans the IR folders and provides a structured list of impulse response files.
 *
 * Two locations are scanned in order:
 *   1. /Library/Application Support/Ping/Factory IRs/        (system-wide, read-only)
 *      Immediate subdirectories become named categories shown as section headings in the UI.
 *   2. ~/Library/Audio/Impulse Responses/Ping/               (per-user, flat)
 *
 * Results are exposed as a flat indexed array of IREntry structs (factory entries first,
 * then user entries) via getEntries(). All legacy methods (getDisplayNames, getIRFileAt, etc.)
 * remain unchanged and iterate over the same underlying array.
 */
class IRManager
{
public:
    /** A single IR file with its source and category metadata. */
    struct IREntry
    {
        juce::File   file;
        juce::String category;    // subfolder name shown as heading, e.g. "Halls"; empty = no heading
        bool         isFactory = false;
    };

    IRManager() = default;

    /** Returns the per-user IR folder: ~/Library/Audio/Impulse Responses/Ping/ */
    static juce::File getIRFolder();

    /** Returns the system-wide factory IR folder:
        /Library/Application Support/Ping/Factory IRs/ */
    static juce::File getSystemFactoryIRFolder();

    /** Re-scans both the factory and user folders. Call when you want to refresh the list. */
    void refresh();

    /** Full structured entry list, factory entries first then user entries. */
    const juce::Array<IREntry>& getEntries() const { return irEntries; }

    /** Number of IRs currently found (factory + user). */
    int getNumIRs() const { return irEntries.size(); }

    /** Get the File for a given index (0-based, across factory + user). Returns invalid File if out of range. */
    juce::File getIRFileAt (int index) const;

    /** Returns display names (filename without extension) in entry order. */
    juce::StringArray getDisplayNames() const;

    /** Returns full File for each IR in entry order; indices match getDisplayNames(). */
    juce::Array<juce::File> getIRFiles() const;

    /** Display names for 4-channel IRs only. Indices match getIRFiles4Channel(). */
    juce::StringArray getDisplayNames4Channel() const;
    /** Files that have 4 channels. */
    juce::Array<juce::File> getIRFiles4Channel() const;
    /** Get file at index into the 4-channel subset. */
    juce::File getIRFileAt4Channel (int index) const;

private:
    juce::Array<IREntry> irEntries;

    void scanFolder();
};
