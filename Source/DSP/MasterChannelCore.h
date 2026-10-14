#pragma once

#include <array>
#include <algorithm>
#include <cmath>
#include <juce_dsp/juce_dsp.h>
#include "Drive670Core.h"
#include "DriveBankCalibration.h"

namespace vitaplug::harmohit::masterdsp
{
enum class ExciterMode : int { presence3k = 0, brilliance8k, pushAll };

struct HarmoSettings
{
    float harmonics = 0.0f;
    float exciter = 0.0f;
    float drive = 0.0f;
    float outputTrimDb = 0.0f;
    ExciterMode exciterMode = ExciterMode::presence3k;
};

// Fixed-storage core: it accepts host-owned channel pointers and never creates
// memory on the audio thread.  Branches produce residuals, which are summed
// with the unfiltered dry input; zero intensities are therefore transparent.
class MasterChannelCore
{
public:
    // Two 2x stages give the production 4x path. The stage-count constant is
    // deliberately separate so a future prepared 8x mode can add a third
    // stage without changing the branch processing architecture.
    static constexpr int oversamplingStages = 2;
    static constexpr int oversamplingFactor = 1 << oversamplingStages;
    static constexpr int futureOversamplingStages = 3;

    explicit MasterChannelCore(int stages = 2)
        : factor(1 << stages), oversampling(2, static_cast<size_t>(stages),
              juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true) {}

    void prepare(double newSampleRate, int maximumBlockSize, int newChannels)
    {
        baseSampleRate = std::max(1.0, newSampleRate);
        sampleRate = baseSampleRate * static_cast<double>(factor);
        drive670.prepare(sampleRate);   // also resets and snaps to the step set by setInitialDrive()
        channels = std::clamp(newChannels, 1, 2);
        oversampling.initProcessing(static_cast<size_t>(std::max(1, maximumBlockSize)));
        reset();
        for (auto& smoother : smoothers)
            smoother.reset();
        smoothers[3].reset(1.0f);
        exciterPresenceMix.reset(1.0f);
        exciterBrillianceMix.reset(0.0f);
    }

    void reset() noexcept
    {
        for (auto& channel : state) channel = {};
        oversampling.reset();
    }

    int getOversamplingFactor() const noexcept { return factor; }

    // DRIVE 670 integration. Call before prepare(): bank calibration (reciprocal
    // operating offset, see DriveBankCalibration.h) and the current DRIVE value,
    // so that prepare() starts snapped at that step exactly like the LAB plugin.
    void setDriveBank(drivecal::Bank bank) noexcept
    {
        driveOperatingGain = std::pow(10.0f, drivecal::offsetDb(bank) / 20.0f);
        driveInverseGain = 1.0f / driveOperatingGain;
        if (bank == drivecal::Bank::stereo) driveOperatingGain = driveInverseGain = 1.0f;   // exact LAB path
    }
    void setDriveOffsetDbForLab(float db) noexcept { driveOperatingGain = std::pow(10.0f, db / 20.0f); driveInverseGain = 1.0f / driveOperatingGain; }   // calibration tests only
    void setInitialDrive(float control01) noexcept { driveSteps.snap(control01); drive670.setDrive(driveSteps.current()); }
    const vitaplug::drive670::Drive670Core::Meter& getDriveMeter() const noexcept { return drive670.getMeter(); }   // tests only
    int getDriveStep() const noexcept { return driveSteps.current(); }
    int getLatencySamples() const noexcept { return static_cast<int>(std::lround(oversampling.getLatencyInSamples())); }

