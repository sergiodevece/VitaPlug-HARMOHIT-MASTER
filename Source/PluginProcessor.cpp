#include "PluginProcessor.h"
#include "PluginEditor.h"
HarmoHitAudioProcessor::HarmoHitAudioProcessor()
    : AudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true)
                                     .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, "MASTER_R1", createParameterLayout())
{
    const char* prefixes[]{"stereo", "mid", "side"};
    for (int i = 0; i < 3; ++i) {
        const juce::String prefix = juce::String(prefixes[i]) + ".";
        banks[static_cast<size_t>(i)] = {
            parameters.getRawParameterValue(prefix+"harmonics"),
            parameters.getRawParameterValue(prefix+"exciter"),
            parameters.getRawParameterValue(prefix+"drive"),
            parameters.getRawParameterValue(prefix+"range"),
            parameters.getRawParameterValue(prefix+"pushAll")};
    }
    outputTrim = parameters.getRawParameterValue("outputTrim");
    bypass = parameters.getRawParameterValue("bypass");
    processingMode = parameters.getRawParameterValue("processingMode");
}
const juce::String HarmoHitAudioProcessor::getName() const { return "VitaPlug HARMOHIT MASTER"; }
juce::AudioProcessorEditor* HarmoHitAudioProcessor::createEditor() { return new HarmoHitAudioProcessorEditor(*this); }
juce::AudioProcessorValueTreeState::ParameterLayout HarmoHitAudioProcessor::createParameterLayout() {
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    using Range = juce::NormalisableRange<float>;
    for (const auto* prefix : {"stereo", "mid", "side"}) {
        const auto name = juce::String(prefix);
        for (const auto* control : {"harmonics", "exciter", "drive"})
            layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{name+"."+control,1},
                name+" "+control, Range{0,100,0.1f},0,
                juce::AudioParameterFloatAttributes{}.withLabel("%")));
        layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{name+".range",1},
            name+" Range",juce::StringArray{"3 kHz","8 kHz"},0));
        layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{name+".pushAll",1},name+" Push All",false));
    }
    layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{"processingMode",1},
        "Processing Mode",juce::StringArray{"STEREO","M/S"},0));
    // Edit selection is serialized independently and never read by the DSP.
    layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{"editBank",1},
        "M/S Edit Bank",juce::StringArray{"M","S"},0));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"outputTrim",1},
        "Output Trim",Range{-24,12,0.1f},0,juce::AudioParameterFloatAttributes{}.withLabel("dB")));
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"bypass",1},"Bypass",false));
    return layout;
}
void HarmoHitAudioProcessor::prepareToPlay(double rate, int block) {
    if (!std::isfinite(rate) || rate <= 0.0) rate = 48000.0;
    currentSampleRate = rate;
    inputVuMeanSquare = outputVuMeanSquare = 0;
    inputVuDbfs.store(-72); outputVuDbfs.store(-72);
    core.prepare(rate, block, currentSettings()); setLatencySamples(core.getLatencySamples());
}
void HarmoHitAudioProcessor::releaseResources() { core.reset(); }
bool HarmoHitAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    const auto input = layouts.getMainInputChannelSet(), output = layouts.getMainOutputChannelSet();
    return input == output && (input == juce::AudioChannelSet::mono() || input == juce::AudioChannelSet::stereo());
}
void HarmoHitAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) {
    juce::ScopedNoDenormals noDenormals;
    updateVuMeter(buffer,true);
    const auto settings = currentSettings();
    std::array<float*,2> channels{};
    const int count = std::min(2,buffer.getNumChannels());
    for (int i=0;i<count;++i) channels[static_cast<size_t>(i)]=buffer.getWritePointer(i);
    core.process(channels.data(),count,buffer.getNumSamples(),settings);
    updateVuMeter(buffer,false); vuUpdateCounter.fetch_add(1,std::memory_order_relaxed);
}
vitaplug::harmohit::masterdsp::MasterSettings HarmoHitAudioProcessor::currentSettings() const noexcept {
    vitaplug::harmohit::masterdsp::MasterSettings settings;
    for (size_t i = 0; i < banks.size(); ++i) {
        const auto& b = banks[i];
        settings.banks[i] = {b.harmonics->load()/100.0f,b.exciter->load()/100.0f,b.drive->load()/100.0f,
            0.0f, static_cast<vitaplug::harmohit::masterdsp::ExciterMode>(
                b.push->load() >= 0.5f ? 2 : (b.range->load() >= 0.5f ? 1 : 0))};
    }
    settings.midSide = processingMode->load() >= 0.5f;
    settings.trimDb = outputTrim->load(); settings.bypass = bypass->load() >= 0.5f;
    return settings;
}
void HarmoHitAudioProcessor::processBlockBypassed(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) {
    juce::ScopedNoDenormals noDenormals;
    updateVuMeter(buffer,true);
    std::array<float*,2> channels{};
    const int count = std::min(2,buffer.getNumChannels());
    for (int i=0;i<count;++i) channels[static_cast<size_t>(i)] = buffer.getWritePointer(i);
    core.process(channels.data(),count,buffer.getNumSamples(),{});
    updateVuMeter(buffer,false); vuUpdateCounter.fetch_add(1,std::memory_order_relaxed);
}
void HarmoHitAudioProcessor::updateVuMeter(const juce::AudioBuffer<float>& buffer, bool isInput) noexcept
{
    const int channels = std::min(2, buffer.getNumChannels());
    const int samples = buffer.getNumSamples();
    if (channels <= 0 || samples <= 0) return;

    // Power is averaged across channels, never summed: a stereo programme
    // retains its RMS calibration even when the channels are anti-phase.
    double energy = 0.0;
    for (int channel = 0; channel < channels; ++channel)
    {
        const auto* data = buffer.getReadPointer(channel);
        for (int sample = 0; sample < samples; ++sample)
        {
            const auto value = data[sample];
            if (std::isfinite(value)) energy += static_cast<double>(value) * value;
        }
    }
    const auto meanSquare = static_cast<float>(energy / static_cast<double>(channels * samples));
    // 300 ms exponential VU integration. This tracks power in the audio
    // thread, while the editor applies a separate visual-only needle smoothing.
    const auto coefficient = std::exp(static_cast<float>(-samples / (currentSampleRate * 0.300)));
    auto& integratedSquare = isInput ? inputVuMeanSquare : outputVuMeanSquare;
    integratedSquare = coefficient * integratedSquare + (1.0f - coefficient) * meanSquare;
    const auto dbfs = 10.0f * std::log10(std::max(integratedSquare, 1.0e-12f));
    (isInput ? inputVuDbfs : outputVuDbfs).store(juce::jlimit(-72.0f, 24.0f, dbfs), std::memory_order_relaxed);
}
void HarmoHitAudioProcessor::getStateInformation(juce::MemoryBlock& data) {
    if (auto xml=parameters.copyState().createXml()) copyXmlToBinary(*xml,data);
}
void HarmoHitAudioProcessor::setStateInformation(const void* data,int size) {
    if (auto xml=getXmlFromBinary(data,size))
        if (xml->hasTagName(parameters.state.getType())) parameters.replaceState(juce::ValueTree::fromXml(*xml));
}
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new HarmoHitAudioProcessor(); }
