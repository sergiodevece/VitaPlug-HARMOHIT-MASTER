#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include "Drive670PostGain.h"

namespace vitaplug::drive670
{
// DRIVE 670 LAB core. Rate-agnostic, allocation-free, no oversampling of its
// own: the host engine decides the processing rate (4x in the lab plugin, the
// existing 4x domain of HARMOHIT MASTER after integration).
//
// Signal flow per frame of n linked channels (n = 2 for L/R, 1 for Mid or Side):
//
//   x --[input attenuator Gin, 1 dB steps]--> grid of the variable-mu stage
//     --[variable-mu push-pull cell: gain g = exp(-Vc) (remote cutoff, dB-linear
//        control); curvature grows as the effective bias b(Vc) falls]--> cell
//     --[output amplifier: fixed-bias push-pull stage (Child-law halves with a
//        plate-current ceiling and an imbalance) = main colour, excited by Gin]
//     --[output transformer: LF core saturation]
//     --[static 1/Gin output staging; no makeup, no loudness normalisation]--> wet
//   out = x + mix * (wet - x)        (mix ramps 0 <-> 1 around DRIVE 0)
//   out *= postGain                   (R0.2: DRIVE step * 1 dB, after everything; see Drive670PostGain.h)
//
//   Sidechain (FEEDBACK): |cell| of the linked channels -> threshold ->
//   sidechain gain -> RC network (attack, release, TC5 program-dependent store)
//   -> control voltage Vc. Because g multiplies the whole cell transfer, the
//   loop cannot latch up, and its ratio rises with level (1 + Ksc * out), the
//   variable-mu behaviour.
//
// This is a behavioural model in the spirit of the 670 topology, not a circuit
// simulation of 6386/6973 valves or of its transformers.
struct StepConfig
{
    float inputSteps;        // input attenuator steps above the operating reference (1 dB each)
    float threshold;         // sidechain threshold in cell units (calibrated, see Documentation)
    float imbalance;         // push-pull imbalance -> even harmonics
    float transformer;       // output-transformer LF saturation amount
    float attackMs;          // Fairchild-style time-constant settings
    float releaseS;
    float slowReleaseS;      // TC5 program-dependent store (equals releaseS when unused)
    float chargeS;           // store charge time; 0 disables program dependence
    float sidechainGain;     // Ksc: loop ratio ~ 1 + Ksc * output (variable-mu)
};

// DRIVE 1..10. Regime 1 = TC5, regime 2 = TC3, regime 3 = TC2 (user settings).
// Thresholds are calibrated offline (Tests/Calibrate.cpp --calibrate, output in
// Documentation/calibration_output.txt) so that MIX-LOGIC at -18 dBFS RMS reaches
// 0.5/1/1.5 | 2/2.4/2.7/3 | 3.5/4.2/5 dB mean GR in its loudest passages.
// They are not universal gain-reduction guarantees.
inline constexpr std::array<StepConfig, 10> stepTable {{
    //  steps  threshold  imbalance  transformer  att   rel   slow   charge  Ksc
    {   3.0f,  0.3613f,   0.040f,    0.10f,       0.2f, 2.0f, 10.0f, 1.0f, 1.5f },  // 1  TC5
    {   3.0f,  0.2862f,   0.050f,    0.14f,       0.2f, 2.0f, 10.0f, 1.0f, 1.5f },  // 2  TC5
    {   3.0f,  0.2168f,   0.060f,    0.18f,       0.2f, 2.0f, 10.0f, 1.0f, 1.5f },  // 3  TC5
    {   5.0f,  0.2174f,   0.070f,    0.22f,       0.4f, 2.0f,  2.0f, 0.0f, 1.5f },  // 4  TC3
    {   5.0f,  0.1611f,   0.080f,    0.26f,       0.4f, 2.0f,  2.0f, 0.0f, 1.5f },  // 5  TC3
    {   6.0f,  0.1609f,   0.090f,    0.30f,       0.4f, 2.0f,  2.0f, 0.0f, 1.5f },  // 6  TC3
    {   6.0f,  0.1189f,   0.100f,    0.34f,       0.4f, 2.0f,  2.0f, 0.0f, 1.5f },  // 7  TC3
    {   8.0f,  0.2043f,   0.110f,    0.40f,       0.2f, 0.8f,  0.8f, 0.0f, 2.5f },  // 8  TC2
    {   9.0f,  0.2008f,   0.120f,    0.46f,       0.2f, 0.8f,  0.8f, 0.0f, 3.0f },  // 9  TC2
    {  10.0f,  0.1857f,   0.130f,    0.52f,       0.2f, 0.8f,  0.8f, 0.0f, 3.5f },  // 10 TC2
}};

class Drive670Core
{
public:
    static constexpr int maxChannels = 2;
    // Operating reference: a sine at -18 dBFS RMS (0 VU in this model) drives
    // the grids to gridAtReference volts peak with Gin = 0 dB. Bias Vb0 = 1.
    static constexpr float referencePeak = 0.17782794f;   // -15 dBFS = -18 dBFS RMS sine
    static constexpr float defaultGridAtReference = 0.18f;   // output-stage grid at the reference (Calibrate --probe)
    static constexpr float cellGridAtReference = 0.06f;      // variable-mu grid at the reference: clean until GR deepens
    static constexpr float softness = 0.2f;                // remote-cutoff softening

