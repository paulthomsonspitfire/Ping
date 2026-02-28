#include "PluginProcessor.h"
#include "PluginEditor.h"

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
    static const juce::String irInputDrive { "irInputDrive" };
    static const juce::String erLevel { "erLevel" };
    static const juce::String tailLevel { "tailLevel" };
    static const juce::String reverseTrim { "reversetrim" };
    static const juce::String band0Freq { "b0freq" }, band0Gain { "b0gain" }, band0Q { "b0q" };
    static const juce::String band1Freq { "b1freq" }, band1Gain { "b1gain" }, band1Q { "b1q" };
    static const juce::String band2Freq { "b2freq" }, band2Gain { "b2gain" }, band2Q { "b2q" };
}

static juce::File getLicenceDirectory()
{
    auto systemDir = juce::File::getSpecialLocation (juce::File::commonApplicationDataDirectory)
                         .getChildFile ("Audio")
                         .getChildFile ("Ping")
                         .getChildFile ("P!NG");
    return systemDir;
}

static juce::File getLicenceFile()
{
    auto userFile = juce::File::getSpecialLocation (juce::File::userHomeDirectory)
                        .getChildFile ("Library")
                        .getChildFile ("Audio")
                        .getChildFile ("Ping")
                        .getChildFile ("P!NG")
                        .getChildFile ("licence.xml");
    if (userFile.existsAsFile())
        return userFile;
    return getLicenceDirectory().getChildFile ("licence.xml");
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

/** Persist current licence to disk. Uses /Library/Audio/Ping/P!NG/ (main Library). */
static void persistLicenceToFile (const std::string& normalisedName, const juce::String& serial,
                                  const juce::String& displayName)
{
    auto dir = getLicenceDirectory();
    if (! dir.createDirectory().wasOk())
    {
        auto userDir = juce::File::getSpecialLocation (juce::File::userHomeDirectory)
                           .getChildFile ("Library")
                           .getChildFile ("Audio")
                           .getChildFile ("Ping")
                           .getChildFile ("P!NG");
        dir = userDir;
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
    irManager.refresh();
    loadStoredLicence();
}

PingProcessor::~PingProcessor() {}

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
    layout.add (std::make_unique<juce::AudioParameterFloat> (IDs::irInputDrive, "IR Input Drive", 0.0f, 1.0f, 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (IDs::erLevel, "Early Reflections",
        juce::NormalisableRange<float> (-48.0f, 6.0f, 0.1f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (IDs::tailLevel, "Tail",
        juce::NormalisableRange<float> (-48.0f, 6.0f, 0.1f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (IDs::reverseTrim, "Reverse Trim", 0.0f, 0.95f, 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (IDs::band0Freq, "Band 0 Freq (Hz)", 20.0f, 20000.0f, 400.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (IDs::band0Gain, "Band 0 Gain (dB)", -12.0f, 12.0f, 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (IDs::band0Q, "Band 0 Q", 0.3f, 10.0f, 0.707f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (IDs::band1Freq, "Band 1 Freq (Hz)", 20.0f, 20000.0f, 1000.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (IDs::band1Gain, "Band 1 Gain (dB)", -12.0f, 12.0f, 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (IDs::band1Q, "Band 1 Q", 0.3f, 10.0f, 0.707f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (IDs::band2Freq, "Band 2 Freq (Hz)", 20.0f, 20000.0f, 4000.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (IDs::band2Gain, "Band 2 Gain (dB)", -12.0f, 12.0f, 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (IDs::band2Q, "Band 2 Q", 0.3f, 10.0f, 0.707f));
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
    spec.numChannels = 2;
    erConvolver.reset();
    tailConvolver.reset();
    erConvolver.prepare (spec);
    tailConvolver.prepare (spec);

    erLevelSmoothed.reset (sampleRate, 0.02);
    tailLevelSmoothed.reset (sampleRate, 0.02);

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
    saturatorDriveSmoothed.reset (sampleRate, 0.02);

    updateEQ();
    lowBand.prepare (spec);
    midBand.prepare (spec);
    highBand.prepare (spec);

    updateGains();
    updatePredelay();
    updateEQ();
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

    // Dry copy
    juce::AudioBuffer<float> dryBuffer (numChannels, numSamples);
    dryBuffer.copyFrom (0, 0, buffer, 0, 0, numSamples);
    if (numChannels > 1)
        dryBuffer.copyFrom (1, 0, buffer, 1, 0, numSamples);

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

    float erDb = apvts.getRawParameterValue (IDs::erLevel)->load();
    float tailDb = apvts.getRawParameterValue (IDs::tailLevel)->load();
    erLevelSmoothed.setTargetValue (juce::Decibels::decibelsToGain (erDb));
    tailLevelSmoothed.setTargetValue (juce::Decibels::decibelsToGain (tailDb));

    if (useTrueStereo)
    {
        juce::AudioBuffer<float> lIn (1, numSamples), rIn (1, numSamples);
        lIn.copyFrom (0, 0, buffer, 0, 0, numSamples);
        rIn.copyFrom (0, 0, buffer, 1, 0, numSamples);

        juce::AudioBuffer<float> tmp (1, numSamples);
        juce::AudioBuffer<float> lEr (1, numSamples), rEr (1, numSamples);

        juce::dsp::AudioBlock<float> tmpBlock (tmp);
        tmp.copyFrom (0, 0, lIn, 0, 0, numSamples);
        tsErConvLL.process (juce::dsp::ProcessContextReplacing<float> (tmpBlock));
        lEr.copyFrom (0, 0, tmp, 0, 0, numSamples);
        tmp.copyFrom (0, 0, rIn, 0, 0, numSamples);
        tsErConvRL.process (juce::dsp::ProcessContextReplacing<float> (tmpBlock));
        lEr.addFrom (0, 0, tmp, 0, 0, numSamples);

        tmp.copyFrom (0, 0, lIn, 0, 0, numSamples);
        tsErConvLR.process (juce::dsp::ProcessContextReplacing<float> (tmpBlock));
        rEr.copyFrom (0, 0, tmp, 0, 0, numSamples);
        tmp.copyFrom (0, 0, rIn, 0, 0, numSamples);
        tsErConvRR.process (juce::dsp::ProcessContextReplacing<float> (tmpBlock));
        rEr.addFrom (0, 0, tmp, 0, 0, numSamples);

        // Tail: use the regular stereo tailConvolver (IRSynthEngine calibrates tail
        // to ER level, so Normalise::no preserves the correct balance).
        juce::AudioBuffer<float> tailBuf (2, numSamples);
        tailBuf.copyFrom (0, 0, lIn, 0, 0, numSamples);
        tailBuf.copyFrom (1, 0, rIn, 0, 0, numSamples);
        {
            juce::dsp::AudioBlock<float> tailBlock (tailBuf);
            tailConvolver.process (juce::dsp::ProcessContextReplacing<float> (tailBlock));
        }

        for (int i = 0; i < numSamples; ++i)
        {
            float erG = erLevelSmoothed.getNextValue();
            float tailG = tailLevelSmoothed.getNextValue();
            buffer.setSample (0, i, lEr.getSample (0, i) * erG + tailBuf.getSample (0, i) * tailG);
            buffer.setSample (1, i, rEr.getSample (0, i) * erG + tailBuf.getSample (1, i) * tailG);
        }
    }
    else
    {
        juce::AudioBuffer<float> tailBuffer (numChannels, numSamples);
        for (int ch = 0; ch < numChannels; ++ch)
            tailBuffer.copyFrom (ch, 0, buffer, ch, 0, numSamples);

        juce::dsp::AudioBlock<float> erBlock (buffer);
        juce::dsp::AudioBlock<float> tailBlock (tailBuffer);
        erConvolver.process (juce::dsp::ProcessContextReplacing<float> (erBlock));
        tailConvolver.process (juce::dsp::ProcessContextReplacing<float> (tailBlock));

        for (int i = 0; i < numSamples; ++i)
        {
            float erG = erLevelSmoothed.getNextValue();
            float tailG = tailLevelSmoothed.getNextValue();
            for (int ch = 0; ch < numChannels; ++ch)
                buffer.setSample (ch, i, buffer.getSample (ch, i) * erG + tailBuffer.getSample (ch, i) * tailG);
        }
    }

    // EQ
    lowBand.process (context);
    midBand.process (context);
    highBand.process (context);

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

    // Chorus/tail modulation on wet signal (variable delay, LFO-modulated)
    float tailAmt = apvts.getRawParameterValue (IDs::tailMod)->load();
    float depthMs = apvts.getRawParameterValue (IDs::delayDepth)->load();
    float rateHz = apvts.getRawParameterValue (IDs::tailRate)->load();
    if (tailAmt > 0.0001f && depthMs > 0.1f && rateHz > 0.001f)
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

    // Mix dry/wet: output = dry * dryBuffer + wet * wetBuffer (buffer is currently wet)
    float dry = dryGain.getGainLinear();
    float wet = wetGain.getGainLinear();
    for (int ch = 0; ch < numChannels; ++ch)
    {
        buffer.applyGain (ch, 0, numSamples, wet);
        buffer.addFrom (ch, 0, dryBuffer, ch, 0, numSamples, dry);
    }

    // Output level for meter (peak follower with decay), L/R separately
    auto updatePeak = [] (std::atomic<float>& peakStore, float newPeak)
    {
        float current = peakStore.load();
        if (newPeak > current)
            peakStore.store (newPeak);
        else
            peakStore.store (current * 0.95f);
    };
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
    *lowBand.state  = *juce::dsp::IIR::Coefficients<float>::makePeakFilter (currentSampleRate, freq (IDs::band0Freq), q (IDs::band0Q), gain (IDs::band0Gain));
    *midBand.state  = *juce::dsp::IIR::Coefficients<float>::makePeakFilter (currentSampleRate, freq (IDs::band1Freq), q (IDs::band1Q), gain (IDs::band1Gain));
    *highBand.state = *juce::dsp::IIR::Coefficients<float>::makePeakFilter (currentSampleRate, freq (IDs::band2Freq), q (IDs::band2Q), gain (IDs::band2Gain));
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
    lastLoadedIRFile = file;
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (file));
    if (! reader) return;

    juce::AudioBuffer<float> buf ((int) reader->numChannels, (int) reader->lengthInSamples);
    reader->read (&buf, 0, (int) reader->lengthInSamples, 0, true, true);
    double rate = reader->sampleRate;
    loadIRFromBuffer (std::move (buf), rate);
}

void PingProcessor::loadIRFromBuffer (juce::AudioBuffer<float> buffer, double bufferSampleRate)
{
    if (buffer.getNumSamples() == 0) return;

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

    currentIRBuffer = buffer;
    int numCh = buffer.getNumChannels();
    int fullLen = buffer.getNumSamples();

    const int crossoverSamples = static_cast<int> (0.080 * bufferSampleRate);
    const int fadeLength = static_cast<int> (0.010 * bufferSampleRate);

    if (numCh >= 4)
    {
        useTrueStereo = true;
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
            if (fullLen <= crossoverSamples)
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
        juce::dsp::Convolution* tsEr[]  = { &tsErConvLL, &tsErConvRL, &tsErConvLR, &tsErConvRR };
        juce::dsp::Convolution* tsTail[] = { &tsTailConvLL, &tsTailConvRL, &tsTailConvLR, &tsTailConvRR };
        // Group-normalise the 4 ER channels with a single shared scale factor.
        // This preserves every inter-channel amplitude ratio (= all spatial information).
        // We must bound both PEAK (for transients) and L1 sum (for sustained signals):
        // convolution gain for steady input is proportional to sum(|IR|), not peak.
        // Image-source ER IRs have many reflections that sum to 5–20x, causing ~20 dB
        // overload. Scale by 1/max(grpPk, grpL1) so both are bounded to 1.0.
        {
            juce::AudioBuffer<float> erBuf[4];
            for (int c = 0; c < 4; ++c) erBuf[c] = makeMonoEr (c);
            float grpPk = 0.0f;
            float grpL1L = 0.0f, grpL1R = 0.0f;
            const int erN = erBuf[0].getNumSamples();
            for (int i = 0; i < erN; ++i)
            {
                float s0 = erBuf[0].getSample (0, i), s1 = erBuf[1].getSample (0, i);
                float s2 = erBuf[2].getSample (0, i), s3 = erBuf[3].getSample (0, i);
                grpPk = std::max (grpPk, std::abs (s0 + s1));  // combined L
                grpPk = std::max (grpPk, std::abs (s2 + s3));  // combined R
                grpPk = std::max (grpPk, std::abs (s0));       // individual LL
                grpPk = std::max (grpPk, std::abs (s1));       // individual RL
                grpPk = std::max (grpPk, std::abs (s2));       // individual LR
                grpPk = std::max (grpPk, std::abs (s3));       // individual RR
                grpL1L += std::abs (s0 + s1);  // L1 of effective L-out IR
                grpL1R += std::abs (s2 + s3);  // L1 of effective R-out IR
            }
            float grpL1 = std::max (grpL1L, grpL1R);
            float scaleLimit = std::max (grpPk, grpL1);
            if (scaleLimit > 1.0e-9f)
            {
                const float sc = 1.0f / scaleLimit;
                for (int c = 0; c < 4; ++c) erBuf[c].applyGain (sc);
            }
            for (int c = 0; c < 4; ++c)
                tsEr[c]->loadImpulseResponse (std::move (erBuf[c]), bufferSampleRate,
                    juce::dsp::Convolution::Stereo::no, juce::dsp::Convolution::Trim::no,
                    juce::dsp::Convolution::Normalise::no);
        }
        for (int c = 0; c < 4; ++c)
            tsTail[c]->loadImpulseResponse (makeMonoTail (c), bufferSampleRate,
                juce::dsp::Convolution::Stereo::no, juce::dsp::Convolution::Trim::no,
                juce::dsp::Convolution::Normalise::no);

        // Build a stereo combined-tail IR for the regular tailConvolver.
        // iLL_tail == iRL_tail and iLR_tail == iRR_tail (the tail has no true-stereo
        // spatial content), so combining them and normalising with ::yes restores the
        // correct tail level without affecting spatial information.
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
                juce::dsp::Convolution::Normalise::yes);
        }
    }
    else
    {
        useTrueStereo = false;
        bool isStereo = numCh >= 2;
        juce::AudioBuffer<float> erIR;
        juce::AudioBuffer<float> tailIR;

        if (fullLen <= crossoverSamples)
        {
            erIR = std::move (buffer);
            tailIR = juce::AudioBuffer<float> (numCh, juce::jmax (64, fadeLength * 2));
            tailIR.clear();
        }
        else
        {
            int erLen = crossoverSamples + fadeLength;
            erIR = juce::AudioBuffer<float> (numCh, erLen);
            for (int ch = 0; ch < numCh; ++ch)
                erIR.copyFrom (ch, 0, buffer, ch, 0, juce::jmin (erLen, fullLen));
            for (int i = 0; i < fadeLength && (crossoverSamples + i) < erIR.getNumSamples(); ++i)
            {
                float gain = 1.0f - (float) i / (float) fadeLength;
                erIR.applyGain (crossoverSamples + i, 1, gain);
            }

            int tailLen = fullLen - crossoverSamples;
            tailIR = juce::AudioBuffer<float> (numCh, tailLen);
            for (int ch = 0; ch < numCh; ++ch)
                tailIR.copyFrom (ch, 0, buffer, ch, crossoverSamples, tailLen);
            for (int i = 0; i < juce::jmin (fadeLength, tailLen); ++i)
            {
                float gain = (float) i / (float) fadeLength;
                tailIR.applyGain (i, 1, gain);
            }
        }

        erConvolver.loadImpulseResponse (std::move (erIR), bufferSampleRate,
                                        isStereo ? juce::dsp::Convolution::Stereo::yes : juce::dsp::Convolution::Stereo::no,
                                        juce::dsp::Convolution::Trim::no,
                                        juce::dsp::Convolution::Normalise::yes);
        tailConvolver.loadImpulseResponse (std::move (tailIR), bufferSampleRate,
                                           isStereo ? juce::dsp::Convolution::Stereo::yes : juce::dsp::Convolution::Stereo::no,
                                           juce::dsp::Convolution::Trim::no,
                                           juce::dsp::Convolution::Normalise::yes);
    }
}

void PingProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    if (auto xml = state.createXml())
    {
        xml->setAttribute ("irIndex", selectedIRIndex);
        xml->setAttribute ("reverse", reverse);
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
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
        selectedIRIndex = xml->getIntAttribute ("irIndex", -1);
        reverse = xml->getBoolAttribute ("reverse", false);

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

        if (selectedIRIndex >= 0)
        {
            auto file = irManager.getIRFileAt (selectedIRIndex);
            if (file.existsAsFile())
                loadIRFromFile (file);
        }
    }
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
    int irSize = 0;
    if (useTrueStereo)
    {
        int erMax = juce::jmax (tsErConvLL.getCurrentIRSize(), tsErConvRL.getCurrentIRSize(),
                               tsErConvLR.getCurrentIRSize(), tsErConvRR.getCurrentIRSize());
        int tailMax = juce::jmax (tsTailConvLL.getCurrentIRSize(), tsTailConvRL.getCurrentIRSize(),
                                 tsTailConvLR.getCurrentIRSize(), tsTailConvRR.getCurrentIRSize());
        irSize = juce::jmax (erMax, tailMax);
    }
    else
    {
        irSize = juce::jmax (erConvolver.getCurrentIRSize(), tailConvolver.getCurrentIRSize());
    }
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
