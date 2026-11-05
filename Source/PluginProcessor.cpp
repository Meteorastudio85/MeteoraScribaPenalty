#include "PluginProcessor.h"
#include "PluginEditor.h"

ScribaPenaltyAudioProcessor::ScribaPenaltyAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
    platformParam  = apvts.getRawParameterValue ("platform");
    penaltyParam   = apvts.getRawParameterValue ("penalty");
    sourceParam    = apvts.getRawParameterValue ("source");
    holdParam      = apvts.getRawParameterValue ("hold");
    autoResetParam = apvts.getRawParameterValue ("autoreset");
}

juce::AudioProcessorValueTreeState::ParameterLayout ScribaPenaltyAudioProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "platform", 1 }, "Piattaforma",
                                                        scriba::platformNames(), 0));
    layout.add (std::make_unique<AudioParameterBool>   (ParameterID { "penalty", 1 }, "Penalty", true));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "source", 1 }, "Misura",
                                                        StringArray { "Integrato (brano intero)", "Short-term (live 3 s)" }, 0));
    layout.add (std::make_unique<AudioParameterBool>   (ParameterID { "hold", 1 }, "Hold", false));
    layout.add (std::make_unique<AudioParameterBool>   (ParameterID { "autoreset", 1 }, "Auto reset", true));

    return layout;
}

void ScribaPenaltyAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    meter.prepare (sampleRate, juce::jmax (1, getTotalNumInputChannels()), juce::jmax (1, samplesPerBlock));

    gain.reset (sampleRate, 0.3);
    gain.setCurrentAndTargetValue (1.0f);

    // Limiter stile Spotify "Alto": attacco 5 ms, rilascio 100 ms
    limiterAttack  = 1.0f - std::exp (-1.0f / (0.005f * (float) sampleRate));
    limiterRelease = 1.0f - std::exp (-1.0f / (0.100f * (float) sampleRate));
    limiterGain    = 1.0f;
}

bool ScribaPenaltyAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();

    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;

    return out == layouts.getMainInputChannelSet();
}

void ScribaPenaltyAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();

    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, numSamples);

    const int numCh = buffer.getNumChannels();

    // --- Trasporto -----------------------------------------------------------
    bool transportKnown = false;
    bool playing        = true;

    if (auto* ph = getPlayHead())
    {
        if (auto pos = ph->getPosition())
        {
            transportKnown = true;
            playing        = pos->getIsPlaying();
        }
    }

    const bool hold      = holdParam->load() > 0.5f;
    const bool autoReset = autoResetParam->load() > 0.5f;

    if (resetRequested.exchange (false))
        meter.reset();

    if (autoReset && transportKnown && playing && ! wasPlaying && ! hold)
        meter.reset();

    wasPlaying = playing;

    // --- Misura (sul segnale PRIMA della penalty) ------------------------------
    const bool doMeasure = ! hold && (! transportKnown || playing);
    measuring.store (doMeasure);

    if (doMeasure)
        meter.process (buffer);

    // --- Calcolo della penalty ------------------------------------------------
    const auto& platform = scriba::platforms[(size_t) getPlatformIndex()];
    const float measured = sourceParam->load() > 0.5f ? meter.getShortTerm() : meter.getIntegrated();

    float gDb = 0.0f;

    if (measured > -70.0f)
    {
        gDb = platform.targetLufs - measured;

        if (gDb > 0.0f)
        {
            if (! platform.turnsUp)
            {
                gDb = 0.0f;
            }
            else if (! platform.limiterOnUp)
            {
                // Alza solo finche' il true peak resta sotto -1 dBTP
                const float headroom = scriba::peakCeilingDb - meter.getTruePeakDb();
                gDb = juce::jmin (gDb, juce::jmax (0.0f, headroom));
            }
        }
    }

    penaltyDb.store (gDb);

    const bool active     = penaltyParam->load() > 0.5f;
    const bool useLimiter = active && platform.limiterOnUp && gDb > 0.0f;
    const float ceiling   = juce::Decibels::decibelsToGain (scriba::peakCeilingDb);

    gain.setTargetValue (active ? juce::Decibels::decibelsToGain (gDb) : 1.0f);

    // --- Applicazione ---------------------------------------------------------
    float* const* data = buffer.getArrayOfWritePointers();
    float minLimiter = 1.0f;

    for (int i = 0; i < numSamples; ++i)
    {
        const float g = gain.getNextValue();
        float peak = 0.0f;

        for (int ch = 0; ch < numCh; ++ch)
        {
            data[ch][i] *= g;
            peak = juce::jmax (peak, std::abs (data[ch][i]));
        }

        if (useLimiter)
        {
            const float target = peak > ceiling ? ceiling / peak : 1.0f;
            const float coeff  = target < limiterGain ? limiterAttack : limiterRelease;
            limiterGain += (target - limiterGain) * coeff;

            for (int ch = 0; ch < numCh; ++ch)
                data[ch][i] *= limiterGain;

            minLimiter = juce::jmin (minLimiter, limiterGain);
        }
        else
        {
            limiterGain = 1.0f;
        }
    }

    limiterGrDb.store (juce::Decibels::gainToDecibels (minLimiter, -60.0f));
}

juce::AudioProcessorEditor* ScribaPenaltyAudioProcessor::createEditor()
{
    return new ScribaPenaltyAudioProcessorEditor (*this);
}

void ScribaPenaltyAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    if (xml != nullptr)
        copyXmlToBinary (*xml, destData);
}

void ScribaPenaltyAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));

    if (xml != nullptr && xml->hasTagName (apvts.state.getType()))
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ScribaPenaltyAudioProcessor();
}
