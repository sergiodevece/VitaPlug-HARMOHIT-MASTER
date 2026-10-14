#include "PluginProcessor.h"
#include "PluginEditor.h"
#if JUCE_MAC
#include <AudioToolbox/AudioToolbox.h>
#include <CoreFoundation/CoreFoundation.h>
#endif
#include <iostream>
#include <iomanip>
#include <vector>
#include <array>
#include <complex>
#include <stdexcept>
#include <memory>
#include <cstring>
#include <fstream>
#include <bit>

namespace {
constexpr double pi = juce::MathConstants<double>::pi;
constexpr float matrix = 0.7071067811865475f;
const char* banks[]{"stereo", "mid", "side"};
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
// Test-only host for the REGISTERED, INSTALLED AU. A bundle path must not be
// silently resolved to an older component with the same AU identifiers.
#if JUCE_MAC
void ok(OSStatus status, const char* message) { require(status == noErr, message); }
class NativeAU {
public:
    explicit NativeAU(const juce::String& bundlePath) {
        require(bundlePath.isEmpty(), "AU test supports only the installed component; a fresh bundle requires separately authorised validation");
        AudioComponentDescription description{};
        description.componentType = juce::ByteOrder::bigEndianInt("aufx");
        description.componentSubType = juce::ByteOrder::bigEndianInt("HmR1");
        description.componentManufacturer = juce::ByteOrder::bigEndianInt("Vtpl");
        const auto component = AudioComponentFindNext(nullptr, &description);
        require(component != nullptr, "MASTER AU component");
        ok(AudioComponentInstanceNew(component, &unit), "instantiate MASTER AU");
        UInt32 size = 0;
        ok(AudioUnitGetPropertyInfo(unit, kAudioUnitProperty_ParameterList, kAudioUnitScope_Global, 0, &size, nullptr), "AU parameter list size");
        std::vector<AudioUnitParameterID> ids(size / sizeof(AudioUnitParameterID));
        ok(AudioUnitGetProperty(unit, kAudioUnitProperty_ParameterList, kAudioUnitScope_Global, 0, ids.data(), &size), "AU parameter list");
        for (auto id : ids) {
            AudioUnitParameterInfo info{}; size = sizeof(info);
            ok(AudioUnitGetProperty(unit, kAudioUnitProperty_ParameterInfo, kAudioUnitScope_Global, id, &info, &size), "AU parameter info");
            juce::String name(info.name);
            if ((info.flags & kAudioUnitParameterFlag_HasCFNameString) != 0 && info.cfNameString)
                name = juce::String::fromCFString(info.cfNameString);
            parameters.push_back({name, id, info.minValue, info.maxValue});
            if ((info.flags & kAudioUnitParameterFlag_CFNameRelease) != 0 && info.cfNameString) CFRelease(info.cfNameString);
        }
        require(parameters.size() == 19, "AU exposes 19 parameters");
    }
    ~NativeAU() { if (unit) { AudioUnitUninitialize(unit); AudioComponentInstanceDispose(unit); } }
    void set(const juce::String& id, float value) {
        juce::String name;
        if (id == "processingMode") name = "Processing Mode";
        else if (id == "editBank") name = "M/S Edit Bank";
        else if (id == "outputTrim") name = "Output Trim";
        else if (id == "bypass") name = "Bypass";
        else name = id.replaceCharacter('.', ' ');
        for (const auto& p : parameters) if (p.name == name) {
            // JUCE AU publishes normalised continuous values (0..1), with
            // value strings showing APVTS percent/dB. Never send 0..100 to it.
            float normalised = value;
            if (id.endsWith(".drive") || id.endsWith(".harmonics") || id.endsWith(".exciter")) normalised = value / 100.0f;
            else if (id == "outputTrim") normalised = (value + 24.0f) / 36.0f;
            const float hostValue = p.minimum + (p.maximum - p.minimum) * normalised;
            ok(AudioUnitSetParameter(unit, p.id, kAudioUnitScope_Global, 0, hostValue, 0), "AU set parameter");
            AudioUnitParameterValue read{};
            ok(AudioUnitGetParameter(unit, p.id, kAudioUnitScope_Global, 0, &read), "AU read parameter");
            if (std::abs(read - hostValue) >= 0.001f)
                throw std::runtime_error("AU parameter readback " + name.toStdString() + " requested=" + std::to_string(hostValue) + " actual=" + std::to_string(read));
            return;
        }
        throw std::runtime_error("missing AU parameter " + name.toStdString());
    }
    void prepare(double rate) {
        AudioUnitUninitialize(unit);
        AudioStreamBasicDescription format{};
        format.mSampleRate = rate; format.mFormatID = kAudioFormatLinearPCM;
        format.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked | kAudioFormatFlagIsNonInterleaved;
        format.mBytesPerPacket = format.mBytesPerFrame = sizeof(float);
        format.mFramesPerPacket = 1; format.mChannelsPerFrame = 2; format.mBitsPerChannel = 32;
        ok(AudioUnitSetProperty(unit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &format, sizeof(format)), "AU input format");
        ok(AudioUnitSetProperty(unit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 0, &format, sizeof(format)), "AU output format");
        UInt32 frames = 256;
        ok(AudioUnitSetProperty(unit, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 0, &frames, sizeof(frames)), "AU maximum frames");
        AURenderCallbackStruct callback{inputCallback, this};
        ok(AudioUnitSetProperty(unit, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0, &callback, sizeof(callback)), "AU input callback");
        ok(AudioUnitInitialize(unit), "AU initialize"); sampleTime = 0;
    }
    void process(juce::AudioBuffer<float>& buffer) {
        input = &buffer;
        struct TwoBuffers { UInt32 count; AudioBuffer buffers[2]; } list{};
        list.count = 2;
        for (int c = 0; c < 2; ++c) list.buffers[c] = {1, static_cast<UInt32>(buffer.getNumSamples()) * static_cast<UInt32>(sizeof(float)), output[static_cast<size_t>(c)].data()};
        AudioTimeStamp time{}; time.mSampleTime = sampleTime; time.mFlags = kAudioTimeStampSampleTimeValid;
        AudioUnitRenderActionFlags flags{};
        ok(AudioUnitRender(unit, &flags, &time, 0, static_cast<UInt32>(buffer.getNumSamples()), reinterpret_cast<AudioBufferList*>(&list)), "AU render");
        for (int c = 0; c < 2; ++c) buffer.copyFrom(c, 0, output[static_cast<size_t>(c)].data(), buffer.getNumSamples());
        sampleTime += buffer.getNumSamples();
    }
private:
    static OSStatus inputCallback(void* context, AudioUnitRenderActionFlags*, const AudioTimeStamp*, UInt32, UInt32 frames, AudioBufferList* list) {
        auto& self = *static_cast<NativeAU*>(context);
        if (!self.input || frames > static_cast<UInt32>(self.input->getNumSamples())) return kAudioUnitErr_TooManyFramesToProcess;
        for (UInt32 c = 0; c < list->mNumberBuffers; ++c) {
            list->mBuffers[c].mNumberChannels = 1; list->mBuffers[c].mDataByteSize = frames * sizeof(float);
            if (list->mBuffers[c].mData) std::memcpy(list->mBuffers[c].mData, self.input->getReadPointer(static_cast<int>(c)), frames * sizeof(float));
            else list->mBuffers[c].mData = const_cast<float*>(self.input->getReadPointer(static_cast<int>(c)));
        }
        return noErr;
    }
    AudioUnit unit{}; double sampleTime{};
    juce::AudioBuffer<float>* input{};
    std::array<std::array<float, 256>, 2> output{};
    struct Parameter { juce::String name; AudioUnitParameterID id; float minimum, maximum; };
    std::vector<Parameter> parameters;
};
#else
class NativeAU {
public:
    explicit NativeAU(const juce::String&) { throw std::runtime_error("AU diagnosis requires macOS"); }
    void set(const juce::String&, float) {}
    void prepare(double) {}
    void process(juce::AudioBuffer<float>&) {}
};
#endif

struct Runner {
    std::unique_ptr<HarmoHitAudioProcessor> processor;
    std::unique_ptr<NativeAU> au;
    Runner(bool native, const juce::String& path) { if (native) au = std::make_unique<NativeAU>(path); else processor = std::make_unique<HarmoHitAudioProcessor>(); }
    void set(const juce::String& id, float value) {
        if (au) { au->set(id, value); return; }
        auto* p = processor->parameters.getParameter(id); require(p != nullptr, "APVTS parameter exists");
        p->setValueNotifyingHost(p->convertTo0to1(value));
        require(std::abs(processor->parameters.getRawParameterValue(id)->load() - value) < 0.001f, "APVTS readback");
    }
    void prepare(double rate) { if (au) au->prepare(rate); else processor->prepareToPlay(rate, 256); }
    void process(juce::AudioBuffer<float>& b) { if (au) au->process(b); else { juce::MidiBuffer midi; processor->processBlock(b, midi); } }
};
struct Result { std::vector<float> input, output; float grDb = 0.0f; bool hasGr = false; };
Result render(bool native, const juce::String& path, double rate, int bank, int kind, double db, float drive) {
    Runner runner(native, path);
    runner.set("processingMode", bank == 0 ? 0.0f : 1.0f);
    runner.set(juce::String(banks[bank]) + ".drive", drive * 10.0f);
    runner.set("outputTrim", 0); runner.set("bypass", 0);
    runner.prepare(rate);
    const int warm = static_cast<int>(rate / 2), window = static_cast<int>(rate / 10);
    const double amplitude = std::pow(10.0, db / 20.0);
    Result result; result.input.reserve(static_cast<size_t>(window)); result.output.reserve(static_cast<size_t>(window));
    juce::AudioBuffer<float> buffer(2, 256);
    for (int offset = 0; offset < warm + window; offset += 256) {
        const int n = std::min(256, warm + window - offset); buffer.setSize(2, n, false, false, true);
        for (int i = 0; i < n; ++i) {
            const int index = offset + i;
            const double t = index / rate;
            double x = std::sin(2 * pi * 1000 * t);
            if (kind == 1) x = index % static_cast<int>(rate / 100) == 0 ? 1.0 : 0.0;
            if (kind == 2) {
                const double age = (index % static_cast<int>(rate / 50)) / rate;
                x = std::exp(-age / 0.002) * (0.8 * std::cos(2 * pi * 100 * age) + 0.2 * std::cos(2 * pi * 4300 * age));
            }
            const float selected = static_cast<float>(amplitude * x);
            const float other = static_cast<float>(amplitude * 0.45 * std::sin(2 * pi * 1300 * t));
            const float left = bank == 0 ? selected : (selected + other) * matrix;
            const float right = bank == 0 ? other : (bank == 1 ? (selected - other) : (other - selected)) * matrix;
            buffer.setSample(0, i, left); buffer.setSample(1, i, right);
            if (index >= warm) result.input.push_back(selected);
        }
        runner.process(buffer);
        for (int i = 0; i < n; ++i) if (offset + i >= warm) {
            const auto l = buffer.getSample(0, i), r = buffer.getSample(1, i);
            result.output.push_back(bank == 0 ? l : (bank == 1 ? l + r : l - r) * matrix);
        }
    }
    if (!native) { result.grDb = runner.processor->getEngine().getBank(bank).getDriveMeter().gainReductionDb; result.hasGr = true; }
    return result;
}
struct Stats { double rms{}, peak{}, crest{}; };
Stats statistics(const std::vector<float>& signal) {
    Stats value;
    for (float x : signal) { require(std::isfinite(x), "finite DRIVE audio"); value.rms += static_cast<double>(x) * x; value.peak = std::max(value.peak, std::abs(static_cast<double>(x))); }
    value.rms = std::sqrt(value.rms / static_cast<double>(signal.size()));
    value.crest = 20 * std::log10(value.peak / std::max(value.rms, 1e-20)); return value;
}
double harmonic(const std::vector<float>& signal, double rate, int order) {
    std::complex<double> sum{};
    for (size_t i = 0; i < signal.size(); ++i) sum += static_cast<double>(signal[i]) * std::polar(1.0, -2 * pi * 1000 * order * static_cast<double>(i) / rate);
    return 2 * std::abs(sum) / static_cast<double>(signal.size());
}
void editorPath() {
    HarmoHitAudioProcessor p; HarmoHitAudioProcessorEditor editor(p);
    for (int bank = 0; bank < 3; ++bank) {
        auto set = [&](const char* id, float v) { auto* parameter = p.parameters.getParameter(id); parameter->setValueNotifyingHost(parameter->convertTo0to1(v)); };
        set("processingMode", bank == 0 ? 0.0f : 1.0f); set("editBank", bank == 2 ? 1.0f : 0.0f); editor.synchroniseControls();
        auto* slider = dynamic_cast<juce::Slider*>(editor.findChildWithID("drive")); require(slider != nullptr, "DRIVE knob exists");
        slider->setValue(70, juce::sendNotificationSync);
        for (int b = 0; b < 3; ++b) require(std::abs(p.parameters.getRawParameterValue(juce::String(banks[b]) + ".drive")->load() - (b <= bank ? 70.0f : 0.0f)) < 0.001f, "DRIVE attachment updates selected bank only");
        set("bypass", 0);
    }
    std::cerr << "PASS editor DRIVE -> attachments -> three APVTS banks\n";
}
void automationAndState() {
    HarmoHitAudioProcessor a, b;
    auto set = [](HarmoHitAudioProcessor& p, const juce::String& id, float value) {
        auto* parameter = p.parameters.getParameter(id);
        parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
    };
    a.prepareToPlay(48000, 256); b.prepareToPlay(48000, 256);
    juce::AudioBuffer<float> x(2, 256), y(2, 256); juce::MidiBuffer midi;
    float previous = 0; double maximumStep = 0;
    constexpr float values[]{0, 10, 30, 50, 70, 100};
    for (int block = 0; block < 100; ++block) {
        const float value = values[(block / 10) % 6];
        for (const auto* prefix : banks) for (auto* p : {&a, &b}) set(*p, juce::String(prefix) + ".drive", value);
        for (auto* p : {&a, &b}) set(*p, "processingMode", block >= 30 && (block / 10) % 2 == 0 ? 1.0f : 0.0f);
        set(a, "editBank", static_cast<float>(block % 2)); set(b, "editBank", 0);
        for (int i = 0; i < 256; ++i) { x.setSample(0, i, 0.2f); x.setSample(1, i, 0.1f); }
        y.makeCopyOf(x); a.processBlock(x, midi); b.processBlock(y, midi);
        for (int i = 0; i < 256; ++i) for (int c = 0; c < 2; ++c) {
            const float sample = x.getSample(c, i);
            require(std::isfinite(sample), "finite automated DRIVE output");
            require(std::bit_cast<uint32_t>(sample) == std::bit_cast<uint32_t>(y.getSample(c, i)), "editing bank never changes DRIVE audio");
            if (c == 0) { if (block >= 10) maximumStep = std::max(maximumStep, std::abs(static_cast<double>(sample - previous))); previous = sample; }
        }
    }
    require(maximumStep < 0.001, "smoothed DRIVE and mode change have no abrupt DC step");
    juce::MemoryBlock state; a.getStateInformation(state);
    HarmoHitAudioProcessor restored; restored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    for (const auto* prefix : banks) {
        const auto id = juce::String(prefix) + ".drive";
        require(std::bit_cast<uint32_t>(a.parameters.getRawParameterValue(id)->load()) == std::bit_cast<uint32_t>(restored.parameters.getRawParameterValue(id)->load()), "three DRIVE banks survive state restore");
    }
    std::cerr << "PASS DRIVE automation/state/edit-only regression; maximum_DC_step=" << maximumStep << '\n';
}
}
// R1.2 adaptive DRIVE regressions removed with the R1.2 engine (DRIVE 670 R0.2 integration);
// the new engine is covered by Tests/Drive670IntegrationTests.cpp.
int main(int argc, char** argv) {
    try {
        juce::ScopedJuceInitialiser_GUI gui;
        const bool native = argc > 1 && juce::String(argv[1]).startsWith("--au");
        bool dspOnly = false;
        bool compareAU = false;
        for (int i = 1; i < argc; ++i) if (juce::String(argv[i]) == "--dsp-only") dspOnly = true;
        for (int i = 1; i < argc; ++i) if (juce::String(argv[i]) == "--compare-au") compareAU = true;
        const juce::String path = native && argc > 2 && !juce::String(argv[2]).startsWith("--") ? juce::String(argv[2]) : juce::String{};
        std::ofstream csv;
        for (int i = 1; i < argc; ++i) if (juce::String(argv[i]).startsWith("--csv=")) {
            csv.open(juce::String(argv[i]).substring(6).toStdString()); require(csv.is_open(), "open measurement CSV");
        }
        auto& report = csv.is_open() ? static_cast<std::ostream&>(csv) : std::cout;
        if (!native && !dspOnly) editorPath();
        if (!native) automationAndState();
        report << std::setprecision(10) << "bank,rate,signal,input_peak_dBFS,drive,input_rms,output_rms,input_peak,output_peak,delta_peak,delta_rms,delta_relative_rms,H3_dBc,H5_dBc,THD_2_9_percent,input_crest_dB,output_crest_dB\n";
        int rows = 0;
        double maximumAUError = 0; size_t comparedSamples = 0;
        for (double rate : {44100.0, 48000.0, 96000.0}) for (int bank = 0; bank < 3; ++bank) for (int kind = 0; kind < 3; ++kind) for (double db : {-24.0, -18.0, -12.0, -6.0}) {
            const auto neutral = render(native, path, rate, bank, kind, db, 0);
            double previousGr = 1.0;
            for (float drive : {0.0f, 1.0f, 3.0f, 5.0f, 7.0f, 10.0f}) {
                const auto result = drive <= 0 ? neutral : render(native, path, rate, bank, kind, db, drive);
                if (compareAU) {
                    const auto au = render(true, {}, rate, bank, kind, db, drive);
                    require(au.output.size() == result.output.size(), "matching AU frame count");
                    for (size_t i = 0; i < result.output.size(); ++i) {
                        const double error = std::abs(static_cast<double>(au.output[i]) - result.output[i]);
                        maximumAUError = std::max(maximumAUError, error); ++comparedSamples;
                    }
                    require(maximumAUError <= 1e-7, "installed AU matches source DRIVE audio");
                }
                const auto in = statistics(result.input), out = statistics(result.output);
                double deltaPeak = 0, deltaEnergy = 0;
                for (size_t i = 0; i < result.output.size(); ++i) {
                    const double d = static_cast<double>(result.output[i]) - neutral.output[i];
                    deltaPeak = std::max(deltaPeak, std::abs(d)); deltaEnergy += d * d;
                }
                const double deltaRms = std::sqrt(deltaEnergy / static_cast<double>(result.output.size()));
                require(drive <= 0 ? deltaPeak <= 0 : deltaPeak > 1e-8, "DRIVE nonzero changes actual samples in every bank");
                // DRIVE 670: the monotonic property is gain reduction (the raw difference
                // to neutral is not monotonic once heavy compression meets POST GAIN, as
                // in the approved LAB). Checked on the processor path, where the bank meter exists.
                if (result.hasGr) { require(result.grDb <= previousGr + 0.05, "DRIVE 670 gain reduction is non-decreasing with DRIVE"); previousGr = result.grDb; }
                const double fundamental = harmonic(result.output, rate, 1);
                double h3 = 0, h5 = 0, thd = 0;
                if (kind == 0) {
                    for (int order : {2, 3, 4, 5, 6, 7, 8, 9}) { const double h = harmonic(result.output, rate, order); thd += h * h; }   // 670: even + odd
                    thd = 100 * std::sqrt(thd) / std::max(fundamental, 1e-20);
                    h3 = 20 * std::log10(std::max(harmonic(result.output, rate, 3) / fundamental, 1e-12));
                    h5 = 20 * std::log10(std::max(harmonic(result.output, rate, 5) / fundamental, 1e-12));
                    if (drive >= 3) require(harmonic(result.output, rate, 3) > 100 * harmonic(neutral.output, rate, 3), "DRIVE generates H3 above neutral numerical floor");
                    if (drive >= 10) require(thd > 0.1, "maximum DRIVE (670) sine distortion H2..H9 is measurable at every tested level");
                }
                report << banks[bank] << ',' << rate << ',' << (kind == 0 ? "sine1k" : kind == 1 ? "impulse" : "attack") << ',' << db << ',' << drive << ','
                    << in.rms << ',' << out.rms << ',' << in.peak << ',' << out.peak << ',' << deltaPeak << ',' << deltaRms << ',' << deltaRms / std::max(statistics(neutral.output).rms, 1e-20) << ','
                    << h3 << ',' << h5 << ',' << thd << ',' << in.crest << ',' << out.crest << '\n'; ++rows;
            }
        }
        std::cerr << "PASS DRIVE matrix rows=" << rows << " backend=" << (native ? "installed AU" : "PluginProcessor") << '\n';
        if (compareAU) std::cerr << "PASS installed AU vs source compared_samples=" << comparedSamples << " maximum_sample_error=" << maximumAUError << '\n';
    } catch (const std::exception& error) { std::cerr << "FAIL " << error.what() << '\n'; return 1; }
}
