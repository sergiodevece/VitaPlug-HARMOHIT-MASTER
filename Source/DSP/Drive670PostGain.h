#pragma once

#include <algorithm>
#include <cmath>

namespace vitaplug::drive670
{
// R0.2 POST GAIN: postGainDb = DRIVE step * 1 dB (0..+10 dB), applied after all
// compression and colour. It is a plain multiplier: no limiter, no clipping, no
// signal-dependent compensation. It never feeds the detector or the non-linear
// stages. Changes ramp linearly in the gain domain over a fixed short time and
// land exactly on the step's gain (no asymptotic smoothing error). One instance
// per core, so MID and SIDE banks each derive their gain from their own step.
class Drive670PostGain
{
public:
    static constexpr float dbPerStep = 1.0f;
    static constexpr double rampSeconds = 0.020;

    static float gainForStep(int step) noexcept
    {
        return static_cast<float>(std::pow(10.0, static_cast<double>(dbPerStep) * std::clamp(step, 0, 10) / 20.0));
    }

    void prepare(double processingRate) noexcept
    {
        rampSamples = std::max(1, static_cast<int>(std::lround(std::max(1.0, processingRate) * rampSeconds)));
        snap();
    }
    // Called when DRIVE changes (audio thread safe: no allocation).
    void setStep(int step) noexcept
    {
        const float t = gainForStep(step);
        if (t == target) return;
        target = t; remaining = rampSamples; increment = (target - current) / static_cast<float>(rampSamples);
    }
    void snap() noexcept { current = target; remaining = 0; increment = 0.0f; }
    // One value per frame, shared by every channel of the frame (no inter-channel offset).
    float next() noexcept
    {
        if (remaining > 0) { current += increment; if (--remaining == 0) current = target; }
        return current;
    }
    float getCurrent() const noexcept { return current; }
    float getTarget() const noexcept { return target; }

private:
    float current = 1.0f, target = 1.0f, increment = 0.0f;
    int remaining = 0, rampSamples = 1;
};
} // namespace vitaplug::drive670
