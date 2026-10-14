#pragma once
// AUDIT-ONLY PROTOTYPE (R12). Not product code. Implements recommended
// alternative "A": program-referenced knee saturation.
//   R   = sqrt(one-pole mean square of x), attack 60 ms / release 400 ms,
//         linked over the channels of one core, floored at -50 dBFS.
//   T   = R * 10^(h(d)/20)      knee placed h dB above programme RMS
//   u   = x / T
//   wet = T * (tanh(u) + beta(d) * tanh(u)^2)  (even part DC-blocked, 10 Hz)
//   y   = x + w(d) * (wet - x)
// Small-signal slope is 1 (no makeup gain, no normalisation). The knee moves
// with programme level, so the amount of colour depends on crest structure,
// not on absolute dBFS.  No transient detector; attacks are handled by the
// knee height h(d) and the blend w(d).
#include <array>
#include <cmath>
#include <algorithm>
struct ProtoParams { float hDb, w, beta; };
inline int protoVariant = 0; // 0 = A (first law), 1 = A2 (calibration variant)
inline ProtoParams protoLaw(float d)
{
    d = std::clamp(d, 0.0f, 1.0f);
    if (protoVariant == 1)
        return { 20.0f - 15.0f * d * (2.0f - d) * 0.5f - 7.5f * d * d, // 20 dB .. 5 dB, flatter start
                 0.90f * std::sqrt(d), 0.25f * d };
    return { 16.0f - 12.0f * d,                // 16 dB .. 4 dB above RMS
             0.85f * std::sqrt(d),             // 0 .. 0.85 parallel blend
             0.20f * d };                      // even component, HARMOHIT R2 flavour
}
struct ProtoState { double ms = 1.0e-6; float dcIn = 0, dcOut = 0; };
struct ProtoDrive
{
    double fs = 192000.0; float att = 0, rel = 0, dcPole = 0;
    void prepare(double oversampledRate)
    {
        fs = oversampledRate;
        att = 1.0f - (float) std::exp(-1.0 / (fs * 0.060));
        rel = 1.0f - (float) std::exp(-1.0 / (fs * 0.400));
        dcPole = (float) std::exp(-2.0 * 3.14159265358979 * 10.0 / fs);
    }
    // x: per-channel samples of one frame; n channels share the detector.
    void processFrame(float* x, int n, float d, std::array<ProtoState, 2>& s) const
    {
        double p = 0; for (int c = 0; c < n; ++c) p = std::max(p, (double) x[c] * x[c]);
        for (int c = 0; c < n; ++c) { auto& m = s[(size_t) c].ms; m += (p > m ? att : rel) * (p - m); }
        if (d <= 0.0f) return;
        const auto law = protoLaw(d);
        double ms = 0; for (int c = 0; c < n; ++c) ms = std::max(ms, s[(size_t) c].ms);
        const float R = (float) std::sqrt(std::max(ms, protoVariant == 1 ? 1.0e-4 : 1.0e-5)); // -50 dBFS floor
        const float T = R * std::pow(10.0f, law.hDb / 20.0f);
        for (int c = 0; c < n; ++c)
        {
            const float t = std::tanh(x[c] / T);
            const float even = t * t;                         // >= 0, needs DC block
            auto& st = s[(size_t) c];
            const float evenAc = even - st.dcIn + dcPole * st.dcOut; st.dcIn = even; st.dcOut = evenAc;
            const float wet = T * (t + law.beta * evenAc);
            x[c] = x[c] + law.w * (wet - x[c]);
        }
    }
};