    struct Meter { float gainReductionDb = 0.0f; float controlVoltage = 0.0f; float postGain = 1.0f; };

    void prepare(double processingRate) noexcept
    {
        fs = std::max(1.0, processingRate);
        paramCoefficient = coefficient(0.150);                    // regime morph time
        mixCoefficient = coefficient(0.030);                      // DRIVE 0 crossfade
        evenHighPass = static_cast<float>(std::exp(-2.0 * pi * 8.0 / fs));
        transformerLowPass = static_cast<float>(1.0 - std::exp(-2.0 * pi * 90.0 / fs));
        unityGainSlope = slope(bias0);
        stageQuiescent = tube(bias0);
        postGain.prepare(fs);
        reset();
    }

    void reset() noexcept
    {
        state = {};
        target = targetFor(drive);
        current = target;
        mix = drive > 0 ? 1.0f : 0.0f;
        postGain.setStep(drive);
        postGain.snap();
    }

    // DRIVE is a discrete 0..10 control. Changing it only moves targets; every
    // coefficient glides (150 ms) and detector states are never reset.
    void setDrive(int newDrive) noexcept { drive = std::clamp(newDrive, 0, 10); target = targetFor(drive); postGain.setStep(drive); }
    int getDrive() const noexcept { return drive; }

    void processFrame(float* x, int n) noexcept
    {
        n = std::clamp(n, 1, maxChannels);
        glide();
        const float vc = state.controlVoltage;
        const float g = std::exp(-vc);                                      // remote-cutoff gain law
        const float bias = std::max(minimumCellBias, bias0 - cellBiasShift * vc);
        const float cellNorm = 1.0f / slope(bias), cellQuiescent = tube(bias);
        const float gin = current.inputGain;
        const float toCell = gin * cellPerFullScale;
        const float toStage = gin * gridPerFullScale;
        float detector = 0.0f;
        std::array<float, maxChannels> wet {};
        for (int c = 0; c < n; ++c)
        {
            auto& ch = state.channel[static_cast<size_t>(c)];
            // variable-mu cell (small imbalance of matched 6386 halves)
            const float u = x[c] * toCell;
            const float cu = tube(bias + u), cd = tube(bias - u);
            const float cellEvenRaw = cellImbalance * (cu + cd - 2.0f * cellQuiescent);
            const float cellEven = highPass(cellEvenRaw, ch.cellEvenIn, ch.cellEvenOut);
            const float cell = g * (cu - cd + cellEven) * cellNorm;           // grid units of the cell, times g
            detector = std::max(detector, std::abs(feedforward ? u : cell));   // FEEDBACK: output of the gain cell
            // output amplifier (main colour), driven by the cell output
            const float v = cell * (gridPerFullScale / cellPerFullScale);
            const float up = tube(bias0 + v), down = tube(bias0 - v);
            const float evenRaw = current.imbalance * (up + down - 2.0f * stageQuiescent);
            const float even = highPass(evenRaw, ch.evenIn, ch.evenOut);       // 8 Hz DC/thump blocker
            const float stage = (up - down + even) / unityGainSlope;
            ch.lowBand += transformerLowPass * (stage - ch.lowBand);
            const float lf = ch.lowBand;
            const float core = current.transformer * (std::tanh(lf * 2.0f) * 0.5f - lf);  // LF core saturation
            wet[static_cast<size_t>(c)] = colourBypass ? g * x[c] : (stage + core) / toStage;
        }
        sidechain(detector);
        mix += mixCoefficient * ((drive > 0 ? 1.0f : 0.0f) - mix);
        for (int c = 0; c < n; ++c) x[c] += mix * (wet[static_cast<size_t>(c)] - x[c]);
        // R0.2 POST GAIN: last operation, after detection and every non-linear stage.
        const float gain = postGain.next();
        if (!postGainBypass)
            for (int c = 0; c < n; ++c) x[c] *= gain;
        meter.postGain = gain;
        meter.controlVoltage = vc;
        meter.gainReductionDb = -8.6858896f * vc;                          // 20*log10(exp(-vc))
    }

    const Meter& getMeter() const noexcept { return meter; }

    // Laboratory hooks (calibration tools only; the plugin never calls them).
    void setCalibrationThreshold(float t) noexcept { thresholdOverride = t; target = targetFor(drive); }
    void setOperatingPoint(float gridAtReference) noexcept { gridPerFullScale = gridAtReference / referencePeak; }
    void setFeedforwardForLab(bool ff) noexcept { feedforward = ff; }   // topology comparison only
    void setColourBypassForLab(bool b) noexcept { colourBypass = b; }   // isolates the dynamics in tests
    void setPostGainBypassForLab(bool b) noexcept { postGainBypass = b; } // exposes the pre-POST-GAIN signal in tests
    float getMix() const noexcept { return mix; }

private:
    static constexpr double pi = 3.14159265358979323846;
    static constexpr float bias0 = 1.0f;

