#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include "PluginProcessor.h"

class ScribaPenaltyAudioProcessorEditor : public juce::AudioProcessorEditor,
                                          private juce::Timer
{
public:
    explicit ScribaPenaltyAudioProcessorEditor (ScribaPenaltyAudioProcessor&);
    ~ScribaPenaltyAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    void drawPenaltyPanel (juce::Graphics&, const scriba::Platform&, juce::Colour accent);
    void drawReadouts (juce::Graphics&, const scriba::Platform&);
    void drawGraph (juce::Graphics&, const scriba::Platform&, juce::Colour accent);

    ScribaPenaltyAudioProcessor& proc;

    juce::ComboBox   platformBox, sourceBox;
    juce::TextButton penaltyButton, holdButton { "HOLD" }, resetButton { "RESET" }, autoResetButton { "AUTO-RESET" };

    using ComboAttachment  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    std::unique_ptr<ComboAttachment>  platformAtt, sourceAtt;
    std::unique_ptr<ButtonAttachment> penaltyAtt, holdAtt, autoResetAtt;

    static constexpr int historySize = 300; // 30 s a 10 Hz
    std::array<float, historySize> history {};
    int historyPos = 0;
    int tick       = 0;

    float integrated = -100.0f, shortTerm = -100.0f, momentary = -100.0f;
    float truePeak   = -100.0f, penalty = 0.0f, limiterGr = 0.0f, seconds = 0.0f;

    juce::Rectangle<int> penaltyArea, readoutArea, graphArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ScribaPenaltyAudioProcessorEditor)
};
