#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <sys/stat.h>

static void writeIRSynthParamsSidecar (const juce::File& wavFile, const IRSynthParams& p,
                                       const PingProcessor::MixerGateState* gates = nullptr);

namespace IDs
{
    static const juce::String dryWet   { "drywet" };
    static const juce::String predelay { "predelay" };
    static const juce::String decay    { "decay" };
    static const juce::String stretch  { "stretch" };
    static const juce::String width    { "width" };
    static const juce::String modDepth { "moddepth" };
    static const juce::String modRate  { "modrate" };
    static const juce::String tailMod  { "tailmod" };
    static const juce::String delayDepth{ "delaydepth" };
    static const juce::String tailRate { "tailrate" };
    static const juce::String inputGain { "inputGain" };
    static const juce::String outputGain { "outputGain" };
    static const juce::String irInputDrive { "irInputDrive" };
    static const juce::String erLevel { "erLevel" };
    static const juce::String tailLevel { "tailLevel" };
    static const juce::String reverseTrim { "reversetrim" };
    static const juce::String band0Freq { "b0freq" }, band0Gain { "b0gain" }, band0Q { "b0q" };
    static const juce::String band1Freq { "b1freq" }, band1Gain { "b1gain" }, band1Q { "b1q" };
    static const juce::String band2Freq { "b2freq" }, band2Gain { "b2gain" }, band2Q { "b2q" };
    // b3 = Low Shelf, b4 = High Shelf
    static const juce::String band3Freq { "b3freq" }, band3Gain { "b3gain" }, band3Q { "b3q" };
    static const juce::String band4Freq { "b4freq" }, band4Gain { "b4gain" }, band4Q { "b4q" };
    static const juce::String erCrossfeedOn { "erCrossfeedOn" };
    static const juce::String erCrossfeedDelayMs { "erCrossfeedDelayMs" };
    static const juce::String erCrossfeedAttDb { "erCrossfeedAttDb" };
    static const juce::String tailCrossfeedOn { "tailCrossfeedOn" };
    static const juce::String tailCrossfeedDelayMs { "tailCrossfeedDelayMs" };
    static const juce::String tailCrossfeedAttDb { "tailCrossfeedAttDb" };
    // Plate onset
    static const juce::String plateOn         { "plateOn" };
    static const juce::String plateDiffusion  { "plateDiffusion" };
    static const juce::String plateColour     { "plateColour" };
    static const juce::String plateSize       { "plateSize" };
    static const juce::String plateIRFeed     { "plateIRFeed" };
    // Bloom hybrid
    static const juce::String bloomOn         { "bloomOn" };
    static const juce::String bloomSize       { "bloomSize" };
    static const juce::String bloomFeedback   { "bloomFeedback" };
    static const juce::String bloomTime       { "bloomTime" };
    static const juce::String bloomIRFeed     { "bloomIRFeed" };
    static const juce::String bloomVolume     { "bloomVolume" };
    // Cloud Multi-LFO
    static const juce::String cloudOn         { "cloudOn" };
    static const juce::String cloudDepth      { "cloudDepth" };
    static const juce::String cloudRate       { "cloudRate" };
    static const juce::String cloudSize       { "cloudSize" };
    static const juce::String cloudVolume     { "cloudVolume" };
    static const juce::String cloudFeedback   { "cloudFeedback" };
    static const juce::String cloudIRFeed     { "cloudIRFeed" };
    static const juce::String shimOn          { "shimOn" };
    static const juce::String shimPitch       { "shimPitch" };
    static const juce::String shimSize        { "shimSize" };
    static const juce::String shimDelay       { "shimDelay" };
    static const juce::String shimIRFeed      { "shimIRFeed" };
    static const juce::String shimVolume      { "shimVolume" };
    static const juce::String shimFeedback    { "shimFeedback" };

    // ── Multi-mic mixer (feature/multi-mic-paths) ────────────────────────────
    // 4 strips × 6 params each. Gains in dB (-48..+6), pans constant-power
    // (-1..+1). "On" toggles the path DSP; "Mute"/"Solo"/"HPOn" per strip.
    // Defaults per brief §2a: MAIN on, DIRECT/OUTRIG/AMBIENT off; HP off on
    // DIRECT+MAIN, on by default on OUTRIG+AMBIENT (where low-mid rumble is
    // typical in ambient-mic placements).
    // IMPORTANT: none of these parameters trigger loadSelectedIR() — only
    // "stretch" and "decay" do. Adding new param listeners here would invite
    // the triple-load / NUPC-overload regression documented in CLAUDE.md.
    static const juce::String mainOn      { "mainOn" };
    static const juce::String mainGain    { "mainGain" };
    static const juce::String mainPan     { "mainPan" };
    static const juce::String mainMute    { "mainMute" };
    static const juce::String mainSolo    { "mainSolo" };
    static const juce::String mainHPOn    { "mainHPOn" };

    static const juce::String directOn    { "directOn" };
    static const juce::String directGain  { "directGain" };
    static const juce::String directPan   { "directPan" };
    static const juce::String directMute  { "directMute" };
    static const juce::String directSolo  { "directSolo" };
    static const juce::String directHPOn  { "directHPOn" };

    static const juce::String outrigOn    { "outrigOn" };
    static const juce::String outrigGain  { "outrigGain" };
    static const juce::String outrigPan   { "outrigPan" };
    static const juce::String outrigMute  { "outrigMute" };
    static const juce::String outrigSolo  { "outrigSolo" };
    static const juce::String outrigHPOn  { "outrigHPOn" };

    static const juce::String ambientOn   { "ambientOn" };
    static const juce::String ambientGain { "ambientGain" };
    static const juce::String ambientPan  { "ambientPan" };
    static const juce::String ambientMute { "ambientMute" };
    static const juce::String ambientSolo { "ambientSolo" };
    static const juce::String ambientHPOn { "ambientHPOn" };
}

// New canonical licence directory: /Library/Application Support/Audio/Ping/
static juce::File getLicenceDirectory()
{
    return juce::File::getSpecialLocation (juce::File::commonApplicationDataDirectory)
               .getChildFile ("Audio")
               .getChildFile ("Ping");
}

// New canonical user licence directory: ~/Library/Audio/Ping/
static juce::File getUserLicenceDirectory()
{
    return juce::File::getSpecialLocation (juce::File::userHomeDirectory)
               .getChildFile ("Library")
               .getChildFile ("Audio")
               .getChildFile ("Ping");
}

// Attempt to migrate a licence file from an old P!NG-nested path to the new flat path.
// Returns true if the file is now available at newFile (either already there or migrated).
// Best-effort: if the copy or delete fails (e.g. no write permission on system paths)
// we leave the old file in place and return false so the caller can still use it.
static bool migrateLicenceFile (const juce::File& oldFile, const juce::File& newFile)
{
    if (! oldFile.existsAsFile())
        return false;
    if (newFile.existsAsFile())
        return true;  // already migrated by another user session
    if (! newFile.getParentDirectory().createDirectory().wasOk())
        return false;
    if (! oldFile.copyFileTo (newFile))
        return false;
    oldFile.deleteFile();
    // Remove the old P!NG directory if it is now empty
    auto oldDir = oldFile.getParentDirectory();
    if (oldDir.getNumberOfChildFiles (juce::File::findFilesAndDirectories) == 0)
        oldDir.deleteFile();
    return true;
}

static juce::File getLicenceFile()
{
    // Canonical new paths (no P!NG subfolder)
    auto newUser   = getUserLicenceDirectory().getChildFile ("licence.xml");
    auto newSystem = getLicenceDirectory()    .getChildFile ("licence.xml");

    // Legacy paths (P!NG subfolder, pre-2.3.8)
    auto oldUser   = juce::File::getSpecialLocation (juce::File::userHomeDirectory)
                         .getChildFile ("Library").getChildFile ("Audio")
                         .getChildFile ("Ping").getChildFile ("P!NG")
                         .getChildFile ("licence.xml");
    auto oldSystem = juce::File::getSpecialLocation (juce::File::commonApplicationDataDirectory)
                         .getChildFile ("Audio").getChildFile ("Ping").getChildFile ("P!NG")
                         .getChildFile ("licence.xml");

    // 1. New user path — canonical location for new installs
    if (newUser.existsAsFile())   return newUser;

    // 2. New system path
    if (newSystem.existsAsFile()) return newSystem;

    // 3. Old user path — migrate silently to new location
    if (oldUser.existsAsFile())
    {
        migrateLicenceFile (oldUser, newUser);
        return newUser.existsAsFile() ? newUser : oldUser;
    }

    // 4. Old system path — best-effort migration (may lack write permission)
    if (oldSystem.existsAsFile())
    {
        migrateLicenceFile (oldSystem, newSystem);
        return newSystem.existsAsFile() ? newSystem : oldSystem;
    }

    // No licence found anywhere — return new user path as the target for activation
    return newUser;
}

static bool looksLikeDate (const std::string& s)
{
    if (s.size() != 10) return false;
    return s[4] == '-' && s[7] == '-' && std::isdigit ((unsigned char) s[0]) && std::isdigit ((unsigned char) s[1]);
}

/** Convert digit char to 0-9 (handles ASCII and fullwidth Unicode ０-９ U+FF10-U+FF19). */
static int digitValue (juce::juce_wchar c)
{
    if (c >= '0' && c <= '9') return (int) (c - '0');
    if (c >= 0xFF10 && c <= 0xFF19) return (int) (c - 0xFF10);
    return -1;
}

/** Decode ASCII decimal codes (e.g. "8097117108 84104111109115111110" -> "Paul Thomson"). */
static juce::String decodeDecimalAscii (const juce::String& s)
{
    if (s.isEmpty()) return {};
    auto t = s.trim();
    bool allDigitsOrSpaces = true;
    for (int i = 0; i < t.length(); ++i)
    {
        int d = digitValue (t[i]);
        if (t[i] != ' ' && t[i] != '\t' && d < 0)
        {
            allDigitsOrSpaces = false;
            break;
        }
    }
    if (! allDigitsOrSpaces)
        return s;

    juce::String decoded;
    int i = 0;
    while (i < t.length())
    {
        while (i < t.length() && (t[i] == ' ' || t[i] == '\t')) { decoded += ' '; ++i; }
        if (i >= t.length()) break;

        int d0 = digitValue (t[i]);
        if (d0 < 0) { ++i; continue; }

        int code = d0, n = 1;
        if (i + 1 < t.length())
        {
            int d1 = digitValue (t[i + 1]);
            if (d1 >= 0)
            {
                int code2 = code * 10 + d1;
                if (i + 2 < t.length())
                {
                    int d2 = digitValue (t[i + 2]);
                    if (d2 >= 0)
                    {
                        int code3 = code2 * 10 + d2;
                        if (code3 >= 32 && code3 <= 255)
                        {
                            code = code3;
                            n = 3;
                        }
                        else if (code2 >= 32 && code2 <= 255)
                        {
                            code = code2;
                            n = 2;
                        }
                    }
                    else if (code2 >= 32 && code2 <= 255)
                    {
                        code = code2;
                        n = 2;
                    }
                }
                else if (code2 >= 32 && code2 <= 255)
                {
                    code = code2;
                    n = 2;
                }
            }
        }
        else if (code >= 32 && code <= 255)
        {
            n = 1;
        }

        if (code >= 32 && code <= 255)
            decoded += (juce::juce_wchar) (unsigned char) code;
        i += n;
    }
    return decoded.length() >= 2 ? decoded : s;
}

/** Persist current licence to disk.
 *  Tries /Library/Application Support/Audio/Ping/ (system-wide) first;
 *  falls back to ~/Library/Audio/Ping/ (per-user) if that directory can't be created. */
static void persistLicenceToFile (const std::string& normalisedName, const juce::String& serial,
                                  const juce::String& displayName)
{
    auto dir = getLicenceDirectory();     // new system path
    if (! dir.createDirectory().wasOk())
    {
        dir = getUserLicenceDirectory();  // new user path
        if (! dir.createDirectory().wasOk())
            return;
    }
    auto file = dir.getChildFile ("licence.xml");
    if (auto xml = std::make_unique<juce::XmlElement> ("Licence"))
    {
        xml->setAttribute ("name", normalisedName);
        xml->setAttribute ("serial", serial);
        juce::String toStore = displayName.isNotEmpty() ? decodeDecimalAscii (displayName) : juce::String();
        if (toStore.isEmpty() && displayName.isNotEmpty())
            toStore = displayName;
        if (toStore.isNotEmpty())
            xml->setAttribute ("displayName", toStore);
        xml->writeTo (file);
    }
}

PingProcessor::PingProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input",  juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "Parameters", createParameterLayout())
{
    for (auto* param : getParameters())
        param->addListener (this);
    irManager.refresh();
    loadStoredLicence();
}

PingProcessor::~PingProcessor()
{
    for (auto* param : getParameters())
        param->removeListener (this);
}

void PingProcessor::parameterValueChanged (int, float)
{
    if (! isRestoringState.load())
        presetDirty.store (true);
}

void PingProcessor::snapshotCleanState()
{
    const auto& params = getParameters();
    cleanParamSnapshot.resize ((size_t) params.size());
    for (int i = 0; i < params.size(); ++i)
        cleanParamSnapshot[(size_t) i] = params[i]->getValue();
    presetDirty.store (false);
}

bool PingProcessor::hasParameterChangedSinceSnapshot() const
{
    const auto& params = getParameters();
    if (cleanParamSnapshot.size() != (size_t) params.size())
        return false;
    for (int i = 0; i < params.size(); ++i)
        if (std::abs (params[i]->getValue() - cleanParamSnapshot[(size_t) i]) > 1e-6f)
            return true;
    return false;
}

