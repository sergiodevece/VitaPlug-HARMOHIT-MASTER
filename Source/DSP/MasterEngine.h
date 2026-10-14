#pragma once
#include "MasterChannelCore.h"
#include <juce_audio_basics/juce_audio_basics.h>

namespace vitaplug::harmohit::masterdsp {
struct MasterSettings {
    std::array<HarmoSettings, 3> banks{};
    bool midSide = false;
    float trimDb = 0.0f;
    bool bypass = false;
};
class MasterEngine final {
public:
    // `initial` carries the current parameter values so that every bank's DRIVE
    // 670 starts snapped at its present step (no ramp from 0 after prepare).
    void prepare(double rate, int maximumBlockSize, const MasterSettings& initial = {}) {
        if (!std::isfinite(rate) || rate <= 0.0) rate = 48000.0;
        capacity = std::clamp(maximumBlockSize, 1, 65536);
        for (auto& buffer : buffers) buffer.setSize(2, capacity);
        stereo.setDriveBank(drivecal::Bank::stereo); mid.setDriveBank(drivecal::Bank::mid); side.setDriveBank(drivecal::Bank::side);
        const auto& b = initial.bypass ? MasterSettings{}.banks : initial.banks;
        stereo.setInitialDrive(b[0].drive); mid.setInitialDrive(b[1].drive); side.setInitialDrive(b[2].drive);
        stereo.prepare(rate, capacity, 2);
        mid.prepare(rate, capacity, 1);
        side.prepare(rate, capacity, 1);
        mode.reset(rate, 0.020); mode.setCurrentAndTargetValue(0.0f);
        trim.reset(rate, 0.020); trim.setCurrentAndTargetValue(1.0f);
    }
    void reset() noexcept { stereo.reset(); mid.reset(); side.reset(); }
    int getLatencySamples() const noexcept { return stereo.getLatencySamples(); }
    MasterChannelCore& getBankForLab(int i) noexcept { return i == 0 ? stereo : i == 1 ? mid : side; }   // calibration tests only
    const MasterChannelCore& getBank(int i) const noexcept { return i == 0 ? stereo : i == 1 ? mid : side; }   // tests/telemetry only
    void process(float* const* channels, int count, int frames, MasterSettings settings) noexcept {
        if (!channels || count < 1 || frames <= 0 || capacity == 0) return;
        if (settings.bypass) settings = {};
        for (auto& bank : settings.banks) bank.outputTrimDb = 0.0f;
        mode.setTargetValue(settings.midSide ? 1.0f : 0.0f);
        const float db = std::isfinite(settings.trimDb) ? std::clamp(settings.trimDb, -24.0f, 12.0f) : 0.0f;
        trim.setTargetValue(std::pow(10.0f, db / 20.0f));
        constexpr float scale = 0.7071067811865475244f;
        for (int offset = 0; offset < frames; offset += capacity) {
            const int n = std::min(capacity, frames - offset);
            auto* left = buffers[0].getWritePointer(0);
            auto* right = buffers[0].getWritePointer(1);
            auto* m = buffers[1].getWritePointer(0);
            auto* s = buffers[2].getWritePointer(0);
            for (int i = 0; i < n; ++i) {
                const auto finite = [](float x) { return std::isfinite(x) ? x : 0.0f; };
                left[i] = finite(channels[0][offset+i]);
                right[i] = count > 1 ? finite(channels[1][offset+i]) : left[i];
                m[i] = (left[i] + right[i]) * scale;
                s[i] = (left[i] - right[i]) * scale;
            }
            float* lr[]{left, right}; float* midData[]{m}; float* sideData[]{s};
            // Both equal-latency routes stay warm, so mode changes do not reset filters.
            stereo.process(lr, 2, n, settings.banks[0]);
            mid.process(midData, 1, n, settings.banks[1]);
            side.process(sideData, 1, n, settings.banks[2]);
            for (int i = 0; i < n; ++i) {
                const float blend = mode.getNextValue(), gain = trim.getNextValue();
                const float decodedL = (m[i] + s[i]) * scale;
                const float decodedR = (m[i] - s[i]) * scale;
                channels[0][offset+i] = finiteOutput((left[i] + blend * (decodedL-left[i])) * gain);
                if (count > 1) channels[1][offset+i] = finiteOutput((right[i] + blend * (decodedR-right[i])) * gain);
            }
        }
    }
private:
    static float finiteOutput(float x) noexcept { return std::isfinite(x) ? x : 0.0f; }
    MasterChannelCore stereo, mid, side;
    std::array<juce::AudioBuffer<float>, 3> buffers;
    juce::SmoothedValue<float> mode, trim;
    int capacity = 0;
};
}