    void process(float* const* data, int requestedChannels, int samples, const HarmoSettings& settings) noexcept
    {
        if (data == nullptr || samples <= 0) return;
        const auto activeChannels = std::min({ requestedChannels, channels, 2 });
        if (activeChannels <= 0) return;

        // The oversampler must never receive non-finite values: sanitise before
        // its filters, then again after downsampling as an output safety net.
        for (int channel = 0; channel < activeChannels; ++channel)
            for (int sample = 0; sample < samples; ++sample)
                data[channel][sample] = finiteOrZero(data[channel][sample]);

        const auto harmonicTarget = shapeHarmonicsIntensity(settings.harmonics);
        const auto exciterTarget = shapeIntensity(settings.exciter, 0.75f);
        // DRIVE 670 R0.2: continuous parameter -> integer step (hysteresis);
        // the approved core glides its own coefficients and POST GAIN.
        drive670.setDrive(driveSteps.map(settings.drive));
        const auto trimTarget = dbToGain(std::clamp(settings.outputTrimDb, -24.0f, 12.0f));
        const auto ramp = std::max(1, static_cast<int>(sampleRate * 0.020));
        smoothers[0].setTarget(harmonicTarget, ramp);
        smoothers[1].setTarget(exciterTarget, ramp);
        smoothers[3].setTarget(trimTarget, ramp);
        const auto presenceTarget = settings.exciterMode == ExciterMode::brilliance8k ? 0.0f
                                  : settings.exciterMode == ExciterMode::pushAll ? 0.5f : 1.0f;
        const auto brillianceTarget = settings.exciterMode == ExciterMode::presence3k ? 0.0f
                                   : settings.exciterMode == ExciterMode::pushAll ? 0.5f : 1.0f;
        // Both spectral residuals are always calculated. Crossfading their
        // weights at the oversampled rate prevents a range/PUSH ALL switch from
        // creating a discontinuity, without changing Harmonics or Drive.
        exciterPresenceMix.setTarget(presenceTarget, ramp);
        exciterBrillianceMix.setTarget(brillianceTarget, ramp);

        std::array<const float*, 2> constData {};
        for (int channel = 0; channel < activeChannels; ++channel)
            constData[static_cast<size_t>(channel)] = data[channel];
        const juce::dsp::AudioBlock<const float> inputBlock { constData.data(), static_cast<size_t>(activeChannels), static_cast<size_t>(samples) };
        auto oversampled = oversampling.processSamplesUp(inputBlock).getSubsetChannelBlock(0, static_cast<size_t>(activeChannels));

        for (size_t sample = 0; sample < oversampled.getNumSamples(); ++sample)
        {
            const auto harmonicAmount = smoothers[0].next();
            const auto exciterAmount = smoothers[1].next();
            const auto trim = smoothers[3].next();
            const auto presenceMix = exciterPresenceMix.next();
            const auto brillianceMix = exciterBrillianceMix.next();
            std::array<float, 2> coloured {}, dry {}, drive {};
            for (int channel = 0; channel < activeChannels; ++channel)
            {
                auto& s = state[static_cast<size_t>(channel)];
                const float input = finiteOrZero(oversampled.getChannelPointer(static_cast<size_t>(channel))[sample]);
                const float harmonicInput = applyMixHarmonicsShelves(input, harmonicAmount, s);
                const float exciter = exciterResidual(harmonicInput, exciterAmount, presenceMix, brillianceMix, s);
                coloured[static_cast<size_t>(channel)] = harmonicInput + exciter;
                dry[static_cast<size_t>(channel)] = input;
                drive[static_cast<size_t>(channel)] = input * driveOperatingGain;
            }
            // DRIVE 670 R0.2 in parallel, as a differential contribution:
            //   out = dry + (HARMONICS/EXCITER residual) + (670(dry) - dry)
            //       = 670(dry) + (coloured - dry)
            // dry, colour and 670 belong to the same oversampled sample (the
            // core has no latency), so the subtraction is exactly aligned and
            // the dry signal is never counted twice. With HARMONICS/EXCITER off
            // (coloured == dry exactly) the bank outputs the LAB signal; with
            // DRIVE resting at 0 it outputs `coloured`, the unchanged MIX path.
            // One detector per bank, linked across its channels (L/R in STEREO).
            drive670.processFrame(drive.data(), activeChannels);
            const bool driveAtRest = drive670.getDrive() == 0 && drive670.getMix() == 0.0f && drive670.getMeter().postGain == 1.0f;
            for (int channel = 0; channel < activeChannels; ++channel)
            {
                const auto c = static_cast<size_t>(channel);
                const float out = driveAtRest ? coloured[c] : drive[c] * driveInverseGain + (coloured[c] - dry[c]);
                oversampled.getChannelPointer(c)[sample] = finiteOrZero(out * trim);
            }
        }
        juce::dsp::AudioBlock<float> outputBlock { data, static_cast<size_t>(activeChannels), static_cast<size_t>(samples) };
        oversampling.processSamplesDown(outputBlock);
        for (int channel = 0; channel < activeChannels; ++channel)
            for (int sample = 0; sample < samples; ++sample)
                data[channel][sample] = finiteOrZero(data[channel][sample]);
    }

private:
    struct OnePole { float z = 0.0f; };
    struct ChannelState
    {
        OnePole harmonicBodyLp, harmonicPresenceLp, harmonicAirLp;
        OnePole presenceLp, presenceHp, brillianceLp, brillianceHp;
    };
    struct Smoother
    {
        float current = 0.0f, target = 0.0f, step = 0.0f; int remaining = 0;
        void reset(float value = 0.0f) noexcept { current = target = value; step = 0.0f; remaining = 0; }
        void setTarget(float value, int samples) noexcept
        {
            if (std::abs(value - target) < 1.0e-7f) return;
            target = value; remaining = std::max(1, samples); step = (target - current) / static_cast<float>(remaining);
        }
        float next() noexcept
        {
            if (remaining > 0)
            {
                current += step;
                if (--remaining == 0) current = target;
            }
            return current;
        }
    };