    struct Params { float inputGain, threshold, imbalance, transformer, attack, release, slowRelease, charge, sidechainGain; };
    struct ChannelState { float evenIn = 0, evenOut = 0, cellEvenIn = 0, cellEvenOut = 0, lowBand = 0; };
    struct State { float controlVoltage = 0, fastStore = 0, slowStore = 0; std::array<ChannelState, maxChannels> channel {}; };

    float coefficient(double seconds) const noexcept { return seconds <= 0.0 ? 0.0f : static_cast<float>(1.0 - std::exp(-1.0 / (fs * seconds))); }
    Params targetFor(int d) const noexcept
    {
        // DRIVE 0 keeps DRIVE 1 dynamics running underneath (mix = 0) so that
        // leaving 0 starts from a settled detector instead of a reset.
        const auto& s = stepTable[static_cast<size_t>(std::max(d, 1) - 1)];
        return { std::pow(10.0f, s.inputSteps / 20.0f), thresholdOverride > 0.0f ? thresholdOverride : s.threshold, s.imbalance, s.transformer,
                 coefficient(s.attackMs * 0.001), coefficient(s.releaseS), coefficient(s.slowReleaseS),
                 s.chargeS > 0.0f ? coefficient(s.chargeS) : 0.0f, s.sidechainGain };
    }
    void glide() noexcept
    {
        const float k = paramCoefficient;
        current.inputGain += k * (target.inputGain - current.inputGain);
        current.threshold += k * (target.threshold - current.threshold);
        current.imbalance += k * (target.imbalance - current.imbalance);
        current.transformer += k * (target.transformer - current.transformer);
        current.attack += k * (target.attack - current.attack);
        current.release += k * (target.release - current.release);
        current.slowRelease += k * (target.slowRelease - current.slowRelease);
        current.charge += k * (target.charge - current.charge);
        current.sidechainGain += k * (target.sidechainGain - current.sidechainGain);
    }
    // Feedback RC network: the fast store charges with the attack constant and
    // releases towards the slow store (TC5 program-dependent memory).
    void sidechain(float detector) noexcept
    {
        const float demand = current.sidechainGain * std::max(0.0f, detector - current.threshold);
        auto& s = state;
        if (demand > s.fastStore) s.fastStore += current.attack * (demand - s.fastStore);
        else s.fastStore += current.release * (s.slowStore - s.fastStore);
        // The slow store is loaded only while the programme itself holds the
        // control voltage up (demand above the store), never by the fast
        // store's own release: isolated peaks recover with the fast constant,
        // sustained or repeated peaks progressively with the slow one.
        const float held = std::min(demand, s.fastStore);
        if (held > s.slowStore) s.slowStore += current.charge * (held - s.slowStore);
        s.slowStore -= current.slowRelease * s.slowStore;
        s.fastStore = std::max(s.fastStore, s.slowStore);
        s.controlVoltage = std::min(s.fastStore, maxControl);
    }
    float highPass(float in, float& lastIn, float& lastOut) const noexcept { const float out = in - lastIn + evenHighPass * lastOut; lastIn = in; lastOut = out; return out; }
    static float softplus(float v) noexcept { const float z = v / softness; return z > 20.0f ? v : softness * std::log1p(std::exp(z)); }
    // Half-stage plate current: Child-law I = softplus(v)^1.5 (soft remote
    // cutoff), limited by a soft plate-current ceiling M (load-line / supply
    // limit) so that a grid driven past cutoff saturates instead of expanding.
    static float tube(float v) noexcept { const float s = softplus(v); const float i = s * std::sqrt(s); return i / std::sqrt(1.0f + (i * i) / (plateLimit * plateLimit)); }
    static float slope(float v) noexcept
    {
        const float z = v / softness; const float sig = z > 20.0f ? 1.0f : 1.0f / (1.0f + std::exp(-z));
        const float s = softplus(v), i = s * std::sqrt(s), r = 1.0f + (i * i) / (plateLimit * plateLimit);
        return 2.0f * 1.5f * std::sqrt(s) * sig / (r * std::sqrt(r));
    }
    static constexpr float plateLimit = 9.0f;   // x quiescent current

    static constexpr float maxControl = 2.3f;           // 20 dB maximum gain reduction
    static constexpr float cellBiasShift = 0.25f, minimumCellBias = 0.45f, cellImbalance = 0.02f;
    static constexpr float cellPerFullScale = cellGridAtReference / referencePeak;
    double fs = 192000.0;
    float paramCoefficient = 0, mixCoefficient = 0, evenHighPass = 0, transformerLowPass = 0, unityGainSlope = 3.0f, stageQuiescent = 1.0f;
    int drive = 0;
    float thresholdOverride = -1.0f;
    bool feedforward = false, colourBypass = false, postGainBypass = false;
    Drive670PostGain postGain;
    float gridPerFullScale = defaultGridAtReference / referencePeak;
    Params current {}, target {};
    float mix = 0.0f;
    State state {};
    Meter meter {};
};
} // namespace vitaplug::drive670
