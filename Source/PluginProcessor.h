#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "DSP/MasterEngine.h"

class HarmoHitAudioProcessor final : public juce::AudioProcessor
{
public:
    HarmoHitAudioProcessor();
    void prepareToPlay(double, int) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override;
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;
    void processBlockBypassed(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    juce::AudioProcessorValueTreeState parameters;
    float getInputVuDbfs() const noexcept { return inputVuDbfs.load(std::memory_order_relaxed); }
    float getOutputVuDbfs() const noexcept { return outputVuDbfs.load(std::memory_order_relaxed); }
    std::atomic<float>& getInputVuSource() noexcept { return inputVuDbfs; }
    std::atomic<float>& getOutputVuSource() noexcept { return outputVuDbfs; }
    std::atomic<uint64_t>& getVuUpdateCounter() noexcept { return vuUpdateCounter; }
    const vitaplug::harmohit::masterdsp::MasterEngine& getEngine() const noexcept { return core; }   // tests only
private:
    vitaplug::harmohit::masterdsp::MasterSettings currentSettings() const noexcept;
    vitaplug::harmohit::masterdsp::MasterEngine core;
    struct BankPointers {
        std::atomic<float> *harmonics{}, *exciter{}, *drive{}, *range{}, *push{};
    };
    std::array<BankPointers, 3> banks{};
    std::atomic<float> *outputTrim{}, *bypass{}, *processingMode{};
    std::atomic<float> inputVuDbfs { -72.0f };
    std::atomic<float> outputVuDbfs { -72.0f };
    std::atomic<uint64_t> vuUpdateCounter { 0 };
    float inputVuMeanSquare = 0.0f;
    float outputVuMeanSquare = 0.0f;
    double currentSampleRate = 44100.0;
    void updateVuMeter(const juce::AudioBuffer<float>&, bool isInput) noexcept;
};
