#pragma once

#include <algorithm>
#include <array>
#include <cmath>

namespace vitaplug::harmohit::masterdsp
{
// HARMOHIT MASTER R1.2 DRIVE: programme-referenced tanh saturation.
//
//   R    = sqrt(mean square), attack 60 ms / release 400 ms, floor -60 dBFS
//   Pk   = peak envelope, instantaneous attack, 150 ms release
//   G    = slow programme memory (mean square, 350 ms both ways), floor -60 dBFS
//   s    = smoothstep of the crest Pk/G between 16 and 24 dB (onset after
//          silence, pause or a sudden level rise; ~0 in dense programme).
//          G is deliberately slower than R so that the protection lasts
//          through the attack (tens of ms) and then releases into the body.
//   T    = max(R * 10^(H(d)/20), s * Pk / U(d))
//   wet  = T * (tanh(x/T) + beta(d) * HP10Hz(tanh(x/T)^2))
//   out  = x + w(d) * (wet - x)          (returned as the residual)
//
// Small-signal slope is exactly 1: no gain, makeup or normalisation. The knee
// T only changes how much of the waveform reaches the curve, so moving T can
// never pump the level. T is continuous in time because Pk follows |x|
// continuously and every detector is a one-pole. The bound |x|/T <= U(d)
// during isolated onsets limits their peak reduction (attack protection)
// without a lookahead, so no latency is added.
class AdaptiveDrive
{
public:
    struct Law { float kneeAboveRmsDb, mix, even, onsetU; };

    // d is the normalised DRIVE control 0..1 (DRIVE 10 == 1).
    static Law law(float d) noexcept
    {
        d = std::clamp(d, 0.0f, 1.0f);
        return { 20.0f - 15.0f * d,                         // knee 20 dB .. 5 dB above RMS
                 0.90f * std::sqrt(d),                      // parallel amount
                 0.20f * d,                                 // second-harmonic weight
                 0.45f + 1.15f * d * std::sqrt(d) };        // onset bound on |x|/T
    }

    struct State
    {
        double meanSquare = 0.0, gateMeanSquare = 0.0;
        float peak = 0.0f;
        std::array<float, 2> evenIn {}, evenOut {};
    };

    void prepare(double oversampledRate) noexcept
    {
        const double fs = std::max(1.0, oversampledRate);
        powerAttack = static_cast<float>(1.0 - std::exp(-1.0 / (fs * 0.060)));
        powerRelease = static_cast<float>(1.0 - std::exp(-1.0 / (fs * 0.400)));
        gateCoefficient = static_cast<float>(1.0 - std::exp(-1.0 / (fs * 0.350)));
        peakRelease = static_cast<float>(std::exp(-1.0 / (fs * 0.150)));
        dcPole = static_cast<float>(std::exp(-2.0 * 3.14159265358979323846 * 10.0 / fs));
    }

    // Processes one oversampled frame of n (1 or 2) linked channels in place
    // into residuals: on return r[c] = out[c] - x[c]. Detectors always run so
    // that leaving DRIVE 0 starts from a settled state; d == 0 yields exact 0.
    void processFrame(const float* x, float* r, int n, float d, State& s) const noexcept
    {
        float magnitude = 0.0f;
        for (int c = 0; c < n; ++c) magnitude = std::max(magnitude, std::abs(x[c]));
        const double power = static_cast<double>(magnitude) * magnitude;
        s.meanSquare += (power > s.meanSquare ? powerAttack : powerRelease) * (power - s.meanSquare);
        s.gateMeanSquare += gateCoefficient * (power - s.gateMeanSquare);
        s.peak = std::max(magnitude, s.peak * peakRelease);

        if (d <= 0.0f)
        {
            for (int c = 0; c < n; ++c) r[c] = 0.0f;
            return;
        }
        if (d != cachedDrive)   // law depends only on d; static settings reuse it (bit-identical)
        {
            cachedDrive = d; cachedLaw = law(d);
            cachedKneeGain = std::pow(10.0f, cachedLaw.kneeAboveRmsDb / 20.0f);
        }
        const auto& l = cachedLaw;
        const float rms = static_cast<float>(std::sqrt(std::max(s.meanSquare, rmsFloorSquared)));
        const float memory = static_cast<float>(std::sqrt(std::max(s.gateMeanSquare, rmsFloorSquared)));
        const float crestDb = 20.0f * std::log10(std::max(s.peak, 1.0e-9f) / memory);
        const float g = std::clamp((crestDb - onsetCrestLowDb) / (onsetCrestHighDb - onsetCrestLowDb), 0.0f, 1.0f);
        const float onset = g * g * (3.0f - 2.0f * g);
        const float kneeRms = rms * cachedKneeGain;
        const float knee = std::max(kneeRms, onset * s.peak / l.onsetU);
        const float inverseKnee = 1.0f / knee;

        for (int c = 0; c < n; ++c)
        {
            const float t = std::tanh(x[c] * inverseKnee);
            const float even = t * t;
            auto& in = s.evenIn[static_cast<size_t>(c)];
            auto& out = s.evenOut[static_cast<size_t>(c)];
            const float evenAc = even - in + dcPole * out;
            in = even; out = evenAc;
            const float wet = knee * (t + l.even * evenAc);
            r[c] = l.mix * (wet - x[c]);
        }
    }

private:
    static constexpr double rmsFloorSquared = 1.0e-6;   // -60 dBFS
    static constexpr float onsetCrestLowDb = 16.0f;
    static constexpr float onsetCrestHighDb = 24.0f;
    mutable float cachedDrive = -1.0f, cachedKneeGain = 1.0f;
    mutable Law cachedLaw {};
    float gateCoefficient = 0.0f, powerAttack = 0.0f, powerRelease = 0.0f, peakRelease = 0.0f, dcPole = 0.0f;
};
} // namespace vitaplug::harmohit::masterdsp
