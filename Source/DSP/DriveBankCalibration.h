#pragma once

#include <algorithm>
#include <cmath>

namespace vitaplug::harmohit::masterdsp
{
// Integration-only calibration of the approved DRIVE 670 R0.2 core inside
// HARMOHIT MASTER. Kept apart from Drive670Core.h (a verbatim copy of the LAB)
// so it can be revised after the M/S smoke without touching the approved sound.
namespace drivecal
{
enum class Bank { stereo, mid, side };

// Reciprocal operating offset: the core sees x * G and its output is scaled by
// 1/G, so below threshold the bank stays level-neutral (no audible pre-gain),
// while the detector and the valve stages work at the bank's own reference.
//   STEREO  0 dB      -> identical to DRIVE 670 LAB R0.2.
//   MID    -3.01 dB   -> M = (L+R)/sqrt2 is 3 dB hotter than L/R for a mono
//                        programme; with -3.01 dB a mono programme in M/S mode
//                        drives the core exactly like STEREO mode does.
//   SIDE   PROVISIONAL, set from measurements (Documentation/R2_DRIVE670_INTEGRATION.md):
//                        real mixes carry Side 15-25 dB below Mid, where the
//                        L/R-calibrated core would barely react.
constexpr float stereoOffsetDb = 0.0f;
constexpr float midOffsetDb = -3.0103f;
constexpr float sideOffsetDb = 15.0f;

constexpr float offsetDb(Bank b) noexcept
{
    return b == Bank::stereo ? stereoOffsetDb : b == Bank::mid ? midOffsetDb : sideOffsetDb;
}

// MASTER's DRIVE parameter is continuous (0..1 after /100, shown 0..10). The
// approved engine has 11 integer steps. Nearest step with a 0.05-step
// hysteresis band so that automation hovering on a boundary cannot toggle
// between two steps every block. prepare() snaps to the current value.
class StepMapper
{
public:
    static constexpr float hysteresis = 0.05f;
    void snap(float control01) noexcept { step = nearest(control01); }
    int map(float control01) noexcept
    {
        const float v = position(control01);
        if (std::abs(v - static_cast<float>(step)) >= 0.5f + hysteresis) step = nearest(control01);
        return step;
    }
    int current() const noexcept { return step; }
    static int nearest(float control01) noexcept { return std::clamp(static_cast<int>(std::lround(position(control01))), 0, 10); }

private:
    static float position(float control01) noexcept { return 10.0f * std::clamp(std::isfinite(control01) ? control01 : 0.0f, 0.0f, 1.0f); }
    int step = 0;
};
} // namespace drivecal
} // namespace vitaplug::harmohit::masterdsp
