// Public synthetic validation; NOT a substitute for private LAB equivalence.
#include "DSP/MasterEngine.h"
#include "DSP/Drive670PostGain.h"
#include <iostream>
#include <vector>
#include <stdexcept>
namespace md = vitaplug::harmohit::masterdsp;
using Signal = std::array<std::vector<float>, 2>;
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
Signal input(int frames = 16384) {
    Signal s;
    for (int c = 0; c < 2; ++c) { s[c].resize(static_cast<size_t>(frames));
        for (int i = 0; i < frames; ++i) s[c][static_cast<size_t>(i)] = static_cast<float>(
            0.21 * std::sin(i * (c == 0 ? 0.13 : 0.19)) + 0.04 * std::sin(i * 0.009)); }
    return s;
}
Signal render(const Signal& in, double rate, int block, const md::MasterSettings& settings) {
    juce::ScopedNoDenormals noDenormals;
    md::MasterEngine engine; engine.prepare(rate, block, settings);
    check(engine.getLatencySamples() == 6, "latency must remain six samples");
    Signal out = in;
    for (size_t i = 0; i < out[0].size(); i += static_cast<size_t>(block)) {
        float* channels[]{out[0].data() + i, out[1].data() + i};
        engine.process(channels, 2, static_cast<int>(std::min(static_cast<size_t>(block), out[0].size() - i)), settings);
    }
    for (const auto& channel : out) for (float value : channel) check(std::isfinite(value), "finite synthetic output");
    return out;
}
double difference(const Signal& a, const Signal& b, size_t start = 0) {
    double error = 0;
    for (int c = 0; c < 2; ++c) for (size_t i = start; i < a[c].size(); ++i)
        error = std::max(error, std::abs(static_cast<double>(a[c][i]) - b[c][i]));
    return error;
}
int main() {
    try {
        for (double rate : {44100.0, 48000.0, 96000.0}) {
            for (int step = 0; step <= 10; ++step) {
                // Two identical approved cores; POST GAIN is the sole test switch.
                vitaplug::drive670::Drive670Core a, b;
                a.setDrive(step); b.setDrive(step); a.prepare(rate * 4); b.prepare(rate * 4);
                b.setPostGainBypassForLab(true);
                const float gain = vitaplug::drive670::Drive670PostGain::gainForStep(step);
                check(std::abs(gain - std::pow(10.0, step / 20.0)) < 1e-6, "POST GAIN law +1 dB per step");
                double worst = 0;
                for (int i = 0; i < 12000; ++i) {
                    float left[]{0.2f * std::sin(i * 0.031f), 0.17f * std::cos(i * 0.023f)};
                    float right[]{left[0], left[1]}; a.processFrame(left, 2); b.processFrame(right, 2);
                    for (int c = 0; c < 2; ++c) worst = std::max(worst, std::abs(static_cast<double>(left[c] - right[c] * gain)));
                    check(a.getMeter().gainReductionDb == b.getMeter().gainReductionDb, "POST GAIN does not feed detector");
                }
                check(worst < 1e-6, "POST GAIN is final linear multiplier");
            }
            const auto x = input();
            const auto neutral = render(x, rate, 256, {});
            for (int step : {1, 3, 5, 7, 10}) {
                md::MasterSettings drive, colour, both;
                drive.banks[0].drive = step / 10.0f;
                colour.banks[0].harmonics = 0.6f; colour.banks[0].exciter = 0.5f;
                colour.banks[0].exciterMode = md::ExciterMode::pushAll;
                both = colour; both.banks[0].drive = drive.banks[0].drive;
                const auto d = render(x, rate, 256, drive), h = render(x, rate, 256, colour), all = render(x, rate, 256, both);
                auto expected = d;
                for (int c = 0; c < 2; ++c) for (size_t i = 0; i < x[c].size(); ++i) expected[c][i] += h[c][i] - neutral[c][i];
                const double parallelError = difference(all, expected);
                check(parallelError < 3e-6, "parallel DRIVE + unchanged H/E differential routing");
                check(difference(d, neutral) > 1e-4, "DRIVE changes synthetic waveform");
                std::cout << "parallel rate=" << rate << " DRIVE=" << step << " max_error=" << parallelError << '\n';
            }
            md::MasterSettings settings; settings.midSide = true;
            settings.banks[1].drive = 0.6f; settings.banks[2].drive = 0.2f;
            settings.banks[1].harmonics = 0.3f; settings.banks[2].exciter = 0.4f;
            const auto reference = render(x, rate, 256, settings);
            for (int block : {1, 17, 64, 137, 1024}) {
                const double error = difference(reference, render(x, rate, block, settings));
                check(error < 3e-6, "fixed-settings block-size invariance");
                std::cout << "block rate=" << rate << " size=" << block << " max_error=" << error << '\n';
            }
            auto changed = settings; changed.banks[1].drive = 1.0f;
            const auto alternate = render(x, rate, 256, changed);
            double sideError = 0, midChange = 0;
            for (size_t i = 4096; i < x[0].size(); ++i) {
                sideError = std::max(sideError, std::abs(static_cast<double>((reference[0][i] - reference[1][i]) - (alternate[0][i] - alternate[1][i]))));
                midChange = std::max(midChange, std::abs(static_cast<double>((reference[0][i] + reference[1][i]) - (alternate[0][i] + alternate[1][i]))));
            }
            check(sideError < 3e-6 && midChange > 1e-4, "independent MID DRIVE leaves SIDE unchanged");
            auto silence = x; for (auto& c : silence) std::fill(c.begin(), c.end(), 0);
            const auto silent = render(silence, rate, 137, settings);
            for (const auto& c : silent) for (float v : c) check(std::abs(v) < 1e-8, "silence remains silent");
            auto bad = x; bad[0][9] = std::numeric_limits<float>::quiet_NaN(); bad[1][11] = std::numeric_limits<float>::infinity();
            (void)render(bad, rate, 137, settings);
            settings.trimDb = -12;
            const auto trimmed = render(x, rate, 256, settings);
            double trimError = 0;
            for (int c = 0; c < 2; ++c) for (size_t i = 4096; i < x[c].size(); ++i)
                trimError = std::max(trimError, std::abs(static_cast<double>(trimmed[c][i] - reference[c][i] * std::pow(10.0f, -12.0f / 20.0f))));
            check(trimError < 2e-6, "global trim remains -12 dB after M/S reconstruction");
        }
        std::cout << "PASS public synthetic 670 checks. Private LAB equivalence NOT executed by this suite.\n";
    } catch (const std::exception& e) { std::cerr << "FAIL " << e.what() << '\n'; return 1; }
}