juce::AudioProcessorValueTreeState::ParameterLayout PingProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add (std::make_unique<juce::AudioParameterFloat> (IDs::dryWet,   "Dry / Wet",  0.0f, 1.0f, 0.3f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (IDs::predelay, "Predelay (ms)", 0.0f, 500.0f, 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (IDs::decay,    "Damping", 0.0f, 1.0f, 1.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (IDs::stretch,  "Stretch", 0.5f, 2.0f, 1.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (IDs::width,   "Width", 0.0f, 2.0f, 1.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (IDs::modDepth, "LFO Depth", 0.0f, 1.0f, 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (IDs::modRate, "LFO Rate", 0.01f, 2.0f, 0.5f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (IDs::tailMod,  "Tail Modulation", 0.0f, 1.0f, 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (IDs::delayDepth, "Delay Depth (ms)", 0.5f, 8.0f, 2.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (IDs::tailRate, "Tail Rate (Hz)", 0.05f, 3.0f, 0.5f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (IDs::inputGain, "IR Input Gain (dB)", -24.0f, 12.0f, 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (IDs::outputGain, "Wet Output (dB)", -24.0f, 12.0f, 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (IDs::irInputDrive, "IR Input Drive", 0.0f, 1.0f, 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (IDs::erLevel, "Early Reflections",
        juce::NormalisableRange<float> (-48.0f, 6.0f, 0.1f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (IDs::tailLevel, "Tail",
        juce::NormalisableRange<float> (-48.0f, 6.0f, 0.1f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (IDs::reverseTrim, "Reverse Trim", 0.0f, 0.95f, 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (IDs::band0Freq, "Band 0 Freq (Hz)", 20.0f, 20000.0f, 220.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (IDs::band0Gain, "Band 0 Gain (dB)", -12.0f, 12.0f, 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (IDs::band0Q, "Band 0 Q", 0.3f, 10.0f, 0.707f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (IDs::band1Freq, "Band 1 Freq (Hz)", 20.0f, 20000.0f, 1600.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (IDs::band1Gain, "Band 1 Gain (dB)", -12.0f, 12.0f, 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (IDs::band1Q, "Band 1 Q", 0.3f, 10.0f, 0.707f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (IDs::band2Freq, "Band 2 Freq (Hz)", 20.0f, 20000.0f, 4800.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (IDs::band2Gain, "Band 2 Gain (dB)", -12.0f, 12.0f, 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (IDs::band2Q, "Band 2 Q", 0.3f, 10.0f, 0.707f));
    // Low shelf (b3) and High shelf (b4)
    layout.add (std::make_unique<juce::AudioParameterFloat> (IDs::band3Freq, "Low Shelf Freq (Hz)", 20.0f, 1200.0f, 80.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (IDs::band3Gain, "Low Shelf Gain (dB)", -12.0f, 12.0f, 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (IDs::band3Q, "Low Shelf Slope", 0.3f, 2.0f, 0.707f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (IDs::band4Freq, "High Shelf Freq (Hz)", 2000.0f, 20000.0f, 12000.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (IDs::band4Gain, "High Shelf Gain (dB)", -12.0f, 12.0f, 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (IDs::band4Q, "High Shelf Slope", 0.3f, 2.0f, 0.707f));
    layout.add (std::make_unique<juce::AudioParameterBool> (IDs::erCrossfeedOn, "ER Crossfeed On", false));
    layout.add (std::make_unique<juce::AudioParameterFloat> (IDs::erCrossfeedDelayMs, "ER Crossfeed Delay (ms)", 5.0f, 15.0f, 10.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (IDs::erCrossfeedAttDb, "ER Crossfeed Att (dB)", -24.0f, 0.0f, -6.0f));
    layout.add (std::make_unique<juce::AudioParameterBool> (IDs::tailCrossfeedOn, "Tail Crossfeed On", false));
    layout.add (std::make_unique<juce::AudioParameterFloat> (IDs::tailCrossfeedDelayMs, "Tail Crossfeed Delay (ms)", 5.0f, 15.0f, 10.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (IDs::tailCrossfeedAttDb, "Tail Crossfeed Att (dB)", -24.0f, 0.0f, -6.0f));
    // Plate onset
    layout.add (std::make_unique<juce::AudioParameterBool>  (IDs::plateOn,        "Plate On",         false));
    layout.add (std::make_unique<juce::AudioParameterFloat> (IDs::plateDiffusion, "Plate Diffusion",  0.30f, 0.88f, 0.50f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (IDs::plateColour,    "Plate Colour",     0.0f, 1.0f,  1.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (IDs::plateSize,      "Plate Size",       0.5f, 14.0f, 6.27f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (IDs::plateIRFeed,    "Plate IR Feed",    0.0f, 1.0f,  0.47f));
    // Bloom hybrid
    layout.add (std::make_unique<juce::AudioParameterBool>  (IDs::bloomOn,        "Bloom On",         false));
    layout.add (std::make_unique<juce::AudioParameterFloat> (IDs::bloomSize,      "Bloom Size",       0.25f, 2.0f, 0.77f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (IDs::bloomFeedback,  "Bloom Feedback",   0.0f, 0.65f, 0.49f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (IDs::bloomTime,      "Bloom Time",       50.0f, 500.0f, 290.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (IDs::bloomIRFeed,    "Bloom IR Feed",    0.0f, 1.0f,   0.4f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (IDs::bloomVolume,    "Bloom Volume",     0.0f, 1.0f,   0.0f));
    // Cloud Multi-LFO
    layout.add (std::make_unique<juce::AudioParameterBool>  (IDs::cloudOn,     "Cloud On",      false));
    layout.add (std::make_unique<juce::AudioParameterFloat> (IDs::cloudDepth,    "Cloud Width",    0.0f,  1.0f,  0.3f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (IDs::cloudRate,     "Cloud Density",  0.1f,  4.0f,  2.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (IDs::cloudSize,     "Cloud Length",   25.0f, 1000.0f, 200.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (IDs::cloudVolume,   "Cloud Volume",   0.0f,  1.0f,  0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (IDs::cloudFeedback, "Cloud Feedback", 0.0f,  0.7f,  0.3f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (IDs::cloudIRFeed,   "Cloud IR Feed",  0.0f,  1.0f,  0.5f));
    // Shimmer
    layout.add (std::make_unique<juce::AudioParameterBool>  (IDs::shimOn,     "Shimmer On",      false));
    layout.add (std::make_unique<juce::AudioParameterFloat> (IDs::shimPitch,  "Shimmer Pitch",
                    juce::NormalisableRange<float> (-24.f, 24.f, 1.f), 12.f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (IDs::shimSize,  "Shimmer Grain",  50.0f,  500.0f, 300.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (IDs::shimDelay, "Shimmer Delay",   0.0f, 1000.0f, 500.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (IDs::shimIRFeed,   "Shimmer IR Feed",   0.0f, 1.0f, 0.5f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (IDs::shimVolume,   "Shimmer Volume",    0.0f, 1.0f, 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (IDs::shimFeedback, "Shimmer Feedback",  0.0f, 0.7f, 0.45f));

    // ── Multi-mic mixer (feature/multi-mic-paths) ────────────────────────────
    // Per brief §2a. Gains use the same range as erLevel / tailLevel
    // (-48..+6 dB, 0.1 step). Pans are constant-power (-1..+1, step 0.001).
    // Defaults follow the brief: MAIN on, extras off; HP off on DIRECT+MAIN,
    // on by default on OUTRIG+AMBIENT.
    const juce::NormalisableRange<float> gainRange (-48.0f, 6.0f, 0.1f);
    const juce::NormalisableRange<float> panRange  (-1.0f, 1.0f, 0.001f);

    // MAIN
    layout.add (std::make_unique<juce::AudioParameterBool>  (IDs::mainOn,    "MAIN On",    true));
    layout.add (std::make_unique<juce::AudioParameterFloat> (IDs::mainGain,  "MAIN Gain (dB)", gainRange, 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (IDs::mainPan,   "MAIN Pan",       panRange,  0.0f));
    layout.add (std::make_unique<juce::AudioParameterBool>  (IDs::mainMute,  "MAIN Mute",  false));
    layout.add (std::make_unique<juce::AudioParameterBool>  (IDs::mainSolo,  "MAIN Solo",  false));
    layout.add (std::make_unique<juce::AudioParameterBool>  (IDs::mainHPOn,  "MAIN HP On", false));

    // DIRECT (order-0 only — direct arrivals, no reflections, no FDN)
    layout.add (std::make_unique<juce::AudioParameterBool>  (IDs::directOn,    "DIRECT On",    false));
    layout.add (std::make_unique<juce::AudioParameterFloat> (IDs::directGain,  "DIRECT Gain (dB)", gainRange, 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (IDs::directPan,   "DIRECT Pan",       panRange,  0.0f));
    layout.add (std::make_unique<juce::AudioParameterBool>  (IDs::directMute,  "DIRECT Mute",  false));
    layout.add (std::make_unique<juce::AudioParameterBool>  (IDs::directSolo,  "DIRECT Solo",  false));
    layout.add (std::make_unique<juce::AudioParameterBool>  (IDs::directHPOn,  "DIRECT HP On", false));

    // OUTRIG (wider stereo pair, full ER + Tail)
    layout.add (std::make_unique<juce::AudioParameterBool>  (IDs::outrigOn,    "OUTRIG On",    false));
    layout.add (std::make_unique<juce::AudioParameterFloat> (IDs::outrigGain,  "OUTRIG Gain (dB)", gainRange, 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (IDs::outrigPan,   "OUTRIG Pan",       panRange,  0.0f));
    layout.add (std::make_unique<juce::AudioParameterBool>  (IDs::outrigMute,  "OUTRIG Mute",  false));
    layout.add (std::make_unique<juce::AudioParameterBool>  (IDs::outrigSolo,  "OUTRIG Solo",  false));
    layout.add (std::make_unique<juce::AudioParameterBool>  (IDs::outrigHPOn,  "OUTRIG HP On", true));

    // AMBIENT (higher, further-back pair, full ER + Tail)
    layout.add (std::make_unique<juce::AudioParameterBool>  (IDs::ambientOn,    "AMBIENT On",    false));
    layout.add (std::make_unique<juce::AudioParameterFloat> (IDs::ambientGain,  "AMBIENT Gain (dB)", gainRange, 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (IDs::ambientPan,   "AMBIENT Pan",       panRange,  0.0f));
    layout.add (std::make_unique<juce::AudioParameterBool>  (IDs::ambientMute,  "AMBIENT Mute",  false));
    layout.add (std::make_unique<juce::AudioParameterBool>  (IDs::ambientSolo,  "AMBIENT Solo",  false));
    layout.add (std::make_unique<juce::AudioParameterBool>  (IDs::ambientHPOn,  "AMBIENT HP On", true));

    return layout;
}

void PingProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = (juce::uint32) samplesPerBlock;
    spec.numChannels = 2;

    spec.numChannels = 1;  // True stereo convolvers are mono
    tsErConvLL.reset(); tsErConvRL.reset(); tsErConvLR.reset(); tsErConvRR.reset();
    tsTailConvLL.reset(); tsTailConvRL.reset(); tsTailConvLR.reset(); tsTailConvRR.reset();
    tsErConvLL.prepare (spec); tsErConvRL.prepare (spec); tsErConvLR.prepare (spec); tsErConvRR.prepare (spec);
    tsTailConvLL.prepare (spec); tsTailConvRL.prepare (spec); tsTailConvLR.prepare (spec); tsTailConvRR.prepare (spec);

    // Multi-mic path convolvers (feature/multi-mic-paths) — reset + prepare with the same spec.
    tsDirectConvLL.reset();   tsDirectConvRL.reset();   tsDirectConvLR.reset();   tsDirectConvRR.reset();
    tsDirectConvLL.prepare (spec); tsDirectConvRL.prepare (spec); tsDirectConvLR.prepare (spec); tsDirectConvRR.prepare (spec);
    tsOutrigErConvLL.reset();   tsOutrigErConvRL.reset();   tsOutrigErConvLR.reset();   tsOutrigErConvRR.reset();
    tsOutrigTailConvLL.reset(); tsOutrigTailConvRL.reset(); tsOutrigTailConvLR.reset(); tsOutrigTailConvRR.reset();
    tsOutrigErConvLL.prepare (spec);   tsOutrigErConvRL.prepare (spec);   tsOutrigErConvLR.prepare (spec);   tsOutrigErConvRR.prepare (spec);
    tsOutrigTailConvLL.prepare (spec); tsOutrigTailConvRL.prepare (spec); tsOutrigTailConvLR.prepare (spec); tsOutrigTailConvRR.prepare (spec);
    tsAmbErConvLL.reset();   tsAmbErConvRL.reset();   tsAmbErConvLR.reset();   tsAmbErConvRR.reset();
    tsAmbTailConvLL.reset(); tsAmbTailConvRL.reset(); tsAmbTailConvLR.reset(); tsAmbTailConvRR.reset();
    tsAmbErConvLL.prepare (spec);   tsAmbErConvRL.prepare (spec);   tsAmbErConvLR.prepare (spec);   tsAmbErConvRR.prepare (spec);
    tsAmbTailConvLL.prepare (spec); tsAmbTailConvRL.prepare (spec); tsAmbTailConvLR.prepare (spec); tsAmbTailConvRR.prepare (spec);
    spec.numChannels = 2;
    tailConvolver.reset();
    tailConvolver.prepare (spec);

    // All convolvers were just reset + re-prepared — they are back to unity pass-through
    // until loadImpulseResponse fires again via the callAsync posted at the end of this
    // function. Clear the per-path IR-loaded gates so processBlock's MAIN / Direct /
    // Outrig / Ambient strips don't feed a full-level pass-through signal into the wet
    // bus during the window between here and the async IR reload completing. Each flag
    // will be re-asserted by its corresponding loadIRFromBuffer call, which also arms
    // irLoadFadeSamplesRemaining for the wet fade-in that masks the NUPC partial-swap.
    // Without this, a re-prepare (sample-rate change, PDC recompute, Logic track-switch
    // after transport stop) would produce a brief loud burst of unfiltered sum-of-LR
    // audio instead of the silent-wet + 1 s fade-in the initial-load path provides.
    mainIRLoaded   .store (false);
    directIRLoaded .store (false);
    outrigIRLoaded .store (false);
    ambientIRLoaded.store (false);

    // Mirror the *IRLoaded reset above for the IR Synth panel display strings —
    // the deferred callAsync at the end of prepareToPlay will repopulate them
    // through the load path that re-arms the convolvers.
    for (auto& s : pathDisplayName) s = "<empty>";

    // Reset the per-path convolver-ready trackers too. After reset() the convolvers
    // are back to unity pass-through with no real engine installed, so their next
    // readiness transition (once the deferred loadIRFromBuffer fires from the
    // callAsync at the end of prepareToPlay) will correctly re-arm the wet fade.
    mainConvPrevReady   .store (false);
    directConvPrevReady .store (false);
    outrigConvPrevReady .store (false);
    ambientConvPrevReady.store (false);

    erLevelSmoothed.reset (sampleRate, 0.02);
    tailLevelSmoothed.reset (sampleRate, 0.02);

    // Per-path mixer strips — initialise current/target to the knob's linear gain so the
    // SmoothedValue does not create a 20 ms ramp on the very first processBlock call.
    // HP is fixed at 110 Hz 2nd-order Butterworth per the multi-mic brief (no cutoff knob).
    constexpr float kMicStripHPHz = 110.0f;
    auto initStrip = [this, sampleRate] (juce::SmoothedValue<float>& g,
                                          HP2ndOrder& hp,
                                          const juce::String& gainId)
    {
        g.reset (sampleRate, 0.02);
        const float gDb = apvts.getRawParameterValue (gainId)->load();
        g.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (gDb));
        hp.prepare (kMicStripHPHz, sampleRate);
        hp.reset();
        hp.enabled = false; // actual enabled flag is refreshed per-block in processBlock
    };
    initStrip (mainGainSmoothed,    mainHP,    IDs::mainGain);
    initStrip (directGainSmoothed,  directHP,  IDs::directGain);
    initStrip (outrigGainSmoothed,  outrigHP,  IDs::outrigGain);
    initStrip (ambientGainSmoothed, ambientHP, IDs::ambientGain);

    predelayLine.reset();
    predelayLine.setMaximumDelayInSamples ((int) (2.0 * sampleRate)); // up to 2 s
    predelayLine.prepare (spec);

    chorusDelayLine.reset();
    chorusDelayLine.setMaximumDelayInSamples ((int) (0.015 * sampleRate)); // up to 15 ms
    chorusDelayLine.prepare (spec);

    dryGain.reset();
    wetGain.reset();
    dryGain.prepare (spec);
    wetGain.prepare (spec);

    inputGainSmoothed.reset (sampleRate, 0.02);
    outputGainSmoothed.reset (sampleRate, 0.02);
    saturatorDriveSmoothed.reset (sampleRate, 0.02);

    updateEQ();
    lowShelfBand.prepare (spec);
    lowBand.prepare (spec);
    midBand.prepare (spec);
    highBand.prepare (spec);
    highShelfBand.prepare (spec);

    // Stereo decorrelation allpass (R channel only): 7.13 ms, 14.27 ms — incommensurate with FDN
    decorrDelays[0] = std::max (1, (int)std::round (7.13 * sampleRate / 1000.0));
    decorrDelays[1] = std::max (1, (int)std::round (14.27 * sampleRate / 1000.0));
    decorrBufs[0].resize ((size_t)decorrDelays[0], 0.0f);
    decorrBufs[1].resize ((size_t)decorrDelays[1], 0.0f);
    decorrPtrs[0] = 0;
    decorrPtrs[1] = 0;

    crossfeedMaxSamples = std::max (1, (int)std::ceil (0.015 * sampleRate));
    crossfeedErBufRtoL.resize ((size_t)crossfeedMaxSamples, 0.0f);
    crossfeedErBufLtoR.resize ((size_t)crossfeedMaxSamples, 0.0f);
    crossfeedTailBufRtoL.resize ((size_t)crossfeedMaxSamples, 0.0f);
    crossfeedTailBufLtoR.resize ((size_t)crossfeedMaxSamples, 0.0f);
    crossfeedErWriteRtoL = crossfeedErWriteLtoR = crossfeedTailWriteRtoL = crossfeedTailWriteLtoR = 0;

    // Plate onset: 6 allpass stages, prime delay times (at 48 kHz base).
    // Buffers allocated at 14× base primes so plateSize 0.5–14.0 needs no reallocation (~200 ms max on prime 691).
    {
        static constexpr int platePrimes[kNumPlateStages] = { 24, 71, 157, 293, 431, 691 };
        for (int ch = 0; ch < 2; ++ch)
            for (int s = 0; s < kNumPlateStages; ++s)
            {
                int d = (int)std::round (platePrimes[s] * sampleRate / 48000.0 * 14.0); // 14× headroom — supports plateSize up to 14.0 (~200 ms on prime 691)
                plateAPs[ch][s].buf.assign ((size_t)d, 0.f);
                plateAPs[ch][s].ptr    = 0;
                plateAPs[ch][s].effLen = 0;
                plateAPs[ch][s].g      = 0.40f;
            }
        plateShelfState.fill (0.f);
    }
    plateBuffer.setSize (2, samplesPerBlock);
    plateBuffer.clear();

    // Bloom hybrid: 6 allpass stages with separate L/R prime delay sets.
    // Incommensurate primes produce genuinely independent L/R textures after feedback cycles.
    // At 48 kHz: L {241,383,577,863,1297,1913} ≈ 5–40 ms, R {263,431,673,1049,1531,2111} ≈ 5.5–44 ms.
    // Buffers allocated at 2× base primes to cover bloomSize 0.25–2.0 without reallocation.
    // g = 0.35 hardcoded (transparent scatter). effLen set each processBlock via bloomSize.
    {
        static constexpr int bloomPrimesL[kNumBloomStages] = { 241,  383,  577,  863,  1297, 1913 };
        static constexpr int bloomPrimesR[kNumBloomStages] = { 263,  431,  673, 1049,  1531, 2111 };
        for (int ch = 0; ch < 2; ++ch)
        {
            const int* primes = (ch == 0) ? bloomPrimesL : bloomPrimesR;
            for (int s = 0; s < kNumBloomStages; ++s)
            {
                int d = (int)std::round (primes[s] * sampleRate / 48000.0 * 2.0); // 2× headroom for bloomSize up to 2.0
                bloomAPs[ch][s].buf.assign ((size_t)d, 0.f);
                bloomAPs[ch][s].ptr    = 0;
                bloomAPs[ch][s].effLen = 0;   // will be set each block via bloomSize
                bloomAPs[ch][s].g      = 0.35f; // hardcoded — transparent scatter
            }
        }
        int fbSamps = (int)std::ceil (kBloomFeedbackMaxMs * sampleRate / 1000.0);
        for (int ch = 0; ch < 2; ++ch)
        {
            bloomFbBufs[ch].assign ((size_t)fbSamps, 0.f);
            bloomFbWritePtrs[ch] = 0;
        }
    }
    bloomBuffer.setSize (2, samplesPerBlock);
    bloomBuffer.clear();

    // Cloud Granular Delay: 3-second capture buffer, variable-length grains.
    {
        const int capBufSamps = (int)std::ceil (kCloudCaptureBufMs * sampleRate / 1000.0);
        for (int ch = 0; ch < 2; ++ch)
        {
            cloudCaptureBufs[ch].assign ((size_t)capBufSamps, 0.f);
            cloudCaptureWritePtrs[ch] = 0;
        }
        for (auto& g : cloudGrains)
        {
            g.readPos  = 0.f;
            g.grainLen = 0;
            g.phase    = 1.f;   // inactive
            g.reverse  = false;
            g.srcCh    = -1;
        }
        cloudSpawnPhase                = 0.f;
        cloudCurrentSpawnIntervalSamps = 0.75f * (float)sampleRate; // 750 ms default
        cloudNextGrainSlot             = 0;
        cloudSpawnSeed                 = 12345u;
        cloudFbSamples.fill (0.f);
        cloudBuffer.setSize (2, samplesPerBlock);
        cloudBuffer.clear();

        // 4-stage all-pass diffusion cascade (Clouds TEXTURE-style grain-boundary smearing).
        // Delays are prime-number spaced and sub-15 ms to avoid audible echo.
        // Buffers are allocated exactly to the delay size (effLen=0 → uses buf.size()).
        static constexpr float kCloudDiffDelaysMs[kNumCloudDiffuseStages] = { 13.7f, 7.3f, 4.1f, 1.7f };
        for (int ch = 0; ch < 2; ++ch)
        {
            for (int s = 0; s < kNumCloudDiffuseStages; ++s)
            {
                const int delaySamps = juce::jmax (1,
                    (int)std::round (kCloudDiffDelaysMs[s] * sampleRate / 1000.0));
                cloudDiffuseAPs[ch][s].buf.assign ((size_t)delaySamps, 0.f);
                cloudDiffuseAPs[ch][s].ptr    = 0;
                cloudDiffuseAPs[ch][s].effLen = 0;  // use buf.size()
                cloudDiffuseAPs[ch][s].g      = 0.65f;
            }
        }
    }

    // Shimmer: 8-voice harmonic cloud — all voices read pre-conv dry, no loopback.
    {
        // Stage 0: base 7 ms, allocate at 2× to cover modulation sweep.
        // Stage 1: base 14 ms, allocate at 2×.
        const int ap0BufLen = juce::roundToInt (14.f * (float)sampleRate / 1000.f);
        const int ap1BufLen = juce::roundToInt (28.f * (float)sampleRate / 1000.f);
        const int ap0Base   = ap0BufLen / 2;  // 7 ms in samples
        const int ap1Base   = ap1BufLen / 2;  // 14 ms in samples

        const float lfoPhaseStep = juce::MathConstants<float>::twoPi / (float)kNumShimVoices;

        for (int v = 0; v < kNumShimVoices; ++v)
        {
            // Grain voices
            for (int ch = 0; ch < 2; ++ch)
            {
                auto& voice = shimVoicesHarm[v][ch];
                voice.grainBuf.assign (kShimBufLen, 0.f);
                voice.writePtr    = 0;
                voice.readPtrA    = 0.f;
                voice.readPtrB    = (float)(kShimGrainLen / 2);
                voice.grainPhaseA = 0.f;
                voice.grainPhaseB = 0.5f;

                // Allpass stage 0
                shimAPs[v][0][ch].buf.assign (ap0BufLen, 0.f);
                shimAPs[v][0][ch].ptr    = 0;
                shimAPs[v][0][ch].effLen = ap0Base;
                shimAPs[v][0][ch].g      = 0.5f;

                // Allpass stage 1
                shimAPs[v][1][ch].buf.assign (ap1BufLen, 0.f);
                shimAPs[v][1][ch].ptr    = 0;
                shimAPs[v][1][ch].effLen = ap1Base;
                shimAPs[v][1][ch].g      = 0.5f;
            }

            // LFO phases: spread 2π/8 = 45° apart per voice.
            // Allpass LFO uses an offset multiplier (1.3×) to decorrelate from main LFO.
            shimLfoPhase[v]   = (float)v * lfoPhaseStep;
            shimApLfoPhase[v] = (float)v * lfoPhaseStep * 1.3f;
        }
    }
    shimBuffer.setSize (2, samplesPerBlock);
    shimBuffer.clear();
    shimRng = 0x92d68ca2u;
    shimOnsetCounters.fill (0);
    shimWasEnabled = false;

    // Per-voice delay lines: max period is 1.6× the DELAY knob max (1000 ms),
    // so the ceiling is 1600 ms.  Allocate 1700 ms for a comfortable margin.
    // ~1700 ms × sr ≈ 81 600 samples per buffer at 48 kHz.
    // 8 voices × 2 ch × 81 600 × 4 bytes ≈ 5.2 MB total — still well within budget.
    const int maxDelayBufLen = juce::roundToInt (1700.f * (float)sampleRate / 1000.f) + 4;
    for (int v = 0; v < kNumShimVoices; ++v)
        for (int ch = 0; ch < 2; ++ch)
        {
            shimDelayBufs[v][ch].assign ((size_t)maxDelayBufLen, 0.f);
            shimDelayPtrs[v][ch] = 0;
        }

    updateGains();
    updatePredelay();
    updateEQ();

    // Mark the audio engine as prepared.  From this point on, setStateInformation (live preset
    // switch) will call loadImpulseResponse immediately.
    audioEnginePrepared.store (true);

    // Post a callAsync to load (or reload) the IR on the message thread AFTER this prepareToPlay
    // returns.  This is the deferred load triggered by setStateInformation's initial-load path;
    // it also handles the case where prepareToPlay is called again after a sample-rate change
    // (which resets all convolvers, losing the loaded IR).
    //
    // The lambda accesses irFromSynth / rawSynthBuffer / selectedIRFile on the message thread,
    // where they are always written, so there is no data race.
    juce::MessageManager::callAsync ([this]()
    {
        if (irFromSynth && rawSynthBuffer.getNumSamples() > 0)
            reloadSynthIR();
        else if (selectedIRFile.existsAsFile())
            loadIRFromFile (selectedIRFile);
        else if (! stateWasRestored.load())
        {
            auto defaultPreset = PresetManager::getSystemFactoryPresetFolder()
                                     .getChildFile ("Halls")
                                     .getChildFile ("Orch Beauty Med Hall.xml");
            if (defaultPreset.existsAsFile())
            {
                juce::MemoryBlock data;
                if (defaultPreset.loadFileAsData (data))
                {
                    setStateInformation (data.getData(), (int) data.getSize());
                    lastPresetName = "Orch Beauty Med Hall";
                    snapshotCleanState();
                }
            }
        }
    });
}

void PingProcessor::releaseResources() {}

void PingProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();
    if (numChannels < 2) return;

    if (! isLicensed())
    {
        buffer.clear();
        return;
    }

    updateGains();
    updatePredelay();
    updateEQ();

    // Peak follower helper — used throughout processBlock for all meters
    auto updatePeak = [this] (std::atomic<float>& peakStore, float newPeak)
    {
        float current = peakStore.load();
        if (newPeak > current)
            peakStore.store (newPeak);
        else
            peakStore.store (current * 0.95f);
    };

    // Dry copy
    juce::AudioBuffer<float> dryBuffer (numChannels, numSamples);
    dryBuffer.copyFrom (0, 0, buffer, 0, 0, numSamples);
    if (numChannels > 1)
        dryBuffer.copyFrom (1, 0, buffer, 1, 0, numSamples);

    // Input level metering — tap the raw input (pre-predelay, pre-processing)
    {
        float inPkL = 0.f, inPkR = 0.f;
        for (int i = 0; i < numSamples; ++i)
        {
            if (numChannels > 0) inPkL = juce::jmax (inPkL, std::abs (dryBuffer.getSample (0, i)));
            if (numChannels > 1) inPkR = juce::jmax (inPkR, std::abs (dryBuffer.getSample (1, i)));
        }
        updatePeak (inputLevelPeakL, inPkL);
        updatePeak (inputLevelPeakR, inPkR);
    }

    // Wet path: predelay -> convolution -> EQ -> width
    juce::dsp::AudioBlock<float> block (buffer);
    juce::dsp::ProcessContextReplacing<float> context (block);

    // Predelay
    for (int ch = 0; ch < numChannels; ++ch)
    {
        float* ptr = buffer.getWritePointer (ch);
        for (int i = 0; i < numSamples; ++i)
        {
            float in = ptr[i];
            predelayLine.pushSample (ch, in);
            ptr[i] = predelayLine.popSample (ch);
        }
    }

    // Input gain (wet path only, before saturator)
    float gainDb = apvts.getRawParameterValue (IDs::inputGain)->load();
    inputGainSmoothed.setTargetValue (juce::Decibels::decibelsToGain (gainDb));
    for (int i = 0; i < numSamples; ++i)
    {
        float g = inputGainSmoothed.getNextValue();
        for (int ch = 0; ch < numChannels; ++ch)
            buffer.setSample (ch, i, buffer.getSample (ch, i) * g);
    }

    // Harmonic saturator: cubic soft clip with mix and compensation
    float driveRaw = apvts.getRawParameterValue (IDs::irInputDrive)->load();
    saturatorDriveSmoothed.setTargetValue (driveRaw);
    const float inflection = 1.732050808f;  // sqrt(3), inflection point of x - x³/3
    for (int i = 0; i < numSamples; ++i)
    {
        float drive = saturatorDriveSmoothed.getNextValue();
        float mix = drive;
        float compensation = 1.0f / (1.0f + drive * 0.5f);
        float scale = 1.0f + drive * 3.0f;

        for (int ch = 0; ch < numChannels; ++ch)
        {
            float drySample = buffer.getSample (ch, i);
            float x = drySample * scale;
            x = juce::jlimit (-inflection, inflection, x);
            float saturated = x - (x * x * x / 3.0f);
            float out = drySample * (1.0f - mix) + saturated * mix;
            out *= compensation;
            buffer.setSample (ch, i, out);
        }
    }

    // ——————————————————————————————————————————————————————————————————
    // Plate onset: parallel allpass diffuser cascade
    //   Processes the post-saturator signal through 6 allpass stages + colour LP.
    //   plateBuffer stores the processed signal for two uses:
    //     1) IR FEED  — added to the convolver input (here, before convolution)
    //     2) VOLUME   — added directly to the wet bus (after convolution, before EQ)
    // ——————————————————————————————————————————————————————————————————
    if (apvts.getRawParameterValue (IDs::plateOn)->load() > 0.5f)
    {
        const float irFeed    = apvts.getRawParameterValue (IDs::plateIRFeed)->load();
        const float diffusion = apvts.getRawParameterValue (IDs::plateDiffusion)->load();
        const float colour    = apvts.getRawParameterValue (IDs::plateColour)->load();
        const float plateSz   = juce::jlimit (0.5f, 4.0f, apvts.getRawParameterValue (IDs::plateSize)->load());

        // colour → 1-pole lowpass cutoff: 0 → 2 kHz (warm/dark), 1 → 8 kHz (bright)
        const float cutHz      = 2000.f + colour * 6000.f;
        const float shelfAlpha = 1.f - std::exp (-2.f * juce::MathConstants<float>::pi * cutHz / (float)currentSampleRate);

        // Pre-compute effective delay lengths per stage (constant within this block)
        static constexpr int platePrimes[kNumPlateStages] = { 24, 71, 157, 293, 431, 691 };
        int stageLens[kNumPlateStages];
        for (int s = 0; s < kNumPlateStages; ++s)
            stageLens[s] = (int)std::round (platePrimes[s] * currentSampleRate / 48000.0 * (double)plateSz);

        // Set g coefficient from diffusion parameter — applies to all stages, both channels
        for (int ch = 0; ch < numChannels; ++ch)
            for (int s = 0; s < kNumPlateStages; ++s)
                plateAPs[ch][s].g = diffusion;

        // Compute plate signal and store in plateBuffer; add to convolver input via IR FEED
        for (int ch = 0; ch < numChannels; ++ch)
        {
            float* data  = buffer.getWritePointer (ch);
            float* plate = plateBuffer.getWritePointer (ch);

            for (int s = 0; s < kNumPlateStages; ++s)
                plateAPs[ch][s].effLen = stageLens[s];

            for (int i = 0; i < numSamples; ++i)
            {
                const float x = data[i];   // capture input before any modification
                float diff = x;
                for (int s = 0; s < kNumPlateStages; ++s)
                    diff = plateAPs[ch][s].process (diff);

                // 1-pole lowpass shapes colour: low cutoff = warm/dark, high cutoff = bright
                plateShelfState[ch] = plateShelfState[ch] + shelfAlpha * (diff - plateShelfState[ch]);
                plate[i] = plateShelfState[ch];   // store processed plate signal

                // Add plate signal into convolver input (IR FEED — additive on top of main signal)
                data[i] = x + plate[i] * irFeed;
            }
        }
    }
    else
    {
        // plateBuffer not needed this block — zero it so post-convolution injection is silent
        plateBuffer.clear();
    }

    // ——————————————————————————————————————————————————————————————————
    // Bloom hybrid: pre-convolution allpass cascade (self-contained feedback loop)
    //   bloomBuffer stores the cascade output for two uses:
    //     1) IR FEED  — added to the convolver input here (before convolution)
    //     2) VOLUME   — added to the final output after dry/wet blend (independent of wet/dry)
    //   Feedback tap: writes cascade output to bloomFbBufs within this same sample loop.
    //   The convolver output is NOT in the feedback path — Bloom is upstream of the reverb,
    //   like a guitar pedal feeding into a reverb unit with no return path.
    // ——————————————————————————————————————————————————————————————————
    if (apvts.getRawParameterValue (IDs::bloomOn)->load() > 0.5f)
    {
        const float bloomSz    = juce::jlimit (0.25f, 2.0f, apvts.getRawParameterValue (IDs::bloomSize)->load());
        const float fbAmt      = std::min (0.65f, apvts.getRawParameterValue (IDs::bloomFeedback)->load());
        const float bloomTimeMs = juce::jlimit (50.f, 500.f, apvts.getRawParameterValue (IDs::bloomTime)->load());
        const float irFeed     = apvts.getRawParameterValue (IDs::bloomIRFeed)->load();

        // Reallocate bloomBuffer if host delivers a larger-than-expected block
        if (numSamples > bloomBuffer.getNumSamples())
            bloomBuffer.setSize (2, numSamples, false, true, true);
        bloomBuffer.clear();

        // Separate L/R primes for stereo independence — same values as prepareToPlay
        static constexpr int bloomPrimesL[kNumBloomStages] = { 241,  383,  577,  863,  1297, 1913 };
        static constexpr int bloomPrimesR[kNumBloomStages] = { 263,  431,  673, 1049,  1531, 2111 };

        // Set effLen per channel/stage once per block using bloomSize (like Plate pattern)
        // g = 0.35f is hardcoded; no per-block write needed as it was set in prepareToPlay.
        for (int ch = 0; ch < numChannels; ++ch)
        {
            const int* primes = (ch == 0) ? bloomPrimesL : bloomPrimesR;
            for (int s = 0; s < kNumBloomStages; ++s)
                bloomAPs[ch][s].effLen = (int)std::round (primes[s] * currentSampleRate / 48000.0 * bloomSz);
        }

        for (int ch = 0; ch < numChannels; ++ch)
        {
            float* data  = buffer.getWritePointer (ch);
            float* bloom = bloomBuffer.getWritePointer (ch);
            const int fbLen = (int)bloomFbBufs[ch].size();
            const int timeInSamples = juce::jlimit (1, fbLen - 1,
                                          (int)std::round (bloomTimeMs * currentSampleRate / 1000.0));

            for (int i = 0; i < numSamples; ++i)
            {
                // Read feedback: bloomTime ms back in the cascade feedback buffer
                const int rdPtr = (bloomFbWritePtrs[ch] - timeInSamples + fbLen) % fbLen;
                const float fb  = bloomFbBufs[ch][(size_t)rdPtr] * fbAmt;

                // Run (input + feedback) through the 6-stage allpass cascade
                float diff = data[i] + fb;
                for (int s = 0; s < kNumBloomStages; ++s)
                    diff = bloomAPs[ch][s].process (diff);

                // Store cascade output — used for IR feed (here) and volume output (after dry/wet)
                bloom[i] = diff;

                // IR feed: add bloom output into the convolver input (additive, on top of main signal)
                data[i] += diff * irFeed;

                // Feedback tap: write cascade output into bloomFbBufs.
                // Using the cascade output (not the convolved wet) keeps Bloom's loop self-contained.
                bloomFbBufs[ch][(size_t)bloomFbWritePtrs[ch]] = diff;
                bloomFbWritePtrs[ch] = (bloomFbWritePtrs[ch] + 1) % fbLen;
            }
        }
    }
    else
    {
        // bloomBuffer not needed this block — zero it so post-convolution injection is silent
        if (numSamples <= bloomBuffer.getNumSamples())
            bloomBuffer.clear();
    }

    // ——————————————————————————————————————————————————————————————————
    // Cloud Granular Delay: variable-length Hann-windowed grains read from
    // a 3-second circular capture buffer of the dry input.
    //
    // DENSITY (cloudRate 0.1–4.0) controls spawn interval via exponential curve:
    //   t=0 (low)  → spawned every 205–410 ms  (0–1 grains at default LENGTH)
    //   t=0.5 (mid) → spawned every  33–67 ms  (~4 grains at 200 ms LENGTH)
    //   t=1 (high) → spawned every    9–18 ms  (~14 grains at 200 ms LENGTH)
    //   Formula: minMs = 200×0.02^t + 5, maxMs = 400×0.02^t + 10 (exponential)
    //
    // LENGTH (cloudSize) sets grain length directly in ms (25–1000 ms).
    // Grains read from random positions spread across the FULL 3-second buffer
    //   (not just 1–2 grain lengths behind write head), creating time-decorrelated
    //   textures rather than a phase-locked delay.
    // WIDTH (cloudDepth) controls stereo spread + reverse grain probability.
    // FEEDBACK (cloudFeedback) mixes grain output back into the capture buffer.
    // 4-stage all-pass diffusion (Clouds-style) applied to grain sum before output.
    // cloudVolume: added post-blend (applied below alongside bloomVolume).
    // ——————————————————————————————————————————————————————————————————
    if (apvts.getRawParameterValue (IDs::cloudOn)->load() > 0.5f)
    {
        const float cwidth    = juce::jlimit (0.f, 1.f,
                                    apvts.getRawParameterValue (IDs::cloudDepth)->load());
        const float crate     = juce::jlimit (0.1f, 4.0f,
                                    apvts.getRawParameterValue (IDs::cloudRate)->load());
        const float csize     = juce::jlimit (25.f, 1000.f,
                                    apvts.getRawParameterValue (IDs::cloudSize)->load());
        const float cfeedback = juce::jlimit (0.f, 0.7f,
                                    apvts.getRawParameterValue (IDs::cloudFeedback)->load());
        const float cirFeed   = apvts.getRawParameterValue (IDs::cloudIRFeed)->load();

        if (numSamples > cloudBuffer.getNumSamples())
            cloudBuffer.setSize (2, numSamples, false, true, true);
        cloudBuffer.clear();

        const int   capBufSamps = (int)cloudCaptureBufs[0].size();
        const float sr          = (float)currentSampleRate;

        // DENSITY → normalised t (0 = sparse, 1 = dense) — affects spawn interval only.
        // Exponential curve: mid-knob gives meaningful grain overlap; full range is useful.
        const float t = (crate - 0.1f) / (4.0f - 0.1f);

        // Grain length: directly from LENGTH knob (ms)
        const float grainLengthMs = csize;

        // Spawn interval window (ms) — exponential curve so overlap is useful across full knob:
        //   t=0  → 205–410 ms  (~0 grains overlapping at 200 ms LENGTH)
        //   t=0.5 → 33–67 ms  (~4 grains at 200 ms)
        //   t=1.0 →  9–18 ms  (~14 grains at 200 ms)
        const float tPow       = std::pow (0.02f, t);   // 1.0 at t=0 → 0.02 at t=1
        const float minSpawnMs = 200.f * tPow + 5.f;
        const float maxSpawnMs = 400.f * tPow + 10.f;

        for (int i = 0; i < numSamples; ++i)
        {
            // Write dry input + feedback into capture buffers
            for (int ch = 0; ch < numChannels; ++ch)
            {
                const float fb = (ch < 2) ? cloudFbSamples[(size_t)ch] * cfeedback : 0.f;
                cloudCaptureBufs[(size_t)ch][(size_t)cloudCaptureWritePtrs[ch]] =
                    buffer.getSample (ch, i) + fb;
                cloudCaptureWritePtrs[ch] =
                    (cloudCaptureWritePtrs[ch] + 1) % capBufSamps;
            }

            // Spawn new grain when the randomised interval elapses
            cloudSpawnPhase += 1.f / juce::jmax (1.f, cloudCurrentSpawnIntervalSamps);
            while (cloudSpawnPhase >= 1.f)
            {
                cloudSpawnPhase -= 1.f;

                // ── Grain length from LENGTH knob ────────────────────────────
                const int   grainLen = juce::jlimit (1,
                                           (int)(capBufSamps * 0.9f),
                                           (int)std::round (grainLengthMs * sr / 1000.f));

                // ── Random read position: spread across the full 3-second buffer ──────
                // Minimum lookback = grainLen (causality); maximum = 90% of buffer.
                // Scattering across the full history is the key difference from a delay:
                // grains from very different points in time overlap, creating spectral
                // richness and time-smear instead of phase-locked repetition.
                cloudSpawnSeed = cloudSpawnSeed * 1664525u + 1013904223u;
                const float r2          = (float)(cloudSpawnSeed >> 8) / (float)(1u << 24);
                const float minLookback = (float)grainLen;
                const float maxLookback = (float)capBufSamps * 0.9f;
                float startPos = (float)cloudCaptureWritePtrs[0]
                                 - (minLookback + r2 * (maxLookback - minLookback));
                while (startPos < 0.f) startPos += (float)capBufSamps;

                // ── Random direction (WIDTH drives reverse probability) ────────
                cloudSpawnSeed = cloudSpawnSeed * 1664525u + 1013904223u;
                const float r3 = (float)(cloudSpawnSeed >> 8) / (float)(1u << 24);
                const bool  rev = (r3 < cwidth * 0.5f);

                // If reversed, start at far end of grain window and read backwards
                float grainStartPos = startPos;
                if (rev)
                {
                    grainStartPos = startPos + (float)(grainLen - 1);
                    if (grainStartPos >= (float)capBufSamps)
                        grainStartPos -= (float)capBufSamps;
                }

                // ── Random source channel (WIDTH drives cross-channel sampling) ─
                cloudSpawnSeed = cloudSpawnSeed * 1664525u + 1013904223u;
                const float r4 = (float)(cloudSpawnSeed >> 8) / (float)(1u << 24);
                int srcCh = -1; // -1 = normal stereo
                if (numChannels > 1 && r4 < cwidth)
                {
                    cloudSpawnSeed = cloudSpawnSeed * 1664525u + 1013904223u;
                    const float r5 = (float)(cloudSpawnSeed >> 8) / (float)(1u << 24);
                    srcCh = (r5 < 0.5f) ? 0 : 1;
                }

                auto& g    = cloudGrains[(size_t)cloudNextGrainSlot];
                g.readPos  = grainStartPos;
                g.grainLen = grainLen;
                g.phase    = 0.f;
                g.reverse  = rev;
                g.srcCh    = srcCh;
                cloudNextGrainSlot = (cloudNextGrainSlot + 1) % kNumCloudGrains;

                // ── Random next spawn interval ────────────────────────────────
                cloudSpawnSeed = cloudSpawnSeed * 1664525u + 1013904223u;
                const float r6 = (float)(cloudSpawnSeed >> 8) / (float)(1u << 24);
                const float nextSpawnMs = minSpawnMs + r6 * (maxSpawnMs - minSpawnMs);
                cloudCurrentSpawnIntervalSamps = nextSpawnMs * sr / 1000.f;
            }

            // ── Sum active grains (Hann-windowed, normalised by active count) ──
            float grainSumL = 0.f, grainSumR = 0.f;
            int   activeCount = 0;

            for (int g = 0; g < kNumCloudGrains; ++g)
            {
                auto& grain = cloudGrains[(size_t)g];
                if (grain.phase >= 1.f) continue;

                const float win = 0.5f - 0.5f * std::cos (
                    juce::MathConstants<float>::twoPi * grain.phase);

                int         ri    = (int)std::floor (grain.readPos) % capBufSamps;
                if (ri < 0) ri   += capBufSamps;
                const float rfrac = grain.readPos - std::floor (grain.readPos);
                const int   ri1   = (ri + 1) % capBufSamps;

                // Channel source: normal = own channel; biased = fixed channel
                const int chL = (grain.srcCh >= 0) ? grain.srcCh : 0;
                const int chR = (grain.srcCh >= 0) ? grain.srcCh
                                                    : (numChannels > 1 ? 1 : 0);

                grainSumL += (cloudCaptureBufs[(size_t)chL][(size_t)ri]  * (1.f - rfrac)
                            + cloudCaptureBufs[(size_t)chL][(size_t)ri1] * rfrac) * win;
                if (numChannels > 1)
                    grainSumR += (cloudCaptureBufs[(size_t)chR][(size_t)ri]  * (1.f - rfrac)
                                + cloudCaptureBufs[(size_t)chR][(size_t)ri1] * rfrac) * win;

                // Advance read position (forward or reverse)
                if (grain.reverse)
                {
                    grain.readPos -= 1.f;
                    if (grain.readPos < 0.f) grain.readPos += (float)capBufSamps;
                }
                else
                {
                    grain.readPos += 1.f;
                    if (grain.readPos >= (float)capBufSamps)
                        grain.readPos -= (float)capBufSamps;
                }
                grain.phase += 1.f / (float)grain.grainLen;
                ++activeCount;
            }

            // sqrt(N) normalisation: maintains consistent perceived loudness as density changes
            // (1/N would make dense settings very quiet; sqrt(N) is the perceptually correct power law)
            const float scale = (activeCount > 0) ? 1.f / std::sqrt ((float)activeCount) : 0.f;
            float outL = grainSumL * scale;
            float outR = (numChannels > 1) ? grainSumR * scale : outL;

            // 4-stage all-pass diffusion (Clouds-style TEXTURE smearing).
            // Applied per-sample to grain sum before output; smears grain-boundary
            // discontinuities at signal level without adding reverb tail or latency artefacts.
            outL = cloudDiffuseAPs[0][0].process (outL);
            outL = cloudDiffuseAPs[0][1].process (outL);
            outL = cloudDiffuseAPs[0][2].process (outL);
            outL = cloudDiffuseAPs[0][3].process (outL);
            if (numChannels > 1)
            {
                outR = cloudDiffuseAPs[1][0].process (outR);
                outR = cloudDiffuseAPs[1][1].process (outR);
                outR = cloudDiffuseAPs[1][2].process (outR);
                outR = cloudDiffuseAPs[1][3].process (outR);
            }

            cloudBuffer.setSample (0, i, outL);
            if (numChannels > 1) cloudBuffer.setSample (1, i, outR);

            // Inject grain output into convolver input (one-way, no loop back)
            if (cirFeed > 0.f)
            {
                buffer.setSample (0, i, buffer.getSample (0, i) + outL * cirFeed);
                if (numChannels > 1)
                    buffer.setSample (1, i, buffer.getSample (1, i) + outR * cirFeed);
            }

            // Save grain output for feedback into next sample's capture write
            cloudFbSamples[0] = outL;
            cloudFbSamples[1] = (numChannels > 1) ? outR : outL;
        }
    }
    else
    {
        cloudBuffer.clear();
        cloudFbSamples.fill (0.f);
    }

    // ——————————————————————————————————————————————————————————————————
    // Shimmer: 8-voice harmonic cloud (no feedback loop).
    //
    // Every voice reads the CLEAN pre-conv dry signal and injects a
    // pitch-shifted, allpass-smeared copy into the convolver input × shimIRFeed.
    // The convolver IR provides all decay and repetition.
    //
    // Voice layout (shimPitch = N semitones):
    //   Voice 0:  0 st               — unshifted (body / unison reverb tail)
    //   Voice 1: +N st               — fundamental shimmer interval
    //   Voice 2: +2N st              — 2nd harmonic up
    //   Voice 3: −N st               — 1st harmonic down
    //   Voice 4: +3N st              — 3rd harmonic up
    //   Voice 5: −2N st              — 2nd harmonic down
    //   Voice 6:  0 st + 3 cents     — fixed-detune chorus double of voice 0
    //   Voice 7: +N st + 6 cents     — fixed-detune chorus double of voice 1
    //
    // shimFeedback (0–0.7) = LFO depth for per-voice grain delay:
    //   at 0   → all voices fixed at 300 ms delay
    //   at 0.7 → 0.5 Hz LFO sweeps per-voice delay 300–750 ms
    // Phases spread 45° per voice for de-correlation.
    // Each voice also has a 2-stage allpass (7 ms / 14 ms) slowly modulated at
    // ~0.2 Hz (independent phase offsets) for additional spectral wash.
    // ——————————————————————————————————————————————————————————————————
    {
        // Onset stagger: detect shimOn false→true transition and arm per-voice counters.
        // Voice vi waits (vi+1) × shimDelay ms before contributing any output.
        const bool shimIsOn = apvts.getRawParameterValue (IDs::shimOn)->load() > 0.5f;
        if (shimIsOn && !shimWasEnabled)
        {
            const int staggerSamps = juce::roundToInt (
                apvts.getRawParameterValue (IDs::shimDelay)->load()
                * (float)currentSampleRate / 1000.f);
            for (int v = 0; v < kNumShimVoices; ++v)
                shimOnsetCounters[v] = (v + 1) * staggerSamps;
        }
        shimWasEnabled = shimIsOn;
    }

    if (apvts.getRawParameterValue (IDs::shimOn)->load() > 0.5f)
    {
        const float shimPitchSt = apvts.getRawParameterValue (IDs::shimPitch)->load();
        const int   effGrainLen = juce::jmax (1, juce::roundToInt (
                                    apvts.getRawParameterValue (IDs::shimSize)->load()
                                    * (float)currentSampleRate / 1000.f));
        const float shimIRFd    = apvts.getRawParameterValue (IDs::shimIRFeed)->load();

        // FEEDBACK → decay time via exponential mapping: T = 2 × (15/2)^(raw/0.7)
        //   raw=0   → T≈2 s  |  raw=0.3 → T≈5.5 s  |  raw=0.7 → T=15 s
        const float feedbackRaw = apvts.getRawParameterValue (IDs::shimFeedback)->load();
        const float decayT      = 2.f * std::pow (15.f / 2.f, feedbackRaw / 0.7f);

        // DELAY knob in samples (raw; per-voice multipliers applied below).
        const int shimDelaySamps = juce::roundToInt (
            apvts.getRawParameterValue (IDs::shimDelay)->load()
            * (float)currentSampleRate / 1000.f);

        // Per-voice delay multipliers derived from 8 primes (2,3,5,7,11,13,17,19)
        // scaled linearly to [0.4, 1.6]: multiplier = 0.4 + (prime − 2) / 17 × 1.2.
        // Ratios between any pair are ratios of distinct primes — no common factors,
        // so echo recurrence periods between voices are extremely long and inaudible.
        // The multiplier applies only to the DELAY component (shimDelaySamps); at
        // DELAY=0 all voices still fall back to effGrainLen (no spread when delay is off).
        static constexpr float kShimVoiceMultiplier[kNumShimVoices] =
            { 0.400f, 0.471f, 0.612f, 0.753f, 1.035f, 1.176f, 1.459f, 1.600f };

        // Grain read delay: small fixed base (20 ms) + ±5 ms per-voice LFO sweep.
        // The old 300 ms base caused grains to miss any note shorter than ~500 ms.

        // LFO phase increments per sample.
        const float mainLfoInc = juce::MathConstants<float>::twoPi * 0.5f
                                   / (float)currentSampleRate;   // 0.5 Hz
        const float apLfoInc   = juce::MathConstants<float>::twoPi * 0.2f
                                   / (float)currentSampleRate;   // 0.2 Hz

        // Voice pitch layout.
        // semiOff: fixed additional offset in semitones (3 cents = 3/100 st, 6 cents = 6/100 st).
        static constexpr int   semiMult[kNumShimVoices] = { 0, 1, 2, -1, 3, -2, 0, 1 };
        static constexpr float semiOff[kNumShimVoices]  = { 0.f, 0.f, 0.f, 0.f, 0.f, 0.f,
                                                             3.f / 100.f, 6.f / 100.f };

        if (numSamples > shimBuffer.getNumSamples())
            shimBuffer.setSize (2, numSamples, false, true, true);
        shimBuffer.clear();

        auto hannW = [](float p) -> float {
            return 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * p);
        };

        // ── Per-voice setup: update allpass effLen from allpass LFO (once per block) ──
        for (int vi = 0; vi < kNumShimVoices; ++vi)
        {
            // Allpass LFO maps sin() ∈ [−1, 1] → delay sweep around base.
            // Stage 0: 7 ms ± 3 ms → range 4–10 ms.
            // Stage 1: 14 ms ± 5 ms → range 9–19 ms.
            const float apLv   = std::sin (shimApLfoPhase[vi]);
            const int   ap0Len = juce::roundToInt ((7.f + 3.f * apLv)
                                     * (float)currentSampleRate / 1000.f);
            const int   ap1Len = juce::roundToInt ((14.f + 5.f * apLv)
                                     * (float)currentSampleRate / 1000.f);
            for (int ch = 0; ch < numChannels; ++ch)
            {
                shimAPs[vi][0][ch].effLen = juce::jlimit (1,
                    (int)shimAPs[vi][0][ch].buf.size(), ap0Len);
                shimAPs[vi][1][ch].effLen = juce::jlimit (1,
                    (int)shimAPs[vi][1][ch].buf.size(), ap1Len);
            }
            // Advance allpass LFO by block duration.
            shimApLfoPhase[vi] += apLfoInc * (float)numSamples;
            if (shimApLfoPhase[vi] >= juce::MathConstants<float>::twoPi)
                shimApLfoPhase[vi] -= juce::MathConstants<float>::twoPi;
        }

        // ── Run all 8 voices, accumulating into shimBuffer ────────────────────
        for (int vi = 0; vi < kNumShimVoices; ++vi)
        {
            const float stTotal    = (float)semiMult[vi] * shimPitchSt + semiOff[vi];
            const float pitchRatio = std::pow (2.f, stTotal / 12.f);

            // Small fixed base (20 ms) + ±5 ms per-voice LFO for subtle chorus movement.
            // Grains read audio from ~220–225 ms ago at default grain size, so even a
            // short note immediately fills the buffer and all staggered voices have content.
            const float mainLv        = 0.5f * (1.f + std::sin (shimLfoPhase[vi]));  // [0,1]
            const int   voiceDelaySamps = juce::roundToInt ((20.f + 5.f * mainLv)
                                           * (float)currentSampleRate / 1000.f);

            // Per-voice delay period: apply prime-derived multiplier to the DELAY knob
            // value only.  At DELAY=0 voiceDelayPeriod=0 and the max() falls back to
            // effGrainLen — spreading is inactive when the knob is at zero.
            const int voiceDelayPeriod = juce::roundToInt (
                (float)shimDelaySamps * kShimVoiceMultiplier[vi]);
            const int voicePeriodSamps = juce::jlimit (1,
                (int)shimDelayBufs[vi][0].size() - 1,
                std::max (effGrainLen, voiceDelayPeriod));

            // Feedback coefficient that produces the target decay time for this voice.
            // shimFb < 1 always — exp() of a negative argument.
            const float shimFb = std::exp (-3.f * (float)voicePeriodSamps
                                           / (float)currentSampleRate / decayT);

            // Staggered onset: voice vi is silent until its counter reaches 0.
            // onsetStartSample is the first sample in this block at which this voice outputs.
            // The grain engine and delay line both run during silence so voices have real
            // content and stable state when they do come in.
            const int onsetStartSample = juce::jmin (numSamples, shimOnsetCounters[vi]);

            for (int ch = 0; ch < numChannels; ++ch)
            {
                auto&        v   = shimVoicesHarm[vi][ch];
                const float* src = buffer.getReadPointer (ch);   // clean pre-conv dry
                float*       out = shimBuffer.getWritePointer (ch);

                for (int i = 0; i < numSamples; ++i)
                {
                    v.grainBuf[(size_t)v.writePtr] = src[i];

                    auto readAt = [&](float rp) -> float {
                        int   ri   = ((int)std::floor (rp) % kShimBufLen + kShimBufLen) % kShimBufLen;
                        float frac = rp - std::floor (rp);
                        return v.grainBuf[(size_t)ri]                        * (1.f - frac)
                             + v.grainBuf[(size_t)((ri + 1) % kShimBufLen)] * frac;
                    };

                    // Two crossfading Hann grains, then 2-stage allpass for spectral smearing.
                    float grainOut = (readAt (v.readPtrA) * hannW (v.grainPhaseA)
                                   + readAt (v.readPtrB) * hannW (v.grainPhaseB)) * 0.5f;
                    grainOut = shimAPs[vi][0][ch].process (grainOut);
                    grainOut = shimAPs[vi][1][ch].process (grainOut);

                    // ── Per-voice delay line with feedback ───────────────────────────
                    // Runs every sample (even during onset silence) so state is stable
                    // at voice onset. Read delayed sample, write grain + fedback echo.
                    auto&  dBuf  = shimDelayBufs[vi][ch];
                    int&   dPtr  = shimDelayPtrs[vi][ch];
                    const int bufLen   = (int)dBuf.size();
                    const int dReadPtr = ((dPtr - voicePeriodSamps) % bufLen + bufLen) % bufLen;
                    const float delayOut = dBuf[(size_t)dReadPtr];
                    dBuf[(size_t)dPtr] = grainOut + shimFb * delayOut;
                    dPtr = (dPtr + 1) % bufLen;

                    // Only contribute to shimBuffer once the onset window has passed.
                    // Both the immediate grain and the echoing delay output are summed —
                    // grainOut for the immediate attack, delayOut for the decaying tail.
                    if (i >= onsetStartSample)
                        out[i] += grainOut + delayOut;

                    v.readPtrA    += pitchRatio;
                    v.readPtrB    += pitchRatio;
                    v.grainPhaseA += pitchRatio / (float)effGrainLen;
                    v.grainPhaseB += pitchRatio / (float)effGrainLen;

                    if (v.readPtrA >= (float)kShimBufLen) v.readPtrA -= (float)kShimBufLen;
                    if (v.readPtrB >= (float)kShimBufLen) v.readPtrB -= (float)kShimBufLen;

                    // ±25% LCG jitter on the voice delay at each grain boundary.
                    if (v.grainPhaseA >= 1.f)
                    {
                        v.grainPhaseA -= 1.f;
                        shimRng = shimRng * 1664525u + 1013904223u;
                        const int jitter = (int)(((float)(shimRng & 0xffff) / 65535.f - 0.5f)
                                                   * 0.5f * (float)voiceDelaySamps);
                        v.readPtrA = (float)((v.writePtr
                                              - (effGrainLen + voiceDelaySamps + jitter)
                                              + kShimBufLen * 2) % kShimBufLen);
                    }
                    if (v.grainPhaseB >= 1.f)
                    {
                        v.grainPhaseB -= 1.f;
                        shimRng = shimRng * 1664525u + 1013904223u;
                        const int jitter = (int)(((float)(shimRng & 0xffff) / 65535.f - 0.5f)
                                                   * 0.5f * (float)voiceDelaySamps);
                        v.readPtrB = (float)((v.writePtr
                                              - (effGrainLen + voiceDelaySamps + jitter)
                                              + kShimBufLen * 2) % kShimBufLen);
                    }

                    v.writePtr = (v.writePtr + 1) % kShimBufLen;
                }
            }

            // Count down onset timer (clamp at zero so it stays inactive once elapsed).
            shimOnsetCounters[vi] = juce::jmax (0, shimOnsetCounters[vi] - numSamples);

            // Advance main LFO by block duration after both channels are processed.
            shimLfoPhase[vi] += mainLfoInc * (float)numSamples;
            if (shimLfoPhase[vi] >= juce::MathConstants<float>::twoPi)
                shimLfoPhase[vi] -= juce::MathConstants<float>::twoPi;
        }

        // ── Inject the summed cloud into the convolver input ──────────────────
        // 1/√8 normalisation keeps perceived loudness consistent with a single voice.
        if (shimIRFd > 0.001f)
        {
            static constexpr float kVoiceNorm = 1.f / 2.828427125f;  // 1/√8
            const float gain = shimIRFd * kVoiceNorm;
            for (int ch = 0; ch < numChannels; ++ch)
            {
                float*       dst = buffer.getWritePointer (ch);
                const float* shm = shimBuffer.getReadPointer (ch);
                for (int i = 0; i < numSamples; ++i)
                    dst[i] += shm[i] * gain;
            }
        }

    }
    else
    {
        shimBuffer.clear();
        // Clear per-voice delay lines so stale energy doesn't bleed in on re-enable.
        for (int v = 0; v < kNumShimVoices; ++v)
            for (int ch = 0; ch < 2; ++ch)
            {
                std::fill (shimDelayBufs[v][ch].begin(), shimDelayBufs[v][ch].end(), 0.f);
                shimDelayPtrs[v][ch] = 0;
            }
    }

    float erDb = apvts.getRawParameterValue (IDs::erLevel)->load();
    float tailDb = apvts.getRawParameterValue (IDs::tailLevel)->load();
    erLevelSmoothed.setTargetValue (juce::Decibels::decibelsToGain (erDb));
    tailLevelSmoothed.setTargetValue (juce::Decibels::decibelsToGain (tailDb));

    // ── Multi-mic mixer ─────────────────────────────────────────────────────
    // Per-path convolution + HP + gain + pan mixer. Up to four paths contribute:
    //   MAIN    — 8 convolvers (tsEr* + tsTail*) with optional ER/Tail crossfeed,
    //             summed through erLevel/tailLevel × trueStereoWetGain. Always present.
    //   DIRECT  — 4 convolvers (tsDirectConv*). Order-0 path with no ER/Tail split.
    //   OUTRIG  — 8 convolvers (tsOutrigEr* + tsOutrigTail*). ER + Tail summed 1:1.
    //   AMBIENT — 8 convolvers (tsAmbEr*    + tsAmbTail*).    ER + Tail summed 1:1.
    // Each strip applies its own 110 Hz 2nd-order HP (enabled flag only — the biquad
    // updates state every sample regardless so toggling is click-free), a smoothed
    // linear gain, and a constant-power pan. The four mono outputs are summed into the
    // wet buffer before EQ / decorrelation / Width.
    //
    // Mute / Solo / per-path On switches are folded into the smoothed target gain:
    // when a strip is gated off the target becomes 0 and the SmoothedValue ramps down
    // over 20 ms. Convolvers are skipped entirely when their path On flag is false,
    // which is the behaviour expected from a switched-off mic send.
    //
    // Defaults (mainOn=true, mainGain=0 dB, mainPan=0, mainHP off, mainMute=mainSolo=
    // false; all three extras Off) collapse the mixer to MAIN-only, with mainHP as
    // a pass-through biquad, strip gain 1.0 and constant-power pan (~0.707 per side at
    // centre). Note that the constant-power pan law gives -3 dB on each channel at
    // centre relative to the pre-C9 single-path block, which wrote the summed eL+tL
    // straight into the wet buffer. This is consistent with the multi-mic brief
    // specification and matches typical mixer pan behaviour.
    {
        juce::AudioBuffer<float> lIn (1, numSamples), rIn (1, numSamples);
        lIn.copyFrom (0, 0, buffer, 0, 0, numSamples);
        rIn.copyFrom (0, 0, buffer, 1, 0, numSamples);

        // Strip parameters (cheap atomic loads; read once per block).
        const bool  mainOnRaw      = apvts.getRawParameterValue (IDs::mainOn)->load()     > 0.5f;
        const bool  mainMuted      = apvts.getRawParameterValue (IDs::mainMute)->load()   > 0.5f;
        const bool  mainSoloFlag   = apvts.getRawParameterValue (IDs::mainSolo)->load()   > 0.5f;
        const bool  mainHPOnRaw    = apvts.getRawParameterValue (IDs::mainHPOn)->load()   > 0.5f;
        const float mainGainDb     = apvts.getRawParameterValue (IDs::mainGain)->load();
        const float mainPanRaw     = apvts.getRawParameterValue (IDs::mainPan)->load();

        const bool  directOnRaw    = apvts.getRawParameterValue (IDs::directOn)->load()   > 0.5f;
        const bool  directMuted    = apvts.getRawParameterValue (IDs::directMute)->load() > 0.5f;
        const bool  directSoloFlag = apvts.getRawParameterValue (IDs::directSolo)->load() > 0.5f;
        const bool  directHPOnRaw  = apvts.getRawParameterValue (IDs::directHPOn)->load() > 0.5f;
        const float directGainDb   = apvts.getRawParameterValue (IDs::directGain)->load();
        const float directPanRaw   = apvts.getRawParameterValue (IDs::directPan)->load();

        const bool  outrigOnRaw    = apvts.getRawParameterValue (IDs::outrigOn)->load()   > 0.5f;
        const bool  outrigMuted    = apvts.getRawParameterValue (IDs::outrigMute)->load() > 0.5f;
        const bool  outrigSoloFlag = apvts.getRawParameterValue (IDs::outrigSolo)->load() > 0.5f;
        const bool  outrigHPOnRaw  = apvts.getRawParameterValue (IDs::outrigHPOn)->load() > 0.5f;
        const float outrigGainDb   = apvts.getRawParameterValue (IDs::outrigGain)->load();
        const float outrigPanRaw   = apvts.getRawParameterValue (IDs::outrigPan)->load();

        const bool  ambientOnRaw    = apvts.getRawParameterValue (IDs::ambientOn)->load()   > 0.5f;
        const bool  ambientMuted    = apvts.getRawParameterValue (IDs::ambientMute)->load() > 0.5f;
        const bool  ambientSoloFlag = apvts.getRawParameterValue (IDs::ambientSolo)->load() > 0.5f;
        const bool  ambientHPOnRaw  = apvts.getRawParameterValue (IDs::ambientHPOn)->load() > 0.5f;
        const float ambientGainDb   = apvts.getRawParameterValue (IDs::ambientGain)->load();
        const float ambientPanRaw   = apvts.getRawParameterValue (IDs::ambientPan)->load();

        const bool anySolo = mainSoloFlag || directSoloFlag || outrigSoloFlag || ambientSoloFlag;

        // A strip "contributes" when its On flag is true, it is not muted, and either no
        // strip is soloed or this strip is one of the soloed ones. MAIN treats its On
        // flag identically to the extras (default true).
        const bool mainContributes    = mainOnRaw    && ! mainMuted    && (! anySolo || mainSoloFlag);
        const bool directContributes  = directOnRaw  && ! directMuted  && (! anySolo || directSoloFlag);
        const bool outrigContributes  = outrigOnRaw  && ! outrigMuted  && (! anySolo || outrigSoloFlag);
        const bool ambientContributes = ambientOnRaw && ! ambientMuted && (! anySolo || ambientSoloFlag);

        // Target linear gain: 0 when gated off so the SmoothedValue ramps down over 20 ms.
        mainGainSmoothed   .setTargetValue (mainContributes    ? juce::Decibels::decibelsToGain (mainGainDb)    : 0.0f);
        directGainSmoothed .setTargetValue (directContributes  ? juce::Decibels::decibelsToGain (directGainDb)  : 0.0f);
        outrigGainSmoothed .setTargetValue (outrigContributes  ? juce::Decibels::decibelsToGain (outrigGainDb)  : 0.0f);
        ambientGainSmoothed.setTargetValue (ambientContributes ? juce::Decibels::decibelsToGain (ambientGainDb) : 0.0f);

        // Refresh per-strip HP enabled flag (the biquad itself always updates state so
        // re-enable is click-free — see DSP_17 regression test).
        mainHP   .enabled = mainHPOnRaw;
        directHP .enabled = directHPOnRaw;
        outrigHP .enabled = outrigHPOnRaw;
        ambientHP.enabled = ambientHPOnRaw;

        // Constant-power pan coefficients (sampled per block — pan changes slowly so a
        // per-sample smoother is unnecessary).
        auto panCoeffs = [] (float p, float& pL, float& pR)
        {
            p = juce::jlimit (-1.f, 1.f, p);
            const float a = (p + 1.f) * juce::MathConstants<float>::pi * 0.25f;
            pL = std::cos (a);
            pR = std::sin (a);
        };
        float mainPanL,   mainPanR,   directPanL, directPanR, outrigPanL, outrigPanR, ambientPanL, ambientPanR;
        panCoeffs (mainPanRaw,    mainPanL,    mainPanR);
        panCoeffs (directPanRaw,  directPanL,  directPanR);
        panCoeffs (outrigPanRaw,  outrigPanL,  outrigPanR);
        panCoeffs (ambientPanRaw, ambientPanL, ambientPanR);

        // Shared convolver-temp buffer. Use explicit 3-arg AudioBlock constructor so its
        // size matches numSamples exactly (see CLAUDE.md note on convTmp / NUPC state corruption).
        juce::AudioBuffer<float> tmp (1, numSamples);
        juce::dsp::AudioBlock<float> tmpBlock (tmp.getArrayOfWritePointers(), 1, (size_t) numSamples);

        auto runFour = [&] (juce::dsp::Convolution& cLL,
                             juce::dsp::Convolution& cRL,
                             juce::dsp::Convolution& cLR,
                             juce::dsp::Convolution& cRR,
                             juce::AudioBuffer<float>& outL,
                             juce::AudioBuffer<float>& outR)
        {
            tmp.copyFrom (0, 0, lIn, 0, 0, numSamples);
            cLL.process (juce::dsp::ProcessContextReplacing<float> (tmpBlock));
            outL.copyFrom (0, 0, tmp, 0, 0, numSamples);
            tmp.copyFrom (0, 0, rIn, 0, 0, numSamples);
            cRL.process (juce::dsp::ProcessContextReplacing<float> (tmpBlock));
            outL.addFrom (0, 0, tmp, 0, 0, numSamples);

            tmp.copyFrom (0, 0, lIn, 0, 0, numSamples);
            cLR.process (juce::dsp::ProcessContextReplacing<float> (tmpBlock));
            outR.copyFrom (0, 0, tmp, 0, 0, numSamples);
            tmp.copyFrom (0, 0, rIn, 0, 0, numSamples);
            cRR.process (juce::dsp::ProcessContextReplacing<float> (tmpBlock));
            outR.addFrom (0, 0, tmp, 0, 0, numSamples);
        };

        // From here on the buffer holds the accumulated wet signal. Clear it and add
        // each strip's contribution.
        buffer.clear();
        const float trueStereoWetGain = 2.0f;

        // ── Convolver readiness gating & fade re-arm ────────────────────────
        // juce::dsp::Convolution defaults to a unity (pass-through) IR until the NUPC
        // background thread kicked off by loadImpulseResponse() has actually published
        // a real engine — typically tens to hundreds of ms after loadImpulseResponse()
        // returns. Two independent problems follow from that:
        //
        //   1. If we open the wet path the moment loadImpulseResponse() returns
        //      (checking only mainIRLoaded etc.), the wet bus contains pass-through
        //      of the post-predelay dry signal at high gain (trueStereoWetGain = 2.0,
        //      summed across 4–8 mono convolvers) until JUCE swaps in the real IR.
        //      When that swap happens, the audible content changes abruptly — click.
        //
        //   2. The irLoadFadeSamplesRemaining wet-bus fade is armed at loadImpulseResponse()
        //      time. For a newly-instantiated plugin the fade may have already expired
        //      (or be near full gain) by the moment the convolvers are actually ready,
        //      so the step from "wet bus muted because we're gating on readiness" to
        //      "wet bus at full real-convolved gain" is not covered by the fade.
        //
        // Fix: (a) AND all of a path's convolvers into a single readiness flag (every
        // convolver must have a real IR, otherwise summing mixes real output with unity
        // pass-through from the laggards); (b) when a path transitions not-ready → ready
        // between two blocks, re-arm irLoadFadeSamplesRemaining to full so the real
        // signal ramps in smoothly. getCurrentIRSize() returns 0 until the real engine
        // is installed; it's safe to read from the audio thread because currentEngine
        // is only mutated from this same thread inside processSamples → installPendingEngine.
        const bool mainReady = tsErConvLL  .getCurrentIRSize() > 0
                            && tsErConvRL  .getCurrentIRSize() > 0
                            && tsErConvLR  .getCurrentIRSize() > 0
                            && tsErConvRR  .getCurrentIRSize() > 0
                            && tsTailConvLL.getCurrentIRSize() > 0
                            && tsTailConvRL.getCurrentIRSize() > 0
                            && tsTailConvLR.getCurrentIRSize() > 0
                            && tsTailConvRR.getCurrentIRSize() > 0;
        const bool directReady = tsDirectConvLL.getCurrentIRSize() > 0
                              && tsDirectConvRL.getCurrentIRSize() > 0
                              && tsDirectConvLR.getCurrentIRSize() > 0
                              && tsDirectConvRR.getCurrentIRSize() > 0;
        const bool outrigReady = tsOutrigErConvLL  .getCurrentIRSize() > 0
                              && tsOutrigErConvRL  .getCurrentIRSize() > 0
                              && tsOutrigErConvLR  .getCurrentIRSize() > 0
                              && tsOutrigErConvRR  .getCurrentIRSize() > 0
                              && tsOutrigTailConvLL.getCurrentIRSize() > 0
                              && tsOutrigTailConvRL.getCurrentIRSize() > 0
                              && tsOutrigTailConvLR.getCurrentIRSize() > 0
                              && tsOutrigTailConvRR.getCurrentIRSize() > 0;
        const bool ambientReady = tsAmbErConvLL  .getCurrentIRSize() > 0
                               && tsAmbErConvRL  .getCurrentIRSize() > 0
                               && tsAmbErConvLR  .getCurrentIRSize() > 0
                               && tsAmbErConvRR  .getCurrentIRSize() > 0
                               && tsAmbTailConvLL.getCurrentIRSize() > 0
                               && tsAmbTailConvRL.getCurrentIRSize() > 0
                               && tsAmbTailConvLR.getCurrentIRSize() > 0
                               && tsAmbTailConvRR.getCurrentIRSize() > 0;

        const bool mainJustReady    = mainReady    && ! mainConvPrevReady   .load (std::memory_order_relaxed);
        const bool directJustReady  = directReady  && ! directConvPrevReady .load (std::memory_order_relaxed);
        const bool outrigJustReady  = outrigReady  && ! outrigConvPrevReady .load (std::memory_order_relaxed);
        const bool ambientJustReady = ambientReady && ! ambientConvPrevReady.load (std::memory_order_relaxed);

        // If any active path's convolvers transitioned to ready this block, arm (or
        // re-arm, taking the max with whatever is already counting down) the wet fade.
        // Inactive paths don't need the fade — their contribution is gated off anyway.
        const bool anyActivePathJustReady = (mainOnRaw    && mainJustReady)
                                         || (directOnRaw  && directJustReady)
                                         || (outrigOnRaw  && outrigJustReady)
                                         || (ambientOnRaw && ambientJustReady);
        if (anyActivePathJustReady)
        {
            const int cur = irLoadFadeSamplesRemaining.load (std::memory_order_relaxed);
            if (cur < kIRLoadFadeSamples)
                irLoadFadeSamplesRemaining.store (kIRLoadFadeSamples, std::memory_order_relaxed);
        }

        mainConvPrevReady   .store (mainReady,    std::memory_order_relaxed);
        directConvPrevReady .store (directReady,  std::memory_order_relaxed);
        outrigConvPrevReady .store (outrigReady,  std::memory_order_relaxed);
        ambientConvPrevReady.store (ambientReady, std::memory_order_relaxed);

        // ── MAIN ────────────────────────────────────────────────────────────
        float mainPkL = 0.f, mainPkR = 0.f;
        float erPkL = 0.f, erPkR = 0.f, tailPkL = 0.f, tailPkR = 0.f;
        if (mainOnRaw && mainIRLoaded.load() && mainReady)
        {
            juce::AudioBuffer<float> lEr (1, numSamples), rEr (1, numSamples);
            juce::AudioBuffer<float> lTail (1, numSamples), rTail (1, numSamples);
            runFour (tsErConvLL,   tsErConvRL,   tsErConvLR,   tsErConvRR,   lEr,   rEr);
            runFour (tsTailConvLL, tsTailConvRL, tsTailConvLR, tsTailConvRR, lTail, rTail);

            if (apvts.getRawParameterValue (IDs::erCrossfeedOn)->load() > 0.5f && crossfeedMaxSamples > 0)
            {
                const float delayMs = apvts.getRawParameterValue (IDs::erCrossfeedDelayMs)->load();
                const float attDb   = apvts.getRawParameterValue (IDs::erCrossfeedAttDb)->load();
                const int delaySamps = juce::jlimit (0, crossfeedMaxSamples - 1,
                    (int) std::round (delayMs * (float) currentSampleRate / 1000.0f));
                const float gain = juce::Decibels::decibelsToGain (attDb);
                float* lPtr = lEr.getWritePointer (0);
                float* rPtr = rEr.getWritePointer (0);
                for (int i = 0; i < numSamples; ++i)
                {
                    int readRtoL = (crossfeedErWriteRtoL - delaySamps + crossfeedMaxSamples) % crossfeedMaxSamples;
                    int readLtoR = (crossfeedErWriteLtoR - delaySamps + crossfeedMaxSamples) % crossfeedMaxSamples;
                    float l = lPtr[i], r = rPtr[i];
                    lPtr[i] += gain * crossfeedErBufRtoL[(size_t) readRtoL];
                    rPtr[i] += gain * crossfeedErBufLtoR[(size_t) readLtoR];
                    crossfeedErBufRtoL[(size_t) crossfeedErWriteRtoL] = r;
                    crossfeedErBufLtoR[(size_t) crossfeedErWriteLtoR] = l;
                    crossfeedErWriteRtoL = (crossfeedErWriteRtoL + 1) % crossfeedMaxSamples;
                    crossfeedErWriteLtoR = (crossfeedErWriteLtoR + 1) % crossfeedMaxSamples;
                }
            }
            if (apvts.getRawParameterValue (IDs::tailCrossfeedOn)->load() > 0.5f && crossfeedMaxSamples > 0)
            {
                const float delayMs = apvts.getRawParameterValue (IDs::tailCrossfeedDelayMs)->load();
                const float attDb   = apvts.getRawParameterValue (IDs::tailCrossfeedAttDb)->load();
                const int delaySamps = juce::jlimit (0, crossfeedMaxSamples - 1,
                    (int) std::round (delayMs * (float) currentSampleRate / 1000.0f));
                const float gain = juce::Decibels::decibelsToGain (attDb);
                float* lPtr = lTail.getWritePointer (0);
                float* rPtr = rTail.getWritePointer (0);
                for (int i = 0; i < numSamples; ++i)
                {
                    int readRtoL = (crossfeedTailWriteRtoL - delaySamps + crossfeedMaxSamples) % crossfeedMaxSamples;
                    int readLtoR = (crossfeedTailWriteLtoR - delaySamps + crossfeedMaxSamples) % crossfeedMaxSamples;
                    float l = lPtr[i], r = rPtr[i];
                    lPtr[i] += gain * crossfeedTailBufRtoL[(size_t) readRtoL];
                    rPtr[i] += gain * crossfeedTailBufLtoR[(size_t) readLtoR];
                    crossfeedTailBufRtoL[(size_t) crossfeedTailWriteRtoL] = r;
                    crossfeedTailBufLtoR[(size_t) crossfeedTailWriteLtoR] = l;
                    crossfeedTailWriteRtoL = (crossfeedTailWriteRtoL + 1) % crossfeedMaxSamples;
                    crossfeedTailWriteLtoR = (crossfeedTailWriteLtoR + 1) % crossfeedMaxSamples;
                }
            }

            float* bL = buffer.getWritePointer (0);
            float* bR = buffer.getWritePointer (1);
            const float* elp = lEr.getReadPointer (0);
            const float* erp = rEr.getReadPointer (0);
            const float* tlp = lTail.getReadPointer (0);
            const float* trp = rTail.getReadPointer (0);
            for (int i = 0; i < numSamples; ++i)
            {
                const float erG   = erLevelSmoothed  .getNextValue();
                const float tailG = tailLevelSmoothed.getNextValue();
                const float eL = elp[i] * erG   * trueStereoWetGain;
                const float eR = erp[i] * erG   * trueStereoWetGain;
                const float tL = tlp[i] * tailG * trueStereoWetGain;
                const float tR = trp[i] * tailG * trueStereoWetGain;
                erPkL   = juce::jmax (erPkL,   std::abs (eL));
                erPkR   = juce::jmax (erPkR,   std::abs (eR));
                tailPkL = juce::jmax (tailPkL, std::abs (tL));
                tailPkR = juce::jmax (tailPkR, std::abs (tR));
                float sL = eL + tL;
                float sR = eR + tR;
                sL = mainHP.process (sL, 0);
                sR = mainHP.process (sR, 1);
                const float g = mainGainSmoothed.getNextValue();
                sL *= g;
                sR *= g;
                const float oL = sL * mainPanL;
                const float oR = sR * mainPanR;
                bL[i] += oL;
                bR[i] += oR;
                mainPkL = juce::jmax (mainPkL, std::abs (oL));
                mainPkR = juce::jmax (mainPkR, std::abs (oR));
            }
        }
        else
        {
            // MAIN off — still advance all smoothers so unmute/re-enable is click-free.
            for (int i = 0; i < numSamples; ++i)
            {
                erLevelSmoothed  .getNextValue();
                tailLevelSmoothed.getNextValue();
                mainGainSmoothed .getNextValue();
            }
        }
        updatePeak (erLevelPeakL,   erPkL);
        updatePeak (erLevelPeakR,   erPkR);
        updatePeak (tailLevelPeakL, tailPkL);
        updatePeak (tailLevelPeakR, tailPkR);
        updatePeak (mainPeakL,      mainPkL);
        updatePeak (mainPeakR,      mainPkR);

        // ── DIRECT ──────────────────────────────────────────────────────────
        // Gated on directIRLoaded AND directReady (precomputed above from all 4 direct
        // convolvers' getCurrentIRSize() > 0) — see the readiness block above for
        // rationale.
        float directPkL = 0.f, directPkR = 0.f;
        if (directOnRaw && directIRLoaded.load() && directReady)
        {
            juce::AudioBuffer<float> dL (1, numSamples), dR (1, numSamples);
            runFour (tsDirectConvLL, tsDirectConvRL, tsDirectConvLR, tsDirectConvRR, dL, dR);

            float* bL = buffer.getWritePointer (0);
            float* bR = buffer.getWritePointer (1);
            const float* dlp = dL.getReadPointer (0);
            const float* drp = dR.getReadPointer (0);
            for (int i = 0; i < numSamples; ++i)
            {
                float sL = dlp[i] * trueStereoWetGain;
                float sR = drp[i] * trueStereoWetGain;
                sL = directHP.process (sL, 0);
                sR = directHP.process (sR, 1);
                const float g = directGainSmoothed.getNextValue();
                sL *= g;
                sR *= g;
                const float oL = sL * directPanL;
                const float oR = sR * directPanR;
                bL[i] += oL;
                bR[i] += oR;
                directPkL = juce::jmax (directPkL, std::abs (oL));
                directPkR = juce::jmax (directPkR, std::abs (oR));
            }
        }
        else
        {
            for (int i = 0; i < numSamples; ++i) directGainSmoothed.getNextValue();
        }
        updatePeak (directPeakL, directPkL);
        updatePeak (directPeakR, directPkR);

        // ── OUTRIG ──────────────────────────────────────────────────────────
        // Gated on outrigIRLoaded AND outrigReady — see the readiness block above.
        float outrigPkL = 0.f, outrigPkR = 0.f;
        if (outrigOnRaw && outrigIRLoaded.load() && outrigReady)
        {
            juce::AudioBuffer<float> oEL (1, numSamples), oER (1, numSamples);
            juce::AudioBuffer<float> oTL (1, numSamples), oTR (1, numSamples);
            runFour (tsOutrigErConvLL,   tsOutrigErConvRL,   tsOutrigErConvLR,   tsOutrigErConvRR,   oEL, oER);
            runFour (tsOutrigTailConvLL, tsOutrigTailConvRL, tsOutrigTailConvLR, tsOutrigTailConvRR, oTL, oTR);

            float* bL = buffer.getWritePointer (0);
            float* bR = buffer.getWritePointer (1);
            const float* elp = oEL.getReadPointer (0);
            const float* erp = oER.getReadPointer (0);
            const float* tlp = oTL.getReadPointer (0);
            const float* trp = oTR.getReadPointer (0);
            for (int i = 0; i < numSamples; ++i)
            {
                float sL = (elp[i] + tlp[i]) * trueStereoWetGain;
                float sR = (erp[i] + trp[i]) * trueStereoWetGain;
                sL = outrigHP.process (sL, 0);
                sR = outrigHP.process (sR, 1);
                const float g = outrigGainSmoothed.getNextValue();
                sL *= g;
                sR *= g;
                const float oL = sL * outrigPanL;
                const float oR = sR * outrigPanR;
                bL[i] += oL;
                bR[i] += oR;
                outrigPkL = juce::jmax (outrigPkL, std::abs (oL));
                outrigPkR = juce::jmax (outrigPkR, std::abs (oR));
            }
        }
        else
        {
            for (int i = 0; i < numSamples; ++i) outrigGainSmoothed.getNextValue();
        }
        updatePeak (outrigPeakL, outrigPkL);
        updatePeak (outrigPeakR, outrigPkR);

        // ── AMBIENT ─────────────────────────────────────────────────────────
        // Gated on ambientIRLoaded AND ambientReady — see the readiness block above.
        float ambientPkL = 0.f, ambientPkR = 0.f;
        if (ambientOnRaw && ambientIRLoaded.load() && ambientReady)
        {
            juce::AudioBuffer<float> aEL (1, numSamples), aER (1, numSamples);
            juce::AudioBuffer<float> aTL (1, numSamples), aTR (1, numSamples);
            runFour (tsAmbErConvLL,   tsAmbErConvRL,   tsAmbErConvLR,   tsAmbErConvRR,   aEL, aER);
            runFour (tsAmbTailConvLL, tsAmbTailConvRL, tsAmbTailConvLR, tsAmbTailConvRR, aTL, aTR);

            float* bL = buffer.getWritePointer (0);
            float* bR = buffer.getWritePointer (1);
            const float* elp = aEL.getReadPointer (0);
            const float* erp = aER.getReadPointer (0);
            const float* tlp = aTL.getReadPointer (0);
            const float* trp = aTR.getReadPointer (0);
            for (int i = 0; i < numSamples; ++i)
            {
                float sL = (elp[i] + tlp[i]) * trueStereoWetGain;
                float sR = (erp[i] + trp[i]) * trueStereoWetGain;
                sL = ambientHP.process (sL, 0);
                sR = ambientHP.process (sR, 1);
                const float g = ambientGainSmoothed.getNextValue();
                sL *= g;
                sR *= g;
                const float oL = sL * ambientPanL;
                const float oR = sR * ambientPanR;
                bL[i] += oL;
                bR[i] += oR;
                ambientPkL = juce::jmax (ambientPkL, std::abs (oL));
                ambientPkR = juce::jmax (ambientPkR, std::abs (oR));
            }
        }
        else
        {
            for (int i = 0; i < numSamples; ++i) ambientGainSmoothed.getNextValue();
        }
        updatePeak (ambientPeakL, ambientPkL);
        updatePeak (ambientPeakR, ambientPkR);
    }

    // EQ: low shelf → peak 1 → peak 2 → peak 3 → high shelf
    lowShelfBand.process (context);
    lowBand.process (context);
    midBand.process (context);
    highBand.process (context);
    highShelfBand.process (context);

    // Stereo decorrelation: 2-stage allpass on R channel only (preserves mono sum, widens tail image)
    if (numChannels >= 2)
    {
        float* R = buffer.getWritePointer (1);
        for (int i = 0; i < numSamples; ++i)
        {
            float s = R[i];
            for (int stage = 0; stage < 2; ++stage)
            {
                const int d = decorrDelays[stage];
                const int p = decorrPtrs[stage];
                float delayed = decorrBufs[stage][(size_t)p];
                float w = s + decorrG * delayed;
                decorrBufs[stage][(size_t)p] = w;
                decorrPtrs[stage] = (p + 1) % d;
                s = -decorrG * w + delayed;
            }
            R[i] = s;
        }
    }

    // Modulation: LFO on wet gain (period 0.01..2 s, depth 0..100%) — UI reversed: left = slow, right = fast
    float modRateRaw = apvts.getRawParameterValue (IDs::modRate)->load();
    float modPeriodSec = 2.01f - modRateRaw;
    float modDepth = apvts.getRawParameterValue (IDs::modDepth)->load();
    if (modDepth > 0.0001f && modPeriodSec > 0.0001f)
    {
        float phaseInc = (float) (1.0 / (modPeriodSec * currentSampleRate));
        for (int i = 0; i < numSamples; ++i)
        {
            float gain = 1.0f + modDepth * std::sin (2.0f * juce::MathConstants<float>::twoPi * lfoPhase);
            buffer.applyGain (0, i, 1, gain);
            buffer.applyGain (1, i, 1, gain);
            lfoPhase += phaseInc;
            if (lfoPhase >= 1.0f) lfoPhase -= 1.0f;
        }
    }

    float widthVal = apvts.getRawParameterValue (IDs::width)->load();
    applyWidth (buffer, widthVal);

    // Chorus/tail modulation on wet signal (variable delay, LFO-modulated).
    // Skip when current IR is ER-only (synth or loaded from file) — discrete reflections get a strong delay-like repeat.
    bool skipTailMod = getLastIRSynthParams().er_only;
    float tailAmt = apvts.getRawParameterValue (IDs::tailMod)->load();
    float depthMs = apvts.getRawParameterValue (IDs::delayDepth)->load();
    float rateHz = apvts.getRawParameterValue (IDs::tailRate)->load();
    if (! skipTailMod && tailAmt > 0.0001f && depthMs > 0.1f && rateHz > 0.001f)
    {
        const float baseDelayMs = 2.5f;
        float phaseInc = (float) (rateHz / currentSampleRate);
        for (int i = 0; i < numSamples; ++i)
        {
            float mod = 0.5f * depthMs * std::sin (2.0f * juce::MathConstants<float>::twoPi * tailLfoPhase);
            float delayMs = baseDelayMs + mod;
            float delaySamps = (float) (currentSampleRate * delayMs * 0.001);
            tailLfoPhase += phaseInc;
            if (tailLfoPhase >= 1.0f) tailLfoPhase -= 1.0f;

            for (int ch = 0; ch < numChannels; ++ch)
            {
                float wetVal = buffer.getSample (ch, i);
                chorusDelayLine.pushSample (ch, wetVal);
                float delayed = chorusDelayLine.popSample (ch, delaySamps);
                buffer.setSample (ch, i, wetVal * (1.0f - tailAmt) + delayed * tailAmt);
            }
        }
    }

    // (Cloud Insertion 2 removed — Cloud now runs pre-convolution above.
    //  cloudVolume is applied post-blend below, alongside bloomVolume.)

    // (Shimmer post-conv feedback capture removed: the new 8-voice cloud architecture
    //  is entirely pre-conv with no loopback. The IR provides all smearing / decay.)

    // Wet output gain (boost/cut the whole wet signal, like input gain for output)
    float outGainDb = apvts.getRawParameterValue (IDs::outputGain)->load();
    outputGainSmoothed.setTargetValue (juce::Decibels::decibelsToGain (outGainDb));
    for (int i = 0; i < numSamples; ++i)
    {
        float g = outputGainSmoothed.getNextValue();
        for (int ch = 0; ch < numChannels; ++ch)
            buffer.setSample (ch, i, buffer.getSample (ch, i) * g);
    }

    // IR load crossfade: fade wet bus from silence while convolvers swap to new IR.
    // The ts*Conv convolvers load asynchronously; each swaps independently during its
    // process() call. Between a preset switch and full swap-in, some may run old IR /
    // some new, producing wrong-level mixed output (distortion). Fading from silence
    // prevents this from being heard. Dry signal is unaffected — it plays through normally.
    // Sample-based counter stays consistent across buffer sizes (a block-count fade would
    // shrink from ~680 ms at 512-sample buffers to ~170 ms at 128-sample buffers).
    {
        int remaining = irLoadFadeSamplesRemaining.load (std::memory_order_relaxed);
        if (remaining > 0)
        {
            // Linear per-block fade-in: 0 just after load → 1 at kIRLoadFadeSamples elapsed
            float fadeIn = 1.0f - (float) remaining / (float) kIRLoadFadeSamples;
            fadeIn = juce::jlimit (0.0f, 1.0f, fadeIn);
            buffer.applyGain (fadeIn);
            int newRem = juce::jmax (0, remaining - numSamples);
            irLoadFadeSamplesRemaining.store (newRem, std::memory_order_relaxed);
        }
    }

    // Push wet signal for spectrum analyser (before dry/wet mix)
    for (int i = 0; i < numSamples; ++i)
    {
        float s = (buffer.getSample (0, i) + (numChannels > 1 ? buffer.getSample (1, i) : 0.0f)) * 0.5f;
        pushWetSampleForSpectrum (s);
    }

    // Mix dry/wet: output = dry * dryBuffer + wet * wetBuffer (buffer is currently wet)
    float dry = dryGain.getGainLinear();
    float wet = wetGain.getGainLinear();
    for (int ch = 0; ch < numChannels; ++ch)
    {
        buffer.applyGain (ch, 0, numSamples, wet);
        buffer.addFrom (ch, 0, dryBuffer, ch, 0, numSamples, dry);
    }

    // ——————————————————————————————————————————————————————————————————
    // Bloom hybrid Volume output: injected after dry/wet blend so it is independent
    // of the plugin's wet/dry control. Behaves like the direct output of a Bloom pedal
    // in parallel with the dry/wet-controlled reverb signal.
    // ——————————————————————————————————————————————————————————————————
    if (apvts.getRawParameterValue (IDs::bloomOn)->load() > 0.5f && numSamples <= bloomBuffer.getNumSamples())
    {
        const float bloomVol = apvts.getRawParameterValue (IDs::bloomVolume)->load();
        if (bloomVol > 0.f)
        {
            for (int ch = 0; ch < numChannels; ++ch)
            {
                float*       data  = buffer.getWritePointer (ch);
                const float* bloom = bloomBuffer.getReadPointer (ch);
                for (int i = 0; i < numSamples; ++i)
                    data[i] += bloom[i] * bloomVol;
            }
        }
    }

    // ——————————————————————————————————————————————————————————————————
    // Cloud Volume: grain output added post-blend, independent of dry/wet.
    // Behaves like bloomVolume — audible even at dry/wet = 0.
    // ——————————————————————————————————————————————————————————————————
    if (apvts.getRawParameterValue (IDs::cloudOn)->load() > 0.5f)
    {
        const float cloudVol = apvts.getRawParameterValue (IDs::cloudVolume)->load();
        if (cloudVol > 0.f && numSamples <= cloudBuffer.getNumSamples())
        {
            for (int ch = 0; ch < numChannels; ++ch)
            {
                float*       data   = buffer.getWritePointer (ch);
                const float* grains = cloudBuffer.getReadPointer (ch);
                for (int i = 0; i < numSamples; ++i)
                    data[i] += grains[i] * cloudVol;
            }
        }
    }

    // (shimVolume and shimFeedback loopback paths removed — shimmer is now a pure
    //  pre-conv 8-voice harmonic cloud. The IR provides all smearing and decay.)

    // Output level metering
    float peakL = 0.0f, peakR = 0.0f;
    for (int i = 0; i < numSamples; ++i)
    {
        if (numChannels > 0) peakL = juce::jmax (peakL, std::abs (buffer.getSample (0, i)));
        if (numChannels > 1) peakR = juce::jmax (peakR, std::abs (buffer.getSample (1, i)));
    }
    updatePeak (outputLevelPeakL, peakL);
    updatePeak (outputLevelPeakR, peakR);
}

float PingProcessor::getOutputLevelDb (int channel) const
{
    const std::atomic<float>* peakStore = (channel == 0) ? &outputLevelPeakL : &outputLevelPeakR;
    float peak = peakStore->load();
    if (peak < 1e-6f) return -60.0f;
    return juce::Decibels::gainToDecibels (peak);
}

float PingProcessor::getInputLevelDb (int channel) const
{
    const std::atomic<float>* peakStore = (channel == 0) ? &inputLevelPeakL : &inputLevelPeakR;
    float peak = peakStore->load();
    if (peak < 1e-6f) return -60.0f;
    return juce::Decibels::gainToDecibels (peak);
}

float PingProcessor::getErLevelDb (int channel) const
{
    const std::atomic<float>* peakStore = (channel == 0) ? &erLevelPeakL : &erLevelPeakR;
    float peak = peakStore->load();
    if (peak < 1e-6f) return -60.0f;
    return juce::Decibels::gainToDecibels (peak);
}

float PingProcessor::getTailLevelDb (int channel) const
{
    const std::atomic<float>* peakStore = (channel == 0) ? &tailLevelPeakL : &tailLevelPeakR;
    float peak = peakStore->load();
    if (peak < 1e-6f) return -60.0f;
    return juce::Decibels::gainToDecibels (peak);
}

float PingProcessor::getPathPeak (MicPath path, int channel) const noexcept
{
    const std::atomic<float>* peakStore = nullptr;
    switch (path)
    {
        case MicPath::Main:    peakStore = (channel == 0) ? &mainPeakL    : &mainPeakR;    break;
        case MicPath::Direct:  peakStore = (channel == 0) ? &directPeakL  : &directPeakR;  break;
        case MicPath::Outrig:  peakStore = (channel == 0) ? &outrigPeakL  : &outrigPeakR;  break;
        case MicPath::Ambient: peakStore = (channel == 0) ? &ambientPeakL : &ambientPeakR; break;
    }
    return peakStore != nullptr ? peakStore->load() : 0.0f;
}

void PingProcessor::pushWetSampleForSpectrum (float s) noexcept
{
    if (spectrumBlockReady.load()) return;
    spectrumFifo[spectrumFifoIndex++] = s;
    if (spectrumFifoIndex >= spectrumFftSize)
    {
        juce::zeromem (spectrumFftData, sizeof (spectrumFftData));
        memcpy (spectrumFftData, spectrumFifo, (size_t) spectrumFftSize * sizeof (float));
        spectrumBlockReady.store (true);
        spectrumFifoIndex = 0;
    }
}

int PingProcessor::pullSpectrumSamples (float* dest, int maxSamples)
{
    if (! spectrumBlockReady.load() || maxSamples < spectrumFftSize) return 0;
    memcpy (dest, spectrumFftData, (size_t) spectrumFftSize * sizeof (float));
    spectrumBlockReady.store (false);
    return spectrumFftSize;
}

void PingProcessor::updateGains()
{
    float mix = apvts.getRawParameterValue (IDs::dryWet)->load();
    float dryVal = std::sqrt (1.0f - mix);
    float wetVal = std::sqrt (mix);
    dryGain.setGainLinear (dryVal);
    wetGain.setGainLinear (wetVal);
}

void PingProcessor::updatePredelay()
{
    float ms = apvts.getRawParameterValue (IDs::predelay)->load();
    float samples = (float) (currentSampleRate * ms * 0.001);
    predelayLine.setDelay (samples);
}

void PingProcessor::updateEQ()
{
    auto freq = [this] (const juce::String& id) { return apvts.getRawParameterValue (id)->load(); };
    auto gain = [this] (const juce::String& id) { return juce::Decibels::decibelsToGain (apvts.getRawParameterValue (id)->load()); };
    auto q    = [this] (const juce::String& id) { return apvts.getRawParameterValue (id)->load(); };
    *lowShelfBand.state = *juce::dsp::IIR::Coefficients<float>::makeLowShelf  (currentSampleRate, freq (IDs::band3Freq), q (IDs::band3Q), gain (IDs::band3Gain));
    *lowBand.state      = *juce::dsp::IIR::Coefficients<float>::makePeakFilter (currentSampleRate, freq (IDs::band0Freq), q (IDs::band0Q), gain (IDs::band0Gain));
    *midBand.state      = *juce::dsp::IIR::Coefficients<float>::makePeakFilter (currentSampleRate, freq (IDs::band1Freq), q (IDs::band1Q), gain (IDs::band1Gain));
    *highBand.state     = *juce::dsp::IIR::Coefficients<float>::makePeakFilter (currentSampleRate, freq (IDs::band2Freq), q (IDs::band2Q), gain (IDs::band2Gain));
    *highShelfBand.state= *juce::dsp::IIR::Coefficients<float>::makeHighShelf (currentSampleRate, freq (IDs::band4Freq), q (IDs::band4Q), gain (IDs::band4Gain));
}

void PingProcessor::applyWidth (juce::AudioBuffer<float>& wet, float width)
{
    if (wet.getNumChannels() < 2) return;
    int n = wet.getNumSamples();
    float* L = wet.getWritePointer (0);
    float* R = wet.getWritePointer (1);
    for (int i = 0; i < n; ++i)
    {
        float m = (L[i] + R[i]) * 0.5f;
        float s = (L[i] - R[i]) * 0.5f;
        s *= width;
        L[i] = m + s;
        R[i] = m - s;
    }
}

void PingProcessor::loadIRFromFile (const juce::File& file)
{
    if (! file.existsAsFile()) return;

    // ── Multi-mic aux-suffix handling ────────────────────────────────────────
    // The user may select an aux file (e.g. "Venue_outrig.wav") directly from
    // the combo or via Import. Two cases:
    //   (a) Base sibling exists → redirect to load the WHOLE set anchored at
    //       the base file. The user gets MAIN + DIRECT + OUTRIG + AMBIENT in
    //       the right slots, identical to having clicked the base file.
    //   (b) Base sibling missing (orphan) → load just this aux file into its
    //       matching slot. MAIN is left untouched so the user can keep
    //       working with whatever MAIN content they already had.
    {
        const juce::String stem = file.getFileNameWithoutExtension();
        struct AuxMap { const char* suffix; MicPath path; };
        static const AuxMap kAuxMap[] = {
            { "_direct",  MicPath::Direct  },
            { "_outrig",  MicPath::Outrig  },
            { "_ambient", MicPath::Ambient },
        };
        static const char* const kAudioExts[] = { ".wav", ".WAV", ".aiff", ".aif", ".AIFF", ".AIF" };

        for (const auto& m : kAuxMap)
        {
            if (! stem.endsWithIgnoreCase (m.suffix)) continue;
            const auto baseStem = stem.dropLastCharacters ((int) std::strlen (m.suffix));
            const auto parent = file.getParentDirectory();

            juce::File baseFile;
            for (auto* ext : kAudioExts)
            {
                auto candidate = parent.getChildFile (baseStem + ext);
                if (candidate.existsAsFile()) { baseFile = candidate; break; }
            }

            if (baseFile != juce::File())
            {
                // Whole-set redirect: load MAIN + auto-load all siblings.
                loadIRFromFile (baseFile);
                return;
            }

            // Orphan aux file: load into its matching slot, then clear MAIN and the
            // other two aux paths. This gives a clean "you picked one file, you get
            // exactly that path" semantics — without clearing, the previously-loaded
            // MAIN / sibling-aux paths would still be enableable on the front-panel
            // mixer with stale audio from whatever was loaded before.
            juce::AudioFormatManager fm;
            fm.registerBasicFormats();
            std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (file));
            if (! reader) return;
            juce::AudioBuffer<float> auxBuf ((int) reader->numChannels, (int) reader->lengthInSamples);
            reader->read (&auxBuf, 0, (int) reader->lengthInSamples, 0, true, true);

            // Clear every path *except* the one we're about to populate. Done before
            // the load so loadIRFromBuffer's flag-flip is the final state.
            for (auto p : { MicPath::Main, MicPath::Direct, MicPath::Outrig, MicPath::Ambient })
                if (p != m.path)
                    clearMicPath (p);

            loadIRFromBuffer (std::move (auxBuf), reader->sampleRate, /*fromSynth=*/false,
                              /*deferConvolverLoad=*/false, m.path);

            // Display name reflects the orphan filename verbatim (suffix preserved).
            setPathDisplayName (m.path, file.getFileNameWithoutExtension());

            // Auto-enable the corresponding mixer strip. Without this the orphan
            // would load silently — the IR Synth panel would update visually but
            // the user would hear nothing. Honours an in-flight preset restore
            // by skipping (the preset's APVTS state already wins).
            if (! isRestoringState.load())
            {
                const char* paramId = (m.path == MicPath::Direct  ? "directOn"
                                    : (m.path == MicPath::Outrig  ? "outrigOn"
                                    : (m.path == MicPath::Ambient ? "ambientOn"
                                                                  : nullptr)));
                if (paramId != nullptr)
                    if (auto* p = apvts.getParameter (paramId))
                        p->setValueNotifyingHost (1.0f);
            }
            return;
        }
    }

    irFromSynth = false;
    lastLoadedIRFile = file;
    currentIRSampleRate = 48000.0;  // set from reader below
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (file));
    if (! reader) return;

    juce::AudioBuffer<float> buf ((int) reader->numChannels, (int) reader->lengthInSamples);
    reader->read (&buf, 0, (int) reader->lengthInSamples, 0, true, true);
    currentIRSampleRate = reader->sampleRate;
    loadIRFromBuffer (std::move (buf), currentIRSampleRate, false, false, MicPath::Main);

    // MAIN display = the file's stem (sans extension). Each loadMicPathFromFile call
    // below will overwrite its own slot's display string when a sibling is found; if
    // no sibling exists for a given aux path the previously-loaded display is left
    // intact, matching the convolver's actual contents (sibling auto-load is a no-op
    // when the file is absent — it does not clear the path).
    setPathDisplayName (MicPath::Main, file.getFileNameWithoutExtension());

    // Load sibling multi-mic paths if they exist — old IRs without these files are skipped
    // silently, so this is fully backward-compatible.
    loadMicPathFromFile (file, MicPath::Direct);
    loadMicPathFromFile (file, MicPath::Outrig);
    loadMicPathFromFile (file, MicPath::Ambient);
}

void PingProcessor::reloadSynthIR()
{
    // Reload every raw synth buffer we have — each goes back through the full transform
    // pipeline (reverse/stretch/decay/trim) so stretch/decay knobs re-apply. All four share
    // rawSynthSampleRate.
    if (rawSynthBuffer.getNumSamples() > 0)
        loadIRFromBuffer (rawSynthBuffer, rawSynthSampleRate, true, false, MicPath::Main);
    if (rawSynthDirectBuffer.getNumSamples() > 0)
        loadIRFromBuffer (rawSynthDirectBuffer, rawSynthSampleRate, true, false, MicPath::Direct);
    if (rawSynthOutrigBuffer.getNumSamples() > 0)
        loadIRFromBuffer (rawSynthOutrigBuffer, rawSynthSampleRate, true, false, MicPath::Outrig);
    if (rawSynthAmbientBuffer.getNumSamples() > 0)
        loadIRFromBuffer (rawSynthAmbientBuffer, rawSynthSampleRate, true, false, MicPath::Ambient);
}

void PingProcessor::loadMicPathFromFile (const juce::File& baseIRFile, MicPath path)
{
    if (path == MicPath::Main)   // no-op — Main is loaded via loadIRFromFile
        return;
    if (baseIRFile == juce::File())
        return;

    // Derive the suffix-appended sibling filename (e.g. "Venue.wav" → "Venue_direct.wav").
    const juce::String suffix = (path == MicPath::Direct)  ? "_direct"
                              : (path == MicPath::Outrig)  ? "_outrig"
                              :                              "_ambient";
    const auto stem = baseIRFile.getFileNameWithoutExtension();
    const auto ext  = baseIRFile.getFileExtension();           // includes leading dot
    const auto dir  = baseIRFile.getParentDirectory();
    const auto sibling = dir.getChildFile (stem + suffix + ext);
    if (! sibling.existsAsFile())
    {
        // Extra path not available for this IR — clear the slot so the user can't
        // accidentally enable a stale IR from a previously-loaded file. Without this,
        // loading "FooNoSiblings.wav" after "Bar.wav" (which had every sibling) would
        // leave Bar's _direct / _outrig / _ambient still loaded and toggleable on the
        // front-panel mixer, even though they have nothing to do with Foo.
        clearMicPath (path);
        return;
    }

    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (sibling));
    if (! reader) return;

    juce::AudioBuffer<float> buf ((int) reader->numChannels, (int) reader->lengthInSamples);
    reader->read (&buf, 0, (int) reader->lengthInSamples, 0, true, true);
    loadIRFromBuffer (std::move (buf), reader->sampleRate, /*fromSynth=*/false,
                      /*deferConvolverLoad=*/false, path);

    // Display the sibling's stem (e.g. "Venue_outrig"). Aux suffix is preserved so
    // the user can read off which file actually populates the slot.
    setPathDisplayName (path, sibling.getFileNameWithoutExtension());
}

juce::File PingProcessor::writeSynthIRSetToDirectory (const juce::File& destDir, const juce::String& stem)
{
    if (currentIRBuffer.getNumSamples() == 0) return {};
    if (destDir == juce::File()) return {};

    juce::String safeName = stem.trim();
    if (safeName.isEmpty()) return {};
    safeName = safeName.replaceCharacters ("/\\:*?\"<>|", "----------");

    if (! destDir.exists())
        destDir.createDirectory();

    // Writes a 4-channel 24-bit WAV of `src` to `dest`. Returns true on success.
    auto writeBufferAsWav = [] (const juce::File& dest,
                                const juce::AudioBuffer<float>& src,
                                double sampleRate) -> bool
    {
        if (src.getNumSamples() == 0) return false;
        juce::WavAudioFormat wavFormat;
        auto* rawStream = new juce::FileOutputStream (dest);
        if (! rawStream->openedOk())
        {
            delete rawStream;
            return false;
        }
        std::unique_ptr<juce::AudioFormatWriter> writer (
            wavFormat.createWriterFor (rawStream, sampleRate,
                                       (unsigned int) src.getNumChannels(), 24, {}, 0));
        if (! writer) return false;
        writer->writeFromAudioSampleBuffer (src, 0, src.getNumSamples());
        return true;
    };

    // ── MAIN path ────────────────────────────────────────────────────────────
    auto file = destDir.getChildFile (safeName + ".wav");
    if (! writeBufferAsWav (file, currentIRBuffer, currentIRSampleRate))
        return {};
    // Snapshot current mixer-strip gates so reloading the IR file reproduces
    // the user's intended mix-bus configuration (e.g. "outrig + ambient only").
    const auto gates = getCurrentMixerGates();
    writeIRSynthParamsSidecar (file, lastIRSynthParams, &gates);

    // ── Auxiliary paths (DIRECT / OUTRIG / AMBIENT) ─────────────────────────
    // Each is written as a suffixed sibling WAV next to the MAIN IR. The
    // auxiliary paths don't go through the stretch/decay/reverse pipeline
    // (DIRECT is short-circuited; OUTRIG/AMBIENT currently reuse the MAIN
    // transform but the raw buffer is always bit-identical to the synth
    // output, which is what we want to persist), so we save the raw buffers
    // captured by loadIRFromBuffer. Skip any path whose raw buffer is empty.
    if (rawSynthDirectBuffer.getNumSamples() > 0)
        writeBufferAsWav (destDir.getChildFile (safeName + "_direct.wav"),
                          rawSynthDirectBuffer, rawSynthSampleRate);
    if (rawSynthOutrigBuffer.getNumSamples() > 0)
        writeBufferAsWav (destDir.getChildFile (safeName + "_outrig.wav"),
                          rawSynthOutrigBuffer, rawSynthSampleRate);
    if (rawSynthAmbientBuffer.getNumSamples() > 0)
        writeBufferAsWav (destDir.getChildFile (safeName + "_ambient.wav"),
                          rawSynthAmbientBuffer, rawSynthSampleRate);

    return file;
}

juce::File PingProcessor::saveCurrentIRToFile (const juce::String& name)
{
    return writeSynthIRSetToDirectory (IRManager::getIRFolder(), name);
}

void PingProcessor::clearMicPath (MicPath path)
{
    // Display name reset for every path; per-path slot wipes follow.
    setPathDisplayName (path, "<empty>");

    switch (path)
    {
        case MicPath::Main:
            // MAIN owns shared "current IR" state in addition to its raw synth buffer.
            // Clearing all of it ensures getStateInformation won't re-serialise stale
            // data and reloadSynthIR's `getNumSamples() > 0` guard correctly skips MAIN.
            rawSynthBuffer  .setSize (0, 0);
            currentIRBuffer .setSize (0, 0);
            selectedIRFile   = juce::File();
            lastLoadedIRFile = juce::File();
            irFromSynth      = false;
            mainIRLoaded.store (false);
            break;
        case MicPath::Direct:
            rawSynthDirectBuffer.setSize (0, 0);
            directIRLoaded.store (false);
            break;
        case MicPath::Outrig:
            rawSynthOutrigBuffer.setSize (0, 0);
            outrigIRLoaded.store (false);
            break;
        case MicPath::Ambient:
            rawSynthAmbientBuffer.setSize (0, 0);
            ambientIRLoaded.store (false);
            break;
    }
}

void PingProcessor::loadIRFromBuffer (juce::AudioBuffer<float> buffer, double bufferSampleRate, bool fromSynth, bool deferConvolverLoad, MicPath path)
{
    if (buffer.getNumSamples() == 0) return;

    // ── DIRECT path short-circuit ────────────────────────────────────────────
    // Direct IRs are order-0 only (direct arrival, no reflections). Too short to split
    // into ER/Tail; no decay envelope or silence trim applies. Load raw into 4 mono
    // convolvers and return without touching any MAIN state (currentIRBuffer, selectedIRFile,
    // irFromSynth, etc.).
    if (path == MicPath::Direct)
    {
        if (fromSynth)
        {
            rawSynthDirectBuffer = buffer;
            rawSynthSampleRate   = bufferSampleRate;
            // A freshly synthesised IR has no on-disk filename yet — show "<unsaved>"
            // until finishSaveSynthIR() writes the file and the resulting load updates
            // this slot to the saved stem.
            setPathDisplayName (MicPath::Direct, "<unsaved>");
            if (deferConvolverLoad) return;
        }

        // Expand mono/stereo to 4-channel so we always have 4 mono IR vectors to load.
        // (Synth DIRECT always comes in 4-channel; file DIRECT may be mono/stereo.)
        int numCh  = buffer.getNumChannels();
        int numSmp = buffer.getNumSamples();
        if (numCh < 4)
        {
            juce::AudioBuffer<float> expanded (4, numSmp);
            expanded.clear();
            expanded.copyFrom (0, 0, buffer, 0, 0, numSmp);
            expanded.applyGain (0, 0, numSmp, 0.5f);
            const int srcRCh = (numCh >= 2) ? 1 : 0;
            expanded.copyFrom (3, 0, buffer, srcRCh, 0, numSmp);
            expanded.applyGain (3, 0, numSmp, 0.5f);
            expanded.applyGain (juce::Decibels::decibelsToGain (-15.0f));
            buffer = std::move (expanded);
        }

        // Arm wet fade before kicking off background loads (see MAIN path for rationale).
        irLoadFadeSamplesRemaining.store (kIRLoadFadeSamples);

        juce::dsp::Convolution* tsDirect[] = { &tsDirectConvLL, &tsDirectConvRL, &tsDirectConvLR, &tsDirectConvRR };
        for (int c = 0; c < 4; ++c)
        {
            juce::AudioBuffer<float> mono (1, buffer.getNumSamples());
            mono.copyFrom (0, 0, buffer, c, 0, buffer.getNumSamples());
            tsDirect[c]->loadImpulseResponse (std::move (mono), bufferSampleRate,
                juce::dsp::Convolution::Stereo::no, juce::dsp::Convolution::Trim::no,
                juce::dsp::Convolution::Normalise::no);
        }
        directIRLoaded.store (true);
        return;
    }

    // MAIN / OUTRIG / AMBIENT share the same pipeline from here on. Per-path branches
    // below only affect (a) which state to mutate (Main owns currentIRBuffer etc.) and
    // (b) which 8-convolver set receives the final IR.
    const bool isMainPath = (path == MicPath::Main);

    if (isMainPath)
        currentIRSampleRate = bufferSampleRate;

    if (fromSynth)
    {
        if (isMainPath)
        {
            irFromSynth = true;
            selectedIRFile = juce::File();   // clear any previous file selection
            synthesizedIRSampleRate = bufferSampleRate;
        }

        // Auto-trim trailing silence: scan for last sample above -90 dB, add 500 ms safety tail.
        // Must run BEFORE rawSynthBuffer is saved so the stored raw copy is already trimmed.
        {
            const int nSamples = buffer.getNumSamples();
            const int nCh      = buffer.getNumChannels();

            float peak = 1.0e-6f;
            for (int ch = 0; ch < nCh; ++ch)
            {
                const float* p = buffer.getReadPointer (ch);
                for (int i = 0; i < nSamples; ++i)
                    peak = juce::jmax (peak, std::abs (p[i]));
            }

            // 10^(-90/20) ≈ 3.162e-5 — used both as (peak × factor) relative threshold
            // and as an absolute −90 dBFS floor cap (for synth IRs whose peak > 1.0).
            constexpr float kSilenceFactor = 3.1622777e-5f;
            const float threshold = juce::jmin (peak * kSilenceFactor, kSilenceFactor);

            int lastSignificant = 0;
            for (int ch = 0; ch < nCh; ++ch)
            {
                const float* p = buffer.getReadPointer (ch);
                for (int i = nSamples - 1; i >= 0; --i)
                {
                    if (std::abs (p[i]) > threshold)
                    {
                        lastSignificant = juce::jmax (lastSignificant, i);
                        break;
                    }
                }
            }

            const int safetyTail = (int) (0.5 * bufferSampleRate); // 500 ms (also fade length)
            const int minLen     = (int) (0.3 * bufferSampleRate); // 300 ms floor
            int newLen = juce::jmin (lastSignificant + safetyTail + 1, nSamples);
            newLen     = juce::jmax (newLen, minLen);

            if (newLen < nSamples)
            {
                juce::AudioBuffer<float> trimmed (nCh, newLen);
                for (int ch = 0; ch < nCh; ++ch)
                    trimmed.copyFrom (ch, 0, buffer, ch, 0, newLen);
                buffer = std::move (trimmed);

                // End-fade: cosine fade over the last safetyTail (500 ms) samples to smooth
                // the hard cut-point introduced above. Applied ONLY when we actually truncated
                // so reloading an already-trimmed buffer is idempotent (no compounding cos²
                // attenuation that would become audible under heavy output boost).
                const int curLen    = buffer.getNumSamples();
                const int curCh     = buffer.getNumChannels();
                const int fadeSamps = juce::jmin (safetyTail, curLen);
                const int fadeStart = curLen - fadeSamps;
                for (int ch = 0; ch < curCh; ++ch)
                {
                    float* p = buffer.getWritePointer (ch);
                    for (int i = fadeStart; i < curLen; ++i)
                    {
                        const float t = (float)(i - fadeStart) / (float)juce::jmax (fadeSamps, 1);
                        p[i] *= 0.5f * (1.0f + std::cos (juce::MathConstants<float>::pi * t));
                    }
                }
            }
        }

        // save raw copy before any transforms (silence already trimmed + faded) into the right slot
        if      (path == MicPath::Main)    rawSynthBuffer         = buffer;
        else if (path == MicPath::Outrig)  rawSynthOutrigBuffer   = buffer;
        else if (path == MicPath::Ambient) rawSynthAmbientBuffer  = buffer;
        rawSynthSampleRate = bufferSampleRate;

        // A freshly synthesised IR has no on-disk filename yet — show "<unsaved>"
        // for the slot that just got synthesised. Once finishSaveSynthIR() writes the
        // WAV and re-loads it through loadIRFromFile()/loadMicPathFromFile(), the
        // matching display string is overwritten with the stem.
        setPathDisplayName (path, "<unsaved>");

        // During initial session load (audioEnginePrepared = false), setStateInformation calls
        // us with deferConvolverLoad = true.  We've saved the raw buffer; prepareToPlay will
        // later post a callAsync that calls reloadSynthIR(), which re-enters loadIRFromBuffer
        // without the defer flag and completes the full transform + loadImpulseResponse sequence.
        // This prevents the race between loadImpulseResponse background threads and reset().
        if (deferConvolverLoad)
            return;
    }
    else if (isMainPath)
        irFromSynth = false;

    // Optional: apply reverse
    if (reverse)
    {
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        {
            float* p = buffer.getWritePointer (ch);
            int n = buffer.getNumSamples();
            for (int i = 0, j = n - 1; i < j; ++i, --j)
                std::swap (p[i], p[j]);
        }

        // Trim start of reversed IR (skip initial silence/long tail)
        float trimFrac = apvts.getRawParameterValue (IDs::reverseTrim)->load();
        if (trimFrac > 0.001f)
        {
            int n = buffer.getNumSamples();
            int startIdx = (int) (trimFrac * (float) n);
            if (startIdx > 0 && startIdx < n)
            {
                int newLen = n - startIdx;
                if (newLen >= 64)
                {
                    juce::AudioBuffer<float> trimmed (buffer.getNumChannels(), newLen);
                    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                        trimmed.copyFrom (ch, 0, buffer, ch, startIdx, newLen);
                    buffer = std::move (trimmed);
                }
            }
        }
    }

    // Stretch: time-scale IR to 50%..200% of original length (1.0 = natural)
    float stretchFactor = apvts.getRawParameterValue (IDs::stretch)->load();
    int origLen = buffer.getNumSamples();
    int newLen = (int) (origLen * stretchFactor);
    if (newLen < 64) newLen = 64;
    if (newLen != origLen)
    {
        juce::AudioBuffer<float> stretched (buffer.getNumChannels(), newLen);
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        {
            const float* src = buffer.getReadPointer (ch);
            float* dst = stretched.getWritePointer (ch);
            for (int i = 0; i < newLen; ++i)
            {
                float srcIdx = (float) i * (float) origLen / (float) newLen;
                int i0 = (int) srcIdx;
                int i1 = juce::jmin (i0 + 1, origLen - 1);
                float f = srcIdx - (float) i0;
                dst[i] = src[i0] * (1.0f - f) + src[i1] * f;
            }
        }
        buffer = std::move (stretched);
    }

    // Decay: exponential fade-out envelope (0% = flat, 100% = heavily damped) — UI reversed: left = more damped
    float decayParam = 1.0f - apvts.getRawParameterValue (IDs::decay)->load();
    int N = buffer.getNumSamples();
    if (decayParam > 0.001f && N > 0)
    {
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        {
            float* p = buffer.getWritePointer (ch);
            for (int i = 0; i < N; ++i)
            {
                float t = (float) i / (float) N;
                float env = std::exp (-decayParam * 6.0f * t);
                p[i] *= env;
            }
        }
    }

    // Trim trailing silence from ALL IRs — critical for synthesised factory IRs which are
    // allocated at 8×RT60 (up to 60 s) but contain only 8–15 s of actual reverb signal.
    // File-loaded factory IRs skip the fromSynth silence-trim block above, so without this
    // they arrive with a huge silent tail that makes the NUPC background thread unable to
    // keep up at small buffer sizes (persistent glitching). Threshold: −90 dB below peak.
    // Synth IRs are already trimmed in the fromSynth block; this is a fast no-op for them.
    {
        const int nSamples = buffer.getNumSamples();
        const int nCh      = buffer.getNumChannels();

        float peak = 1.0e-6f;
        for (int ch = 0; ch < nCh; ++ch)
        {
            const float* p = buffer.getReadPointer (ch);
            for (int i = 0; i < nSamples; ++i)
                peak = juce::jmax (peak, std::abs (p[i]));
        }

        // 10^(-90/20) ≈ 3.162e-5 — used both as (peak × factor) relative threshold
        // and as an absolute −90 dBFS floor cap (for synth IRs whose peak > 1.0).
        constexpr float kSilenceFactor = 3.1622777e-5f;
        const float threshold = juce::jmin (peak * kSilenceFactor, kSilenceFactor);

        int lastSignificant = 0;
        for (int ch = 0; ch < nCh; ++ch)
        {
            const float* p = buffer.getReadPointer (ch);
            for (int i = nSamples - 1; i >= 0; --i)
            {
                if (std::abs (p[i]) > threshold)
                {
                    lastSignificant = juce::jmax (lastSignificant, i);
                    break;
                }
            }
        }

        const int safetyTail = (int) (0.5 * bufferSampleRate); // 500 ms (also fade length)
        const int minLen     = (int) (0.3 * bufferSampleRate); // 300 ms floor
        int newLen = juce::jmin (lastSignificant + safetyTail + 1, nSamples);
        newLen     = juce::jmax (newLen, minLen);

        if (newLen < nSamples)
        {
            juce::AudioBuffer<float> trimmed (nCh, newLen);
            for (int ch = 0; ch < nCh; ++ch)
                trimmed.copyFrom (ch, 0, buffer, ch, 0, newLen);
            buffer = std::move (trimmed);

            // End-fade: cosine fade over the last safetyTail (500 ms) samples to smooth
            // the hard cut-point introduced above. Applied ONLY when we actually truncated —
            // reloading an already-trimmed IR (factory preset, user-saved WAV, rawSynthBuffer)
            // hits newLen == nSamples, skips both this branch and the fade, and therefore
            // produces a bit-identical buffer on every reload. Without this gate the fade
            // would compound (cos², cos³, …) and become audible under heavy output boost.
            const int curLen    = buffer.getNumSamples();
            const int curCh     = buffer.getNumChannels();
            const int fadeSamps = juce::jmin (safetyTail, curLen);
            const int fadeStart = curLen - fadeSamps;
            for (int ch = 0; ch < curCh; ++ch)
            {
                float* p = buffer.getWritePointer (ch);
                for (int i = fadeStart; i < curLen; ++i)
                {
                    const float t = (float)(i - fadeStart) / (float)juce::jmax (fadeSamps, 1);
                    p[i] *= 0.5f * (1.0f + std::cos (juce::MathConstants<float>::pi * t));
                }
            }
        }
    }

    if (isMainPath)
        currentIRBuffer = buffer;   // store original data for waveform display before any channel expansion
    int numCh = buffer.getNumChannels();
    int fullLen = buffer.getNumSamples();

    // Expand mono/stereo IR to true-stereo 4-channel so all IRs share the same signal path.
    // Cross-channels (iRL, iLR) are zero: a plain stereo file has no cross-channel IR content.
    // Non-zero channels are scaled ×0.5 to cancel the ×2.0 trueStereoWetGain applied in
    // processBlock, keeping output level consistent with the former stereo path.
    // An additional −9 dB trim compensates for the observed level excess of file-based IRs
    // relative to synthesised IRs. Synth IRs (numCh == 4) bypass this block entirely.
    if (numCh < 4)
    {
        juce::AudioBuffer<float> expanded (4, fullLen);
        expanded.clear();                                                 // zeroes iRL (ch1) and iLR (ch2)
        expanded.copyFrom (0, 0, buffer, 0, 0, fullLen);                  // iLL = IR_L
        expanded.applyGain (0, 0, fullLen, 0.5f);
        const int srcRCh = (numCh >= 2) ? 1 : 0;                         // use ch0 for mono sources
        expanded.copyFrom (3, 0, buffer, srcRCh, 0, fullLen);             // iRR = IR_R (or IR_L for mono)
        expanded.applyGain (3, 0, fullLen, 0.5f);
        expanded.applyGain (juce::Decibels::decibelsToGain (-15.0f));     // −15 dB: file IR level trim
        buffer = std::move (expanded);
        numCh  = 4;
    }

    const bool synthErOnly = fromSynth && lastIRSynthParams.er_only;
    const double crossoverSeconds = fromSynth ? 0.085 : 0.080;
    const double fadeSeconds = fromSynth ? 0.020 : 0.010;
    const int crossoverSamples = synthErOnly ? fullLen : static_cast<int> (crossoverSeconds * bufferSampleRate);
    const int fadeLength = static_cast<int> (fadeSeconds * bufferSampleRate);

    if (numCh >= 4)
    {
        auto makeMonoEr = [&] (int ch)
        {
            int erLen = fullLen <= crossoverSamples ? fullLen : crossoverSamples + fadeLength;
            juce::AudioBuffer<float> m (1, erLen);
            m.copyFrom (0, 0, buffer, ch, 0, juce::jmin (erLen, fullLen));
            if (fullLen > crossoverSamples)
                for (int i = 0; i < fadeLength && (crossoverSamples + i) < erLen; ++i)
                    m.applyGain (crossoverSamples + i, 1, 1.0f - (float) i / (float) fadeLength);
            return m;
        };
        auto makeMonoTail = [&] (int ch)
        {
            if (synthErOnly || fullLen <= crossoverSamples)
            {
                juce::AudioBuffer<float> m (1, juce::jmax (64, fadeLength * 2));
                m.clear();
                return m;
            }
            int tailLen = fullLen - crossoverSamples;
            juce::AudioBuffer<float> m (1, tailLen);
            m.copyFrom (0, 0, buffer, ch, crossoverSamples, tailLen);
            for (int i = 0; i < juce::jmin (fadeLength, tailLen); ++i)
                m.applyGain (i, 1, (float) i / (float) fadeLength);
            return m;
        };
        // Arm the wet-signal crossfade BEFORE kicking off any background IR loads.
        // processBlock will fade the wet bus from silence for kIRLoadFadeSamples samples,
        // covering the window during which different convolvers may be running different IRs.
        irLoadFadeSamplesRemaining.store (kIRLoadFadeSamples);

        // Select the destination 8-convolver set based on which mic path this load is for.
        juce::dsp::Convolution* tsEr[4];
        juce::dsp::Convolution* tsTail[4];
        if (path == MicPath::Main)
        {
            tsEr[0]   = &tsErConvLL;   tsEr[1]   = &tsErConvRL;   tsEr[2]   = &tsErConvLR;   tsEr[3]   = &tsErConvRR;
            tsTail[0] = &tsTailConvLL; tsTail[1] = &tsTailConvRL; tsTail[2] = &tsTailConvLR; tsTail[3] = &tsTailConvRR;
        }
        else if (path == MicPath::Outrig)
        {
            tsEr[0]   = &tsOutrigErConvLL;   tsEr[1]   = &tsOutrigErConvRL;   tsEr[2]   = &tsOutrigErConvLR;   tsEr[3]   = &tsOutrigErConvRR;
            tsTail[0] = &tsOutrigTailConvLL; tsTail[1] = &tsOutrigTailConvRL; tsTail[2] = &tsOutrigTailConvLR; tsTail[3] = &tsOutrigTailConvRR;
        }
        else // Ambient
        {
            tsEr[0]   = &tsAmbErConvLL;   tsEr[1]   = &tsAmbErConvRL;   tsEr[2]   = &tsAmbErConvLR;   tsEr[3]   = &tsAmbErConvRR;
            tsTail[0] = &tsAmbTailConvLL; tsTail[1] = &tsAmbTailConvRL; tsTail[2] = &tsAmbTailConvLR; tsTail[3] = &tsAmbTailConvRR;
        }

        for (int c = 0; c < 4; ++c)
            tsEr[c]->loadImpulseResponse (makeMonoEr (c), bufferSampleRate,
                juce::dsp::Convolution::Stereo::no, juce::dsp::Convolution::Trim::no,
                juce::dsp::Convolution::Normalise::no);
        for (int c = 0; c < 4; ++c)
            tsTail[c]->loadImpulseResponse (makeMonoTail (c), bufferSampleRate,
                juce::dsp::Convolution::Stereo::no, juce::dsp::Convolution::Trim::no,
                juce::dsp::Convolution::Normalise::no);

        if      (path == MicPath::Main)    mainIRLoaded   .store (true);
        else if (path == MicPath::Outrig)  outrigIRLoaded .store (true);
        else if (path == MicPath::Ambient) ambientIRLoaded.store (true);

        // Build a stereo combined-tail IR for the regular tailConvolver (main path only —
        // this convolver drives the waveform thumbnail / secondary tail. Non-main mic paths
        // don't contribute to that display).
        if (isMainPath)
        {
            auto t0 = makeMonoTail (0);   // iLL tail
            auto t1 = makeMonoTail (1);   // iRL tail  (== t0)
            auto t2 = makeMonoTail (2);   // iLR tail
            auto t3 = makeMonoTail (3);   // iRR tail  (== t2)
            const int tailLen = t0.getNumSamples();
            juce::AudioBuffer<float> combinedTail (2, tailLen);
            for (int i = 0; i < tailLen; ++i)
            {
                combinedTail.setSample (0, i, t0.getSample (0, i) + t1.getSample (0, i));
                combinedTail.setSample (1, i, t2.getSample (0, i) + t3.getSample (0, i));
            }
            tailConvolver.loadImpulseResponse (std::move (combinedTail), bufferSampleRate,
                juce::dsp::Convolution::Stereo::yes,
                juce::dsp::Convolution::Trim::no,
                juce::dsp::Convolution::Normalise::no);
        }
    }
}


static void irSynthParamsToXml (const IRSynthParams& p, juce::XmlElement& parent)
{
    auto* ir = parent.createNewChildElement ("irSynthParams");
    if (! ir) return;
    ir->setAttribute ("shape", juce::String (p.shape));
    ir->setAttribute ("width", p.width);
    ir->setAttribute ("depth", p.depth);
    ir->setAttribute ("height", p.height);

    // Shape proportion parameters (v2.8.0 polygon geometry). Older sidecars
    // that predate this feature fall back to IRSynthParams struct defaults on
    // read — this is safe for "Rectangular" (values are unused by calcRefs)
    // and produces a sensible default footprint for non-rectangular shapes.
    ir->setAttribute ("shapeNavePct",   p.shapeNavePct);
    ir->setAttribute ("shapeTrptPct",   p.shapeTrptPct);
    ir->setAttribute ("shapeTaper",     p.shapeTaper);
    ir->setAttribute ("shapeCornerCut", p.shapeCornerCut);
    ir->setAttribute ("floor", juce::String (p.floor_material));
    ir->setAttribute ("ceiling", juce::String (p.ceiling_material));
    ir->setAttribute ("wall", juce::String (p.wall_material));
    ir->setAttribute ("windows", p.window_fraction);
    ir->setAttribute ("audience", p.audience);
    ir->setAttribute ("diffusion", p.diffusion);
    ir->setAttribute ("vault", juce::String (p.vault_type));
    ir->setAttribute ("organ", p.organ_case);
    ir->setAttribute ("balconies", p.balconies);
    ir->setAttribute ("temp", p.temperature);
    ir->setAttribute ("humidity", p.humidity);
    ir->setAttribute ("slx", p.source_lx);
    ir->setAttribute ("sly", p.source_ly);
    ir->setAttribute ("srx", p.source_rx);
    ir->setAttribute ("sry", p.source_ry);
    ir->setAttribute ("rlx", p.receiver_lx);
    ir->setAttribute ("rly", p.receiver_ly);
    ir->setAttribute ("rrx", p.receiver_rx);
    ir->setAttribute ("rry", p.receiver_ry);
    ir->setAttribute ("spkl", p.spkl_angle);
    ir->setAttribute ("spkr", p.spkr_angle);
    ir->setAttribute ("micl", p.micl_angle);
    ir->setAttribute ("micr", p.micr_angle);
    ir->setAttribute ("micPat", juce::String (p.mic_pattern));
    ir->setAttribute ("erOnly", p.er_only);
    ir->setAttribute ("sr", p.sample_rate);
    ir->setAttribute ("bakeERTail", p.bake_er_tail_balance);
    ir->setAttribute ("bakedERGain", p.baked_er_gain);
    ir->setAttribute ("bakedTailGain", p.baked_tail_gain);

    // ── Multi-mic paths (feature/multi-mic-paths) ────────────────────────────
    // New attributes — older sidecars that don't contain them fall back to
    // the IRSynthParams struct defaults when read by irSynthParamsFromXml.
    ir->setAttribute ("directOn",  p.direct_enabled);
    ir->setAttribute ("outrigOn",  p.outrig_enabled);
    ir->setAttribute ("ambientOn", p.ambient_enabled);

    // Experimental early-reflection toggles (see IRSynthEngine.h). Absent in
    // older sidecars — defaults keep historical behaviour for MAIN/OUTRIG/
    // AMBIENT and the new direct_max_order default (1) for DIRECT.
    ir->setAttribute ("directMaxOrder", p.direct_max_order);
    ir->setAttribute ("lambertScatter", p.lambert_scatter_enabled);
    ir->setAttribute ("spkDirFull",     p.spk_directivity_full);

    ir->setAttribute ("outrigLx",     p.outrig_lx);
    ir->setAttribute ("outrigLy",     p.outrig_ly);
    ir->setAttribute ("outrigRx",     p.outrig_rx);
    ir->setAttribute ("outrigRy",     p.outrig_ry);
    ir->setAttribute ("outrigLang",   p.outrig_langle);
    ir->setAttribute ("outrigRang",   p.outrig_rangle);
    ir->setAttribute ("outrigHeight", p.outrig_height);
    ir->setAttribute ("outrigPat",    juce::String (p.outrig_pattern));
    ir->setAttribute ("outrigLtilt",  p.outrig_ltilt);
    ir->setAttribute ("outrigRtilt",  p.outrig_rtilt);

    ir->setAttribute ("ambientLx",     p.ambient_lx);
    ir->setAttribute ("ambientLy",     p.ambient_ly);
    ir->setAttribute ("ambientRx",     p.ambient_rx);
    ir->setAttribute ("ambientRy",     p.ambient_ry);
    ir->setAttribute ("ambientLang",   p.ambient_langle);
    ir->setAttribute ("ambientRang",   p.ambient_rangle);
    ir->setAttribute ("ambientHeight", p.ambient_height);
    ir->setAttribute ("ambientPat",    juce::String (p.ambient_pattern));
    ir->setAttribute ("ambientLtilt",  p.ambient_ltilt);
    ir->setAttribute ("ambientRtilt",  p.ambient_rtilt);

    // 3D mic-tilt elevations (radians). MAIN's L/R tilts plus the rigid
    // Decca array tilt. See IRSynthEngine::directivityCos and the
    // "3D microphone polar patterns + tilt" section in CLAUDE.md.
    ir->setAttribute ("miclTilt",     p.micl_tilt);
    ir->setAttribute ("micrTilt",     p.micr_tilt);

    // Decca Tree capture mode (see IRSynthEngine.h). Older sidecars that
    // predate this feature fall back to the IRSynthParams struct defaults
    // (disabled, centred at 0.5/0.65, angle = -π/2).
    ir->setAttribute ("deccaOn",      p.main_decca_enabled);
    ir->setAttribute ("deccaCx",      p.decca_cx);
    ir->setAttribute ("deccaCy",      p.decca_cy);
    ir->setAttribute ("deccaAng",     p.decca_angle);
    ir->setAttribute ("deccaCtrGain", p.decca_centre_gain);
    ir->setAttribute ("deccaToeOut",  p.decca_toe_out);
    ir->setAttribute ("deccaTilt",    p.decca_tilt);

    // Floor-plan Option-mirror axis preference (UI-only, not consumed by the
    // engine). 0 = vertical (x = 0.5 mirror — L/R pairs), 1 = horizontal
    // (y = 0.5 mirror — front/back pairs). Older sidecars without this
    // attribute default to 0 (vertical) for backward compatibility.
    ir->setAttribute ("mirrorAxis",   p.mirror_axis);
}

static void synthesizedIRToXml (const juce::AudioBuffer<float>& buf, double sampleRate, juce::XmlElement& elem)
{
    if (buf.getNumSamples() == 0 || buf.getNumChannels() == 0) return;
    juce::MemoryBlock block;
    const int numCh = buf.getNumChannels();
    const int numSamples = buf.getNumSamples();
    block.append (&numCh, sizeof (int));
    block.append (&numSamples, sizeof (int));
    block.append (&sampleRate, sizeof (double));
    for (int ch = 0; ch < numCh; ++ch)
        block.append (buf.getReadPointer (ch), (size_t) numSamples * sizeof (float));
    juce::String b64 = juce::Base64::toBase64 (block.getData(), block.getSize());
    elem.setAttribute ("data", b64);
}

static bool synthesizedIRFromXml (const juce::XmlElement& xml, juce::AudioBuffer<float>& outBuf, double& outSampleRate)
{
    juce::String b64 = xml.getStringAttribute ("data");
    if (b64.isEmpty()) return false;
    juce::MemoryBlock block;
    juce::MemoryOutputStream mos (block, false);
    if (! juce::Base64::convertFromBase64 (mos, b64)) return false;
    const size_t minSize = sizeof (int) + sizeof (int) + sizeof (double);
    if (block.getSize() < minSize) return false;
    const char* data = static_cast<const char*> (block.getData());
    int numCh, numSamples;
    double sampleRate;
    memcpy (&numCh, data, sizeof (int));
    memcpy (&numSamples, data + sizeof (int), sizeof (int));
    memcpy (&sampleRate, data + sizeof (int) * 2, sizeof (double));
    size_t expectedSize = minSize + (size_t) numCh * (size_t) numSamples * sizeof (float);
    if (numCh < 1 || numCh > 8 || numSamples < 1 || numSamples > 2000000 || block.getSize() != expectedSize)
        return false;
    outBuf.setSize (numCh, numSamples);
    const float* src = reinterpret_cast<const float*> (data + minSize);
    for (int ch = 0; ch < numCh; ++ch)
        outBuf.copyFrom (ch, 0, src + ch * numSamples, numSamples);
    outSampleRate = sampleRate;
    return true;
}

static IRSynthParams irSynthParamsFromXml (const juce::XmlElement* ir)
{
    IRSynthParams p;
    if (! ir) return p;
    p.shape          = ir->getStringAttribute ("shape", "Rectangular").toStdString();
    // v2.8.0 shape-string migration. Old presets / sidecars referenced shapes
    // that the polygon engine no longer supports:
    //   "L-shaped"   → "Rectangular"      (L-shape had no acoustic model in
    //                                     the rectangular scalar engine either
    //                                     — it was floor-plan-only; the nearest
    //                                     acoustic equivalent is a plain box.)
    //   "Cylindrical" → "Circular Hall"   (1:1 rename — same intent.)
    if (p.shape == "L-shaped")   p.shape = "Rectangular";
    if (p.shape == "Cylindrical") p.shape = "Circular Hall";

    p.width          = ir->getDoubleAttribute ("width", 28.0);
    p.depth          = ir->getDoubleAttribute ("depth", 16.0);
    p.height         = ir->getDoubleAttribute ("height", 12.0);

    // Shape proportion parameters (v2.8.0). Fall back to IRSynthParams struct
    // defaults when absent (older sidecars / presets).
    {
        IRSynthParams sd;
        p.shapeNavePct   = ir->getDoubleAttribute ("shapeNavePct",   sd.shapeNavePct);
        p.shapeTrptPct   = ir->getDoubleAttribute ("shapeTrptPct",   sd.shapeTrptPct);
        p.shapeTaper     = ir->getDoubleAttribute ("shapeTaper",     sd.shapeTaper);
        p.shapeCornerCut = ir->getDoubleAttribute ("shapeCornerCut", sd.shapeCornerCut);
    }
    p.floor_material = ir->getStringAttribute ("floor", "Hardwood floor").toStdString();
    p.ceiling_material = ir->getStringAttribute ("ceiling", "Painted plaster").toStdString();
    p.wall_material  = ir->getStringAttribute ("wall", "Concrete / bare brick").toStdString();
    p.window_fraction = ir->getDoubleAttribute ("windows", 0.27);
    p.audience       = ir->getDoubleAttribute ("audience", 0.45);
    p.diffusion      = ir->getDoubleAttribute ("diffusion", 0.40);
    p.vault_type     = ir->getStringAttribute ("vault", "Groin / cross vault  (Lyndhurst Hall)").toStdString();
    p.organ_case     = ir->getDoubleAttribute ("organ", 0.59);
    p.balconies      = ir->getDoubleAttribute ("balconies", 0.54);
    p.temperature    = ir->getDoubleAttribute ("temp", 20.0);
    p.humidity       = ir->getDoubleAttribute ("humidity", 50.0);
    p.source_lx      = ir->getDoubleAttribute ("slx", 0.25);
    p.source_ly      = ir->getDoubleAttribute ("sly", 0.5);
    p.source_rx      = ir->getDoubleAttribute ("srx", 0.75);
    p.source_ry      = ir->getDoubleAttribute ("sry", 0.5);
    p.receiver_lx     = ir->getDoubleAttribute ("rlx", 0.35);
    p.receiver_ly     = ir->getDoubleAttribute ("rly", 0.8);
    p.receiver_rx     = ir->getDoubleAttribute ("rrx", 0.65);
    p.receiver_ry     = ir->getDoubleAttribute ("rry", 0.8);
    p.spkl_angle     = ir->getDoubleAttribute ("spkl", 1.57079632679);
    p.spkr_angle     = ir->getDoubleAttribute ("spkr", 1.57079632679);
    p.micl_angle     = ir->getDoubleAttribute ("micl", -2.35619449019);
    p.micr_angle     = ir->getDoubleAttribute ("micr", -0.785398163397);
    p.mic_pattern    = ir->getStringAttribute ("micPat", "cardioid").toStdString();
    p.er_only        = ir->getBoolAttribute ("erOnly", false);
    p.sample_rate    = ir->getIntAttribute ("sr", 48000);
    p.bake_er_tail_balance = ir->getBoolAttribute ("bakeERTail", false);
    p.baked_er_gain  = ir->getDoubleAttribute ("bakedERGain", 1.0);
    p.baked_tail_gain = ir->getDoubleAttribute ("bakedTailGain", 1.0);

    // Multi-mic paths (attributes may be absent in older sidecars — fall back
    // to IRSynthParams struct defaults).
    IRSynthParams defaults;
    p.direct_enabled  = ir->getBoolAttribute ("directOn",  defaults.direct_enabled);
    p.outrig_enabled  = ir->getBoolAttribute ("outrigOn",  defaults.outrig_enabled);
    p.ambient_enabled = ir->getBoolAttribute ("ambientOn", defaults.ambient_enabled);

    p.direct_max_order         = ir->getIntAttribute  ("directMaxOrder", defaults.direct_max_order);
    p.lambert_scatter_enabled  = ir->getBoolAttribute ("lambertScatter", defaults.lambert_scatter_enabled);
    p.spk_directivity_full     = ir->getBoolAttribute ("spkDirFull",     defaults.spk_directivity_full);

    p.outrig_lx      = ir->getDoubleAttribute ("outrigLx",     defaults.outrig_lx);
    p.outrig_ly      = ir->getDoubleAttribute ("outrigLy",     defaults.outrig_ly);
    p.outrig_rx      = ir->getDoubleAttribute ("outrigRx",     defaults.outrig_rx);
    p.outrig_ry      = ir->getDoubleAttribute ("outrigRy",     defaults.outrig_ry);
    p.outrig_langle  = ir->getDoubleAttribute ("outrigLang",   defaults.outrig_langle);
    p.outrig_rangle  = ir->getDoubleAttribute ("outrigRang",   defaults.outrig_rangle);
    p.outrig_height  = ir->getDoubleAttribute ("outrigHeight", defaults.outrig_height);
    p.outrig_pattern = ir->getStringAttribute ("outrigPat",    juce::String (defaults.outrig_pattern)).toStdString();
    // Pre-tilt sidecars: missing attribute → 0.0 (legacy behaviour, mic
    // facing horizontal). Do NOT fall back to the IRSynthParams default
    // (-π/6) — that would silently retune every old preset.
    p.outrig_ltilt   = ir->getDoubleAttribute ("outrigLtilt",  0.0);
    p.outrig_rtilt   = ir->getDoubleAttribute ("outrigRtilt",  0.0);

    p.ambient_lx     = ir->getDoubleAttribute ("ambientLx",     defaults.ambient_lx);
    p.ambient_ly     = ir->getDoubleAttribute ("ambientLy",     defaults.ambient_ly);
    p.ambient_rx     = ir->getDoubleAttribute ("ambientRx",     defaults.ambient_rx);
    p.ambient_ry     = ir->getDoubleAttribute ("ambientRy",     defaults.ambient_ry);
    p.ambient_langle = ir->getDoubleAttribute ("ambientLang",   defaults.ambient_langle);
    p.ambient_rangle = ir->getDoubleAttribute ("ambientRang",   defaults.ambient_rangle);
    p.ambient_height = ir->getDoubleAttribute ("ambientHeight", defaults.ambient_height);
    p.ambient_pattern = ir->getStringAttribute ("ambientPat",   juce::String (defaults.ambient_pattern)).toStdString();
    p.ambient_ltilt  = ir->getDoubleAttribute ("ambientLtilt",  0.0);
    p.ambient_rtilt  = ir->getDoubleAttribute ("ambientRtilt",  0.0);

    // MAIN mic 3D tilt (radians). Pre-tilt sidecars: 0.0 fallback.
    p.micl_tilt = ir->getDoubleAttribute ("miclTilt", 0.0);
    p.micr_tilt = ir->getDoubleAttribute ("micrTilt", 0.0);

    // Decca Tree capture mode (attributes absent in pre-Decca sidecars).
    p.main_decca_enabled = ir->getBoolAttribute   ("deccaOn",      defaults.main_decca_enabled);
    p.decca_cx           = ir->getDoubleAttribute ("deccaCx",      defaults.decca_cx);
    p.decca_cy           = ir->getDoubleAttribute ("deccaCy",      defaults.decca_cy);
    p.decca_angle        = ir->getDoubleAttribute ("deccaAng",     defaults.decca_angle);
    p.decca_centre_gain  = ir->getDoubleAttribute ("deccaCtrGain", defaults.decca_centre_gain);
    p.decca_toe_out      = ir->getDoubleAttribute ("deccaToeOut",  defaults.decca_toe_out);
    p.decca_tilt         = ir->getDoubleAttribute ("deccaTilt",    0.0);

    // Option-mirror axis preference. Pre-v2.7.7 sidecars default to 0
    // (vertical mirror — the historical behaviour).
    p.mirror_axis        = ir->getIntAttribute    ("mirrorAxis",   defaults.mirror_axis);

    return p;
}

static void writeIRSynthParamsSidecar (const juce::File& wavFile, const IRSynthParams& p,
                                       const PingProcessor::MixerGateState* gates)
{
    juce::XmlElement root ("PingIRSynth");
    irSynthParamsToXml (p, root);
    if (gates != nullptr)
    {
        // <mixerGates> is an independent child element. Older sidecars (or any
        // sidecar saved without a gate snapshot) simply omit it; readers
        // default to the current APVTS state.
        auto* mg = root.createNewChildElement ("mixerGates");
        mg->setAttribute ("mainOn",    gates->mainOn);
        mg->setAttribute ("directOn",  gates->directOn);
        mg->setAttribute ("outrigOn",  gates->outrigOn);
        mg->setAttribute ("ambientOn", gates->ambientOn);
    }
    root.writeTo (wavFile.getSiblingFile (wavFile.getFileNameWithoutExtension() + ".ping"));
}

juce::File PingProcessor::getSidecarFor (const juce::File& irFile)
{
    juce::String stem = irFile.getFileNameWithoutExtension();
    // Strip multi-mic aux suffix so sidecar lookup always points at the MAIN
    // sidecar (which is the only place a .ping is ever written).
    for (const char* sfx : { "_direct", "_outrig", "_ambient" })
        if (stem.endsWithIgnoreCase (sfx))
        {
            stem = stem.dropLastCharacters ((int) std::strlen (sfx));
            break;
        }
    return irFile.getSiblingFile (stem + ".ping");
}

bool PingProcessor::hasAuxSuffix (const juce::File& irFile)
{
    const auto stem = irFile.getFileNameWithoutExtension();
    for (const char* sfx : { "_direct", "_outrig", "_ambient" })
        if (stem.endsWithIgnoreCase (sfx))
            return true;
    return false;
}

IRSynthParams PingProcessor::loadIRSynthParamsFromSidecar (const juce::File& irFile)
{
    auto sidecar = getSidecarFor (irFile);
    if (! sidecar.existsAsFile()) return IRSynthParams();
    if (auto xml = juce::parseXML (sidecar))
    {
        if (auto* ir = xml->getChildByName ("irSynthParams"))
            return irSynthParamsFromXml (ir);
    }
    return IRSynthParams();
}

bool PingProcessor::tryLoadMixerGatesFromSidecar (const juce::File& irFile, MixerGateState& out)
{
    auto sidecar = getSidecarFor (irFile);
    if (! sidecar.existsAsFile()) return false;
    auto xml = juce::parseXML (sidecar);
    if (xml == nullptr) return false;
    auto* mg = xml->getChildByName ("mixerGates");
    if (mg == nullptr) return false;
    out.mainOn    = mg->getBoolAttribute ("mainOn",    out.mainOn);
    out.directOn  = mg->getBoolAttribute ("directOn",  out.directOn);
    out.outrigOn  = mg->getBoolAttribute ("outrigOn",  out.outrigOn);
    out.ambientOn = mg->getBoolAttribute ("ambientOn", out.ambientOn);
    return true;
}

PingProcessor::MixerGateState PingProcessor::getCurrentMixerGates() const
{
    MixerGateState g;
    if (auto* v = apvts.getRawParameterValue (IDs::mainOn))    g.mainOn    = v->load() > 0.5f;
    if (auto* v = apvts.getRawParameterValue (IDs::directOn))  g.directOn  = v->load() > 0.5f;
    if (auto* v = apvts.getRawParameterValue (IDs::outrigOn))  g.outrigOn  = v->load() > 0.5f;
    if (auto* v = apvts.getRawParameterValue (IDs::ambientOn)) g.ambientOn = v->load() > 0.5f;
    return g;
}

void PingProcessor::applyMixerGates (const MixerGateState& g)
{
    auto setBool = [this] (const juce::String& id, bool v)
    {
        if (auto* p = apvts.getParameter (id))
            p->setValueNotifyingHost (v ? 1.0f : 0.0f);
    };
    // Mask each gate against the actual per-path IR-loaded state. A sidecar can
    // request e.g. `outrigOn = true` even when the user just picked a base IR
    // whose `_outrig` sibling does not exist on disk — without masking, the
    // parameter would flip on but the front-panel strip would be greyed out
    // (MicMixerComponent gates the power switch on isPathIRLoaded()), leaving
    // the mixer state silently inconsistent with what the user can interact with.
    // Force any path with no IR to OFF so the parameter and the visible UI agree.
    setBool (IDs::mainOn,    g.mainOn    && isPathIRLoaded (MicPath::Main));
    setBool (IDs::directOn,  g.directOn  && isPathIRLoaded (MicPath::Direct));
    setBool (IDs::outrigOn,  g.outrigOn  && isPathIRLoaded (MicPath::Outrig));
    setBool (IDs::ambientOn, g.ambientOn && isPathIRLoaded (MicPath::Ambient));
}

void PingProcessor::fixImportedFilePermissions (const juce::File& f)
{
    if (! f.exists()) return;
    ::chmod (f.getFullPathName().toRawUTF8(), 0644);
    juce::ChildProcess cp;
    cp.start ({ "xattr", "-d", "com.apple.quarantine", f.getFullPathName() });
    cp.waitForProcessToFinish (2000);
}

void PingProcessor::writeIRSynthSidecar (const juce::File& wavFile, const IRSynthParams& p,
                                         const MixerGateState* gates)
{
    writeIRSynthParamsSidecar (wavFile, p, gates);
}

void PingProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    if (auto xml = state.createXml())
    {
        // Remove any custom children that leaked into the APVTS state tree
        // from a previous setStateInformation → replaceState cycle.
        // Without this, each save/load cycle accumulates duplicate children,
        // inflating the state by 10+ MB per cycle (from the base64 synthIR data)
        // until the DAW truncates or fails to save the state entirely.
        while (auto* old = xml->getChildByName ("irSynthParams"))
            xml->removeChildElement (old, true);
        while (auto* old = xml->getChildByName ("synthIR"))
            xml->removeChildElement (old, true);
        while (auto* old = xml->getChildByName ("synthIRDirect"))
            xml->removeChildElement (old, true);
        while (auto* old = xml->getChildByName ("synthIROutrig"))
            xml->removeChildElement (old, true);
        while (auto* old = xml->getChildByName ("synthIRAmbient"))
            xml->removeChildElement (old, true);

        xml->removeAttribute ("irFilePath");
        if (selectedIRFile != juce::File())
            xml->setAttribute ("irFilePath", selectedIRFile.getFullPathName());
        xml->setAttribute ("reverse", reverse);
        if (lastPresetName.isNotEmpty())
            xml->setAttribute ("presetName", lastPresetName);
        if (irFromSynth && rawSynthBuffer.getNumSamples() > 0)
        {
            auto* synth = xml->createNewChildElement ("synthIR");
            if (synth)
                synthesizedIRToXml (rawSynthBuffer, rawSynthSampleRate, *synth);

            // Multi-mic aux buffers (present only when the synth panel produced them).
            // Each child uses the same wire format as <synthIR>. Older sessions that
            // saved only <synthIR> will restore without these children; on load,
            // setStateInformation simply skips the missing-child branch and the aux
            // convolvers stay silent until a new synthesis runs.
            if (rawSynthDirectBuffer.getNumSamples() > 0)
                if (auto* e = xml->createNewChildElement ("synthIRDirect"))
                    synthesizedIRToXml (rawSynthDirectBuffer, rawSynthSampleRate, *e);
            if (rawSynthOutrigBuffer.getNumSamples() > 0)
                if (auto* e = xml->createNewChildElement ("synthIROutrig"))
                    synthesizedIRToXml (rawSynthOutrigBuffer, rawSynthSampleRate, *e);
            if (rawSynthAmbientBuffer.getNumSamples() > 0)
                if (auto* e = xml->createNewChildElement ("synthIRAmbient"))
                    synthesizedIRToXml (rawSynthAmbientBuffer, rawSynthSampleRate, *e);
        }
        irSynthParamsToXml (lastIRSynthParams, *xml);
        if (currentLicence.valid)
        {
            xml->setAttribute ("licenceName", currentLicence.normalisedName);
            xml->setAttribute ("licenceSerial", savedLicenceSerial);
            if (licenceDisplayName.isNotEmpty())
                xml->setAttribute ("licenceDisplayName", licenceDisplayName);
        }
        copyXmlToBinary (*xml, destData);
    }
}

void PingProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    // Suppress PingEditor::parameterChanged -> loadSelectedIR() during state restoration.
    // apvts.replaceState() queues async parameterChanged notifications for every changed
    // parameter (including "stretch" and "decay").  Without this guard, those fire after
    // setStateInformation returns and each calls loadSelectedIR(), producing 3 × 8 = 24
    // loadImpulseResponse calls in milliseconds — NUPC background thread overload → crackling.
    // callAsync clears the flag AFTER all queued notifications have been processed (FIFO).
    isRestoringState.store (true);
    stateWasRestored.store (true);

    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        apvts.replaceState (juce::ValueTree::fromXml (*xml));

        // Backfill missing `value` properties on parameter trees.  When a preset
        // does not contain a particular parameter (e.g. an older preset saved
        // before the multi-mic mixer / Plate / Bloom / Cloud / Shimmer params
        // existed), JUCE's APVTS `updateParameterConnectionsToChildTrees`
        // creates a child tree node with only the `id` attribute and never
        // arms the adapter's `needsUpdate` flag — so the next call to
        // `flushParameterValuesToValueTree` (during the next save) skips
        // those params entirely and the saved XML again has `<PARAM id="X"/>`
        // with no value.  The bug perpetuates: every subsequent save loses
        // the values too.  Writing the default value into the tree node
        // breaks the cycle: the property exists, future flushes can update
        // it normally when the user tweaks the control, and explicit value
        // changes (Slider/ButtonAttachment -> setValueNotifyingHost) write
        // through correctly.
        {
            auto stateTree = apvts.state;
            const juce::Identifier idProp { "id" };
            const juce::Identifier valueProp { "value" };
            for (int i = 0; i < stateTree.getNumChildren(); ++i)
            {
                auto child = stateTree.getChild (i);
                if (child.hasProperty (valueProp)) continue;
                auto idStr = child.getProperty (idProp).toString();
                if (idStr.isEmpty()) continue;
                if (auto* raw = apvts.getRawParameterValue (idStr))
                    child.setProperty (valueProp, raw->load(), nullptr);
            }
        }

        // EQ frequency migration: if a band's gain is 0 dB (inactive), reset its
        // frequency to the current default.  Old presets had different defaults
        // (e.g. 400/1000/4000/200/8000); bands at 0 dB are inaudible, so this is
        // a silent fixup that gives a better starting point when the user dials in gain.
        struct BandDefault { const juce::String& freqId; const juce::String& gainId; float defaultFreq; };
        const BandDefault bandDefaults[] = {
            { IDs::band3Freq, IDs::band3Gain,   80.0f },
            { IDs::band0Freq, IDs::band0Gain,  220.0f },
            { IDs::band1Freq, IDs::band1Gain, 1600.0f },
            { IDs::band2Freq, IDs::band2Gain, 4800.0f },
            { IDs::band4Freq, IDs::band4Gain, 12000.0f },
        };
        for (const auto& bd : bandDefaults)
        {
            if (auto* gainParam = apvts.getRawParameterValue (bd.gainId))
            {
                if (std::abs (gainParam->load()) < 0.01f)
                {
                    if (auto* p = apvts.getParameter (bd.freqId))
                        p->setValueNotifyingHost (p->convertTo0to1 (bd.defaultFreq));
                }
            }
        }

        reverse = xml->getBoolAttribute ("reverse", false);
        lastPresetName = xml->getStringAttribute ("presetName", "Default");

        // Pre-clear every mic path before loading whatever the preset actually contains.
        // This is the key fix for the "leftover aux IRs from previous preset" bug:
        // a preset that doesn't ship a particular path (e.g. main-signal-only) used to
        // leave stale rawSynth*Buffer / *IRLoaded state from the previous preset, so the
        // user could enable e.g. the OUTRIG strip on the front-panel mixer and hear audio
        // from the prior preset's outrig IR. By wiping all four slots up-front, only the
        // paths the new preset *does* populate (via loadIRFromBuffer / loadIRFromFile /
        // loadMicPathFromFile below) end up with isPathIRLoaded() == true; every other
        // strip greys out on the mixer, exactly matching the preset's intent.
        // Done before restoring selectedIRFile because clearMicPath(MicPath::Main) wipes
        // selectedIRFile too.
        clearMicPath (MicPath::Main);
        clearMicPath (MicPath::Direct);
        clearMicPath (MicPath::Outrig);
        clearMicPath (MicPath::Ambient);

        // Restore selected IR file from saved path.
        selectedIRFile = juce::File();
        juce::String savedPath = xml->getStringAttribute ("irFilePath", "");
        if (savedPath.isNotEmpty())
        {
            juce::File savedFile (savedPath);
            if (savedFile.existsAsFile())
            {
                selectedIRFile = savedFile;
            }
            else
            {
                // Saved path no longer valid (e.g. factory IR folder moved between builds).
                // Try to find the file by filename stem across all known IR locations.
                juce::String stem = savedFile.getFileNameWithoutExtension();
                for (const auto& entry : irManager.getEntries())
                {
                    if (entry.file.getFileNameWithoutExtension() == stem)
                    {
                        selectedIRFile = entry.file;
                        break;
                    }
                }
            }
        }
        if (auto* ir = xml->getChildByName ("irSynthParams"))
            lastIRSynthParams = irSynthParamsFromXml (ir);

        juce::String licenceName = xml->getStringAttribute ("licenceName");
        juce::String licenceSerial = xml->getStringAttribute ("licenceSerial");
        juce::String projectDisplayName = decodeDecimalAscii (xml->getStringAttribute ("licenceDisplayName"));
        bool nameFromFileLooksValid = licenceDisplayName.isNotEmpty() && ! licenceDisplayName.containsOnly ("0123456789 ");
        if (! nameFromFileLooksValid && projectDisplayName.isNotEmpty())
            licenceDisplayName = projectDisplayName;
        if (licenceName.isNotEmpty() && licenceSerial.isNotEmpty())
        {
            LicenceVerifier v;
            currentLicence = v.activate (licenceName.toStdString(), licenceSerial.toStdString());
            if (currentLicence.valid && ! currentLicence.expired)
            {
                savedLicenceSerial = licenceSerial;
                if (licenceDisplayName.isEmpty() && ! looksLikeDate (currentLicence.normalisedName))
                {
                    auto n = juce::String::fromUTF8 (currentLicence.normalisedName.data(), (int) currentLicence.normalisedName.size());
                    licenceDisplayName = decodeDecimalAscii (n);
                }
                persistLicenceToFile (currentLicence.normalisedName, savedLicenceSerial, licenceDisplayName);
            }
        }

        // Helper: decode an <synthIRDirect> / <synthIROutrig> / <synthIRAmbient> child
        // into its MicPath-specific raw buffer.  defer = true writes rawSynth*Buffer
        // only (no convolver touch); defer = false also loads the convolvers.
        juce::XmlElement* xmlRaw = xml.get();
        auto loadAuxSynthChild = [this, xmlRaw] (const char* childName, MicPath path, bool defer)
        {
            auto* aux = xmlRaw->getChildByName (childName);
            if (aux == nullptr) return;
            juce::AudioBuffer<float> buf;
            double sr;
            if (synthesizedIRFromXml (*aux, buf, sr))
                loadIRFromBuffer (std::move (buf), sr, /*fromSynth=*/true,
                                  /*deferConvolverLoad=*/defer, path);
        };

        if (audioEnginePrepared.load())
        {
            // Live preset switch: audio engine is already running, load the IR immediately.
            // No prepareToPlay will follow, so there is nothing to race against.
            if (auto* synth = xml->getChildByName ("synthIR"))
            {
                juce::AudioBuffer<float> buf;
                double sr;
                if (synthesizedIRFromXml (*synth, buf, sr))
                {
                    loadIRFromBuffer (std::move (buf), sr, true);
                    // Aux paths (if saved) follow the main load directly.  Missing
                    // children leave their convolvers empty, which is safe: the
                    // mixer contribution flags gate them from the signal path.
                    loadAuxSynthChild ("synthIRDirect",  MicPath::Direct,  false);
                    loadAuxSynthChild ("synthIROutrig",  MicPath::Outrig,  false);
                    loadAuxSynthChild ("synthIRAmbient", MicPath::Ambient, false);
                }
                else if (selectedIRFile.existsAsFile())
                    loadIRFromFile (selectedIRFile);
            }
            else if (selectedIRFile.existsAsFile())
            {
                loadIRFromFile (selectedIRFile);
            }
        }
        else
        {
            // Initial session load: prepareToPlay has not yet been called, so the JUCE
            // Convolution objects have not been prepared.  Calling loadImpulseResponse here
            // would spawn many NUPC background threads; prepareToPlay then calls reset()
            // on every convolver while those threads are still running — a data race that
            // corrupts NUPC internal state and causes permanent distortion / escalating
            // crackling.  Instead, we save the necessary state and let prepareToPlay post
            // a callAsync that fires loadIRFromFile / reloadSynthIR after reset() completes.
            if (auto* synth = xml->getChildByName ("synthIR"))
            {
                juce::AudioBuffer<float> buf;
                double sr;
                if (synthesizedIRFromXml (*synth, buf, sr))
                {
                    // Save rawSynthBuffer (+ silence trim) without calling loadImpulseResponse.
                    loadIRFromBuffer (std::move (buf), sr, /*fromSynth=*/true, /*deferConvolverLoad=*/true);
                }
                // Aux paths (if saved) are also deferred — prepareToPlay's callAsync calls
                // reloadSynthIR() which iterates all four raw*Buffers and loads the convolvers
                // on the message thread AFTER prepareToPlay's reset() has completed.
                loadAuxSynthChild ("synthIRDirect",  MicPath::Direct,  true);
                loadAuxSynthChild ("synthIROutrig",  MicPath::Outrig,  true);
                loadAuxSynthChild ("synthIRAmbient", MicPath::Ambient, true);
                // (If MAIN XML decode fails, selectedIRFile fallback is handled by
                //  prepareToPlay's callAsync.)
            }
            // selectedIRFile is already set above — prepareToPlay's callAsync will pick it up
            // (and loadIRFromFile internally loads any sibling multi-mic files).
        }
    }

    // Clear isRestoringState AFTER all queued parameterChanged notifications have fired.
    // MessageManager::callAsync posts to the end of the message-thread FIFO; apvts.replaceState()
    // queues its async notifications before this call, so they will all be processed first.
    juce::MessageManager::callAsync ([this]() {
        isRestoringState.store (false);
        snapshotCleanState();
        irSynthDirty.store (false);
    });
}

bool PingProcessor::isLicensed() const
{
    return currentLicence.valid && ! currentLicence.expired;
}

juce::String PingProcessor::decodeLicenceDisplayName (const juce::String& raw)
{
    return decodeDecimalAscii (raw);
}

juce::String PingProcessor::getLicenceNameFromPayload() const
{
    if (! currentLicence.valid || currentLicence.normalisedName.empty() || looksLikeDate (currentLicence.normalisedName))
        return {};
    return juce::String::fromUTF8 (currentLicence.normalisedName.data(), (int) currentLicence.normalisedName.size());
}

juce::String PingProcessor::getLicenceName() const
{
    // Prefer licenceDisplayName if valid (user-typed, correctly formatted); else payload (decoded if needed)
    if (licenceDisplayName.isNotEmpty() && ! licenceDisplayName.containsOnly ("0123456789 "))
        return licenceDisplayName;
    juce::String fromPayload = juce::String::fromUTF8 (currentLicence.normalisedName.data(), (int) currentLicence.normalisedName.size());
    if (fromPayload.isNotEmpty())
    {
        juce::String decoded = decodeDecimalAscii (fromPayload);
        return decoded.isNotEmpty() ? decoded : fromPayload;
    }
    return decodeDecimalAscii (licenceDisplayName);
}

void PingProcessor::setLicence (const LicenceResult& result, const juce::String& serial, const juce::String& displayName)
{
    currentLicence = result;
    savedLicenceSerial = serial;
    if (displayName.isNotEmpty())
        licenceDisplayName = displayName;
    else if (! looksLikeDate (result.normalisedName))
    {
        auto n = juce::String::fromUTF8 (result.normalisedName.data(), (int) result.normalisedName.size());
        licenceDisplayName = decodeDecimalAscii (n);
    }
    else
        licenceDisplayName.clear();

    if (result.valid && ! result.expired)
        persistLicenceToFile (result.normalisedName, serial, licenceDisplayName);
}

void PingProcessor::loadStoredLicence()
{
    auto file = getLicenceFile();
    if (! file.existsAsFile())
        return;

    if (auto xml = juce::parseXML (file))
    {
        juce::String name = xml->getStringAttribute ("name");
        juce::String serial = xml->getStringAttribute ("serial");
        licenceDisplayName = decodeDecimalAscii (xml->getStringAttribute ("displayName"));
        if (name.isNotEmpty() && serial.isNotEmpty())
        {
            LicenceVerifier v;
            currentLicence = v.activate (name.toStdString(), serial.toStdString());
            if (currentLicence.valid && ! currentLicence.expired)
            {
                savedLicenceSerial = serial;
                if (licenceDisplayName.isEmpty() && ! looksLikeDate (currentLicence.normalisedName))
                {
                    auto n = juce::String::fromUTF8 (currentLicence.normalisedName.data(), (int) currentLicence.normalisedName.size());
                    licenceDisplayName = decodeDecimalAscii (n);
                }
            }
        }
    }
}

float PingProcessor::getReverseTrim() const
{
    return apvts.getRawParameterValue (IDs::reverseTrim)->load();
}

void PingProcessor::setReverseTrim (float v)
{
    apvts.getRawParameterValue (IDs::reverseTrim)->store (juce::jlimit (0.0f, 0.95f, v));
}

double PingProcessor::getTailLengthSeconds() const
{
    int erMax = juce::jmax (tsErConvLL.getCurrentIRSize(), tsErConvRL.getCurrentIRSize(),
                            tsErConvLR.getCurrentIRSize(), tsErConvRR.getCurrentIRSize());
    int tailMax = juce::jmax (tsTailConvLL.getCurrentIRSize(), tsTailConvRL.getCurrentIRSize(),
                              tsTailConvLR.getCurrentIRSize(), tsTailConvRR.getCurrentIRSize());
    int irSize = juce::jmax (erMax, tailMax);
    if (currentSampleRate > 0 && irSize > 0)
        return irSize / currentSampleRate;
    return 0.0;
}

juce::AudioProcessorEditor* PingProcessor::createEditor()
{
    return new PingEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PingProcessor();
}