    static float finiteOrZero(float value) noexcept { return std::isfinite(value) ? value : 0.0f; }
    static float dbToGain(float db) noexcept { return std::pow(10.0f, db / 20.0f); }
    float shapeIntensity(float control, float mixMaximum) const noexcept
    {
        const float x = std::clamp(finiteOrZero(control), 0.0f, 1.0f);
        // Progressive, monotonic law: the shallow initial slope gives useful
        // low/mid adjustment resolution, while the cubic rise reserves the
        // intentionally aggressive colour range for the upper control travel.
        // This is a mapping only: no output limiter or auto-normalisation is
        // applied anywhere after the artistic processing stages.
        const float curve = x * (0.38f + 0.62f * x * x);
        return curve * mixMaximum;
    }
    float shapeHarmonicsIntensity(float control) const noexcept
    {
        const float x = std::clamp(finiteOrZero(control), 0.0f, 1.0f);
        // Preserve MIX R5 normalization and shelving macro exactly.
        return x;
    }
    float lowpass(float x, float hz, OnePole& s) const noexcept
    {
        const float a = 1.0f - static_cast<float>(std::exp(-2.0 * 3.14159265358979323846 * hz / sampleRate));
        s.z += a * (x - s.z); return s.z;
    }
    float bandpass(float x, float low, float high, OnePole& lp, OnePole& hp) const noexcept
    {
        const float belowHigh = lowpass(x, high, lp);
        return belowHigh - lowpass(belowHigh, low, hp);
    }
    struct HarmonicShelfGains
    {
        float body = 1.0f;
        float presence = 1.0f;
        float air = 1.0f;
    };
    HarmonicShelfGains calibrateMixHarmonics(float macro) const noexcept
    {
        const float x = std::clamp(finiteOrZero(macro), 0.0f, 1.0f);
        // Independent concave laws move R4's useful upper-range colour into
        // the middle of the macro without collapsing the 0--25% adjustment
        // range. At 50%, the nominal 300/1800/12000 Hz gains closely match
        // R4 around 90%; each curve remains smooth and strictly increasing.
        const float bodyDb = 6.0f * x * (1.35f - 0.35f * x);
        const float presenceDb = 7.0f * x * (1.40f - 0.40f * x);
        const float airDb = 9.0f * x * (1.50f - 0.50f * x);
        return { dbToGain(bodyDb), dbToGain(presenceDb), dbToGain(airDb) };
    }
    float applyMixHarmonicsShelves(float x, float macro, ChannelState& channelState) const noexcept
    {
        const auto gains = calibrateMixHarmonics(macro);

        // Three inexpensive first-order shelves run continuously so their
        // states are already settled when the smoothed macro leaves zero.
        // With unity gains the algebra returns x exactly; no dry/wet
        // cancellation, saturation, DC blocker or output normalisation exists.
        const float bodyBand = lowpass(x, 300.0f, channelState.harmonicBodyLp);
        const float bodyShaped = x + (gains.body - 1.0f) * bodyBand;

        const float belowPresence = lowpass(bodyShaped, 1800.0f, channelState.harmonicPresenceLp);
        const float presenceBand = bodyShaped - belowPresence;
        const float presenceShaped = bodyShaped + (gains.presence - 1.0f) * presenceBand;

        const float belowAir = lowpass(presenceShaped, 12000.0f, channelState.harmonicAirLp);
        const float airBand = presenceShaped - belowAir;
        return presenceShaped + (gains.air - 1.0f) * airBand;
    }
    float exciterResidual(float x, float amount, float presenceMix, float brillianceMix, ChannelState& s) const noexcept
    {
        if (amount <= 0.0f) return 0.0f;
        const float presence = bandpass(x, 1800.0f, 5200.0f, s.presenceLp, s.presenceHp);
        const float brilliance = bandpass(x, 5500.0f, 14000.0f, s.brillianceLp, s.brillianceHp);
        const float p = std::tanh(presence * (1.0f + 5.0f * amount)) - presence;
        const float b = std::tanh(brilliance * (1.0f + 5.0f * amount)) - brilliance;
        return (p * presenceMix + b * brillianceMix) * amount;
    }
    vitaplug::drive670::Drive670Core drive670;
    drivecal::StepMapper driveSteps;
    float driveOperatingGain = 1.0f, driveInverseGain = 1.0f;
    int factor = 4;
    double baseSampleRate = 44100.0, sampleRate = 176400.0;
    int channels = 2;
    juce::dsp::Oversampling<float> oversampling;
    std::array<ChannelState, 2> state {};
    std::array<Smoother, 4> smoothers {};
    Smoother exciterPresenceMix, exciterBrillianceMix;
};
} // namespace vitaplug::harmohit::masterdsp
