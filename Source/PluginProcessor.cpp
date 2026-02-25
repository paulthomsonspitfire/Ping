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
    static const juce::String reverseTrim { "reversetrim" };
    static const juce::String band0Freq { "b0freq" }, band0Gain { "b0gain" }, band0Q { "b0q" };
    static const juce::String band1Freq { "b1freq" }, band1Gain { "b1gain" }, band1Q { "b1q" };
    static const juce::String band2Freq { "b2freq" }, band2Gain { "b2gain" }, band2Q { "b2q" };
}

static juce::File getLicenceFile()
{
    auto dir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("Ping")
                   .getChildFile ("P!NG");
    if (! dir.exists())
        dir.createDirectory();
    return dir.getChildFile ("licence.xml");
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

    convolution.reset();
    convolution.prepare (spec);

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

    // Convolution (stereo)
    convolution.process (context);

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
    bool isStereo = buffer.getNumChannels() >= 2;
    convolution.loadImpulseResponse (std::move (buffer), bufferSampleRate,
                                    isStereo ? juce::dsp::Convolution::Stereo::yes : juce::dsp::Convolution::Stereo::no,
                                    juce::dsp::Convolution::Trim::no,
                                    juce::dsp::Convolution::Normalise::yes);
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
        if (licenceName.isNotEmpty() && licenceSerial.isNotEmpty())
        {
            LicenceVerifier v;
            currentLicence = v.activate (licenceName.toStdString(), licenceSerial.toStdString());
            if (currentLicence.valid && ! currentLicence.expired)
                savedLicenceSerial = licenceSerial;
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

void PingProcessor::setLicence (const LicenceResult& result, const juce::String& serial)
{
    currentLicence = result;
    savedLicenceSerial = serial;

    if (result.valid && ! result.expired)
    {
        auto file = getLicenceFile();
        if (auto xml = std::make_unique<juce::XmlElement> ("Licence"))
        {
            xml->setAttribute ("name", result.normalisedName);
            xml->setAttribute ("serial", serial);
            xml->writeTo (file);
        }
    }
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
        if (name.isNotEmpty() && serial.isNotEmpty())
        {
            LicenceVerifier v;
            currentLicence = v.activate (name.toStdString(), serial.toStdString());
            if (currentLicence.valid && ! currentLicence.expired)
                savedLicenceSerial = serial;
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
    int irSize = convolution.getCurrentIRSize();
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
