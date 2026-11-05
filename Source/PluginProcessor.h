#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "LoudnessMeter.h"
#include "Platforms.h"

class ScribaPenaltyAudioProcessor : public juce::AudioProcessor
{
public:
    ScribaPenaltyAudioProcessor();
    ~ScribaPenaltyAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Scriba Penalty"; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override    { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    juce::AudioProcessorValueTreeState apvts;

    // Per l'interfaccia
    const scriba::LoudnessMeter& getMeter() const noexcept { return meter; }
    void  requestReset() noexcept          { resetRequested.store (true); }
    float getPenaltyDb() const noexcept    { return penaltyDb.load(); }
    float getLimiterGrDb() const noexcept  { return limiterGrDb.load(); }
    bool  isMeasuring() const noexcept     { return measuring.load(); }
    int   getPlatformIndex() const noexcept
    {
        return juce::jlimit (0, scriba::numPlatforms - 1, (int) platformParam->load());
    }

private:
    scriba::LoudnessMeter meter;

    std::atomic<bool>  resetRequested { false };
    std::atomic<bool>  measuring      { false };
    std::atomic<float> penaltyDb      { 0.0f };
    std::atomic<float> limiterGrDb    { 0.0f };

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> gain;

    float limiterGain    = 1.0f;
    float limiterAttack  = 0.0f;
    float limiterRelease = 0.0f;
    bool  wasPlaying     = false;

    std::atomic<float>* platformParam  = nullptr;
    std::atomic<float>* penaltyParam   = nullptr;
    std::atomic<float>* sourceParam    = nullptr;
    std::atomic<float>* holdParam      = nullptr;
    std::atomic<float>* autoResetParam = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ScribaPenaltyAudioProcessor)
};
