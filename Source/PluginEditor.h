#pragma once
#include "PluginProcessor.h"
class HarmoHitAudioProcessorEditor final : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit HarmoHitAudioProcessorEditor(HarmoHitAudioProcessor&);
    ~HarmoHitAudioProcessorEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    void synchroniseControls();
private:
    class Knob;
    class ModeButton;
    class ArtButton;
    void timerCallback() override;
    void bindBank(int);
    void setParameter(const juce::String&,float);
    void paintMeter(juce::Graphics&,int,float);
    HarmoHitAudioProcessor& audioProcessor;
    juce::Image originalPanel, panel, cleanPanel, rangeLever, pushTexture;
    std::array<std::unique_ptr<Knob>,4> knobs;
    std::array<std::unique_ptr<ModeButton>,4> modeButtons;
    std::unique_ptr<ArtButton> rangeButton, pushButton;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>,4> attachments;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> rangeAttachment, pushAttachment;
    int displayedBank = -1;
    float displayedInput = -20, displayedOutput = -20;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HarmoHitAudioProcessorEditor)
};
