// HARMOHIT MASTER x DRIVE 670 LAB R0.2 integration tests (CTest).
// A equivalence with the approved LAB engine, B parallel routing, C Stereo/M/S,
// S SIDE calibration measurements, D automation/state, E robustness/CPU/headroom.
#include "PluginProcessor.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <new>

static std::atomic<long> allocations { 0 };
static std::atomic<bool> counting { false };
void* operator new(std::size_t n) { if (counting) ++allocations; if (void* p = std::malloc(n ? n : 1)) return p; throw std::bad_alloc(); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }

void labRender(float* left, float* right, int frames, double rate, int block, const std::vector<int>& stepPerBlock);

namespace md = vitaplug::harmohit::masterdsp;
using Sig = std::array<std::vector<float>, 2>;
static int failures = 0;
static void check(bool ok, const std::string& what) { std::printf("%s %s\n", ok ? "PASS" : "FAIL", what.c_str()); if (!ok) ++failures; }
static double db(double x) { return 20 * std::log10(std::max(x, 1e-30)); }
static constexpr double pi = 3.14159265358979323846;

// ---------- material ----------
static Sig loadWav(const std::string& path, double rate, size_t start, size_t length48k) {
    juce::AudioFormatManager fm; fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> r(fm.createReaderFor(juce::File(path)));
    if (!r) { std::printf("FAIL cannot read %s\n", path.c_str()); std::exit(1); }
    const int n = static_cast<int>(std::min<juce::int64>(r->lengthInSamples - (juce::int64) start, (juce::int64) length48k));
    juce::AudioBuffer<float> b(2, n); r->read(&b, 0, n, (juce::int64) start, true, r->numChannels > 1);
    if (r->numChannels == 1) b.copyFrom(1, 0, b, 0, 0, n);
    Sig s; const double ratio = r->sampleRate / rate; const int out = static_cast<int>(n / ratio) - 8;
    for (int c = 0; c < 2; ++c) { s[(size_t) c].resize((size_t) out);
        if (std::abs(ratio - 1.0) < 1e-9) std::copy(b.getReadPointer(c), b.getReadPointer(c) + out, s[(size_t) c].begin());
        else { juce::WindowedSincInterpolator in; in.process(ratio, b.getReadPointer(c), s[(size_t) c].data(), out); } }
    return s;
}
static Sig loop(const Sig& s, double seconds, double rate) { Sig o; const size_t n = (size_t)(seconds * rate); for (int c = 0; c < 2; ++c) { o[(size_t) c].resize(n); for (size_t i = 0; i < n; ++i) o[(size_t) c][i] = s[(size_t) c][i % s[(size_t) c].size()]; } return o; }
static double rmsDb(const Sig& s, size_t a = 0, size_t b = 0) { if (!b) b = s[0].size(); double e = 0; for (auto& c : s) for (size_t i = a; i < b; ++i) e += (double) c[i] * c[i]; return 10 * std::log10(std::max(e / (2.0 * (double)(b - a)), 1e-30)); }
static double peakOf(const Sig& s, size_t a = 0) { double p = 0; for (auto& c : s) for (size_t i = a; i < c.size(); ++i) p = std::max(p, (double) std::abs(c[i])); return p; }
static Sig scaled(Sig s, double g) { for (auto& c : s) for (auto& v : c) v = (float)(v * g); return s; }
static Sig toRms(const Sig& s, double target) { return scaled(s, std::pow(10, (target - rmsDb(s)) / 20)); }
struct Material { std::string name; Sig sig; };
static std::vector<Material> materials(double rate, double seconds) {
    const std::string ev = std::string(HARMOHIT_PROJECT_DIR) + "/evidencias/", lab = std::string(DRIVE670_LAB_DIR) + "/reference/";
    const size_t bar = (size_t) std::lround(48000 * 60.0 / 112 * 4);
    std::vector<Material> m;
    m.push_back({ "MIX-LOGIC", toRms(loop(loadWav(ev + "harmohit_drive_stereo_30_50_80_100_por_compas.wav", rate, 0, bar), seconds, rate), -18) });
    m.push_back({ "MIX-AB", toRms(loop(loadWav(ev + "HARMOHIT_MASTER_DRIVE_AB.wav", rate, 0, 2 * bar), seconds, rate), -18) });
    m.push_back({ "DRUMS", toRms(loop(loadWav(lab + "drums_acoustic_break.wav", rate, 0, 48000 * 6), seconds, rate), -18) });
    Sig s; for (auto& c : s) { c.resize((size_t)(seconds * rate)); for (size_t i = 0; i < c.size(); ++i) c[i] = (float)(std::pow(10, (-12 + 3.0103) / 20) * std::sin(2 * pi * 1000 * (double) i / rate)); }
    m.push_back({ "SINE-1k-12", s });
    return m;
}

// ---------- renders ----------
using SettingsAt = std::function<md::MasterSettings(size_t block)>;
static md::MasterSettings stereoDrive(int step, float h = 0, float e = 0, md::ExciterMode mode = md::ExciterMode::presence3k) {
    md::MasterSettings s; s.banks[0] = { h, e, step / 10.0f, 0.0f, mode }; return s; }
struct Render { Sig out; std::vector<float> gr[3]; std::vector<int> steps; };
static Render master(const Sig& in, double rate, int block, const SettingsAt& at, const std::function<void(md::MasterEngine&)>& afterPrepare = {}) {
    juce::ScopedNoDenormals noDenormals;   // as in the plugin's processBlock
    auto e = std::make_unique<md::MasterEngine>(); e->prepare(rate, block, at(0)); if (afterPrepare) afterPrepare(*e);
    Render r { in, {}, {} };
    for (size_t o = 0, b = 0; o < in[0].size(); o += (size_t) block, ++b) { const int n = (int) std::min<size_t>((size_t) block, in[0].size() - o);
        float* p[] { r.out[0].data() + o, r.out[1].data() + o }; e->process(p, 2, n, at(b));
        for (int k = 0; k < 3; ++k) r.gr[k].push_back(e->getBank(k).getDriveMeter().gainReductionDb); r.steps.push_back(e->getBank(0).getDriveStep()); }
    return r;
}
static Sig lab(const Sig& in, double rate, int block, const std::function<int(size_t)>& stepAt) {
    juce::ScopedNoDenormals noDenormals;   // as in the LAB plugin's processBlock
    Sig o = in; std::vector<int> steps; for (size_t b = 0; b * (size_t) block < in[0].size(); ++b) steps.push_back(stepAt(b));
    labRender(o[0].data(), o[1].data(), (int) in[0].size(), rate, block, steps); return o;
}
static double maxDiff(const Sig& a, const Sig& b) { double m = 0; for (int c = 0; c < 2; ++c) for (size_t i = 0; i < a[(size_t) c].size(); ++i) m = std::max(m, (double) std::abs(a[(size_t) c][i] - b[(size_t) c][i])); return m; }
static double residualDb(const Sig& a, const Sig& ref, size_t from = 0) { double e = 0, r = 0; for (int c = 0; c < 2; ++c) for (size_t i = from; i < a[(size_t) c].size(); ++i) { const double d = a[(size_t) c][i] - ref[(size_t) c][i]; e += d * d; r += (double) ref[(size_t) c][i] * ref[(size_t) c][i]; } return 10 * std::log10(std::max(e, 1e-300) / std::max(r, 1e-300)); }
static double thdPercent(const std::vector<float>& y, double rate, size_t start) {
    const size_t n = (size_t)(rate / 10); auto bin = [&](double f) { std::complex<double> s {}; for (size_t i = 0; i < n; ++i) s += (double) y[start + i] * std::polar(1.0, -2 * pi * f * (double) i / rate); return std::abs(s); };
    const double f1 = bin(1000); double h = 0; for (int k = 2; k <= 9; ++k) { const double v = bin(1000.0 * k); h += v * v; } return 100 * std::sqrt(h) / f1; }
static double meanGr(const std::vector<float>& g, size_t fromBlock) { double s = 0; size_t n = 0; for (size_t b = fromBlock; b < g.size(); ++b) { s += -g[b]; ++n; } return n ? s / (double) n : 0; }
static double d2Max(const Sig& z, size_t a, size_t b) { double m = 0; for (size_t i = std::max<size_t>(a, 2); i < b; ++i) for (auto& c : z) m = std::max(m, (double) std::abs(c[i] - 2 * c[i - 1] + c[i - 2])); return m; }
static Sig component(const Sig& lr, bool side) { Sig o; o[0].resize(lr[0].size()); for (size_t i = 0; i < lr[0].size(); ++i) o[0][i] = (side ? lr[0][i] - lr[1][i] : lr[0][i] + lr[1][i]) * 0.70710678f; o[1] = o[0]; return o; }

int main() {
    juce::ScopedJuceInitialiser_GUI gui;
    const int block = 256;
    // ================= A. LAB / MASTER equivalence (STEREO, HARMONICS/EXCITER off, trim 0 dB)
    for (double rate : { 44100.0, 48000.0, 96000.0 }) {
        std::printf("-- A @ %.0f Hz\n", rate);
        const auto mats = materials(rate, 12.0); const size_t settle = (size_t)(4 * rate);
        for (auto& m : mats) {
            double worst = 0;
            for (int step : { 0, 1, 3, 5, 7, 10 }) {
                auto ms = master(m.sig, rate, block, [&](size_t) { return stereoDrive(step); });
                auto ls = lab(m.sig, rate, block, [&](size_t) { return step; });
                const double d = maxDiff(ms.out, ls); worst = std::max(worst, d);
                std::printf("MEASURE A %-10s %5.0f D%2d | MASTER rms %7.2f pk %+6.2f GR %5.2f | LAB rms %7.2f pk %+6.2f | max|diff| %.3g%s\n", m.name.c_str(), rate, step,
                    rmsDb(ms.out, settle), db(peakOf(ms.out, settle)), meanGr(ms.gr[0], settle / block), rmsDb(ls, settle), db(peakOf(ls, settle)), d,
                    m.name == "SINE-1k-12" ? (" | THD MASTER " + std::to_string(thdPercent(ms.out[0], rate, settle)) + "% LAB " + std::to_string(thdPercent(ls[0], rate, settle)) + "%").c_str() : "");
            }
            check(worst == 0.0, "A: " + m.name + " @" + std::to_string((int) rate) + " MASTER STEREO == LAB R0.2 bit-exact at DRIVE 0/1/3/5/7/10");
        }
        // automation crossing TC5->TC3->TC2 and 0->10->0, same block schedule on both
        const int seq[] { 0, 10, 0, 3, 4, 3, 7, 8, 7, 5, 1, 9 }; const size_t seg = (size_t)(0.5 * rate) / block;
        auto stepAt = [&](size_t b) { return seq[std::min<size_t>(11, b / seg)]; };
        auto ma = master(mats[0].sig, rate, block, [&](size_t b) { return stereoDrive(stepAt(b)); });
        auto la = lab(mats[0].sig, rate, block, stepAt);
        check(maxDiff(ma.out, la) == 0.0, "A: automated DRIVE 0>10>0>3>4>3>7>8>7>5>1>9: MASTER == LAB bit-exact @" + std::to_string((int) rate));
    }
    { // through the plugin (APVTS stereo.drive 0..100) at 48 kHz
        const double rate = 48000; const auto mats = materials(rate, 8.0);
        for (int step : { 3, 10 }) {
            HarmoHitAudioProcessor p; auto* prm = p.parameters.getParameter("stereo.drive"); prm->setValueNotifyingHost(prm->convertTo0to1(step * 10.0f));
            p.prepareToPlay(rate, block); Sig y = mats[0].sig; juce::MidiBuffer midi;
            for (size_t o = 0; o < y[0].size(); o += block) { const int n = (int) std::min<size_t>(block, y[0].size() - o); float* ch[] { y[0].data() + o, y[1].data() + o }; juce::AudioBuffer<float> buf(ch, 2, n); p.processBlock(buf, midi); }
            check(maxDiff(y, lab(mats[0].sig, rate, block, [&](size_t) { return step; })) == 0.0, "A: plugin processor (APVTS stereo.drive=" + std::to_string(step * 10) + ") == LAB R0.2 bit-exact");
        }
    }

    // ================= B. Parallel routing (differential contribution)
    for (double rate : { 44100.0, 48000.0 }) {
        const auto mats = materials(rate, 8.0); const auto& x = mats[0].sig;
        const auto neutral = master(x, rate, block, [](size_t) { return stereoDrive(0); }).out;
        struct HE { float h, e; md::ExciterMode m; const char* n; };
        for (auto he : { HE { 0.5f, 0, md::ExciterMode::presence3k, "H50" }, HE { 0, 0.6f, md::ExciterMode::brilliance8k, "E60-8k" }, HE { 0.7f, 0.5f, md::ExciterMode::pushAll, "H70+E50-PUSH" } })
            for (int step : { 3, 10 }) {
                auto withAll = master(x, rate, block, [&](size_t) { return stereoDrive(step, he.h, he.e, he.m); }).out;
                auto colourOnly = master(x, rate, block, [&](size_t) { return stereoDrive(0, he.h, he.e, he.m); }).out;
                auto labD = lab(x, rate, block, [&](size_t) { return step; });
                Sig expect = labD; for (int c = 0; c < 2; ++c) for (size_t i = 0; i < x[0].size(); ++i) expect[(size_t) c][i] += colourOnly[(size_t) c][i] - neutral[(size_t) c][i];
                const double r = residualDb(withAll, expect);
                std::printf("MEASURE B %5.0f %-13s D%2d: out - [LAB(D) + (H/E contribution)] residual %.1f dB, max|diff| %.3g\n", rate, he.n, step, r, maxDiff(withAll, expect));
                check(r < -100.0, std::string("B: ") + he.n + " + DRIVE " + std::to_string(step) + " = LAB(DRIVE) + unchanged H/E contribution (no double dry, aligned) @" + std::to_string((int) rate));
            }
    }

    // ================= C. Stereo / M/S
    {
        const double rate = 48000; const auto mats = materials(rate, 10.0); const Sig& mix = mats[0].sig; const size_t settle = (size_t)(4 * rate);
        Sig mono = mix; for (size_t i = 0; i < mono[0].size(); ++i) mono[0][i] = mono[1][i] = 0.5f * (mix[0][i] + mix[1][i]);
        for (int step : { 3, 5, 10 }) {
            md::MasterSettings ms; ms.midSide = true; ms.banks[1].drive = step / 10.0f;
            auto msOut = master(mono, rate, block, [&](size_t) { return ms; }).out; auto stOut = master(mono, rate, block, [&](size_t) { return stereoDrive(step); }).out;
            const double r = residualDb(msOut, stOut, settle); std::printf("MEASURE C mono programme, M/S MID D%d vs STEREO D%d: residual %.1f dB\n", step, step, r);
            check(r < -80.0, "C: mono programme in M/S (MID " + std::to_string(step) + ") matches STEREO mode (MID -3.01 dB offset)");
        }
        { md::MasterSettings ms; ms.midSide = true; ms.banks[2].drive = 1.0f; auto y = master(mono, rate, block, [&](size_t) { return ms; }).out; bool same = true; for (size_t i = 0; i < y[0].size(); ++i) same = same && y[0][i] == y[1][i];
          check(same, "C: mono programme with SIDE DRIVE 10 stays exactly mono"); }
        { md::MasterSettings a; a.midSide = true; a.banks[1].drive = 0.7f; md::MasterSettings z; z.midSide = true;
          auto ya = master(mix, rate, block, [&](size_t) { return a; }).out, yz = master(mix, rate, block, [&](size_t) { return z; }).out;
          const double r = residualDb(component(ya, true), component(yz, true)); std::printf("MEASURE C Side component with MID D7 vs MID D0: %.1f dB\n", r);
          check(r < -100.0, "C: MID DRIVE does not affect the SIDE component");
          md::MasterSettings b; b.midSide = true; b.banks[2].drive = 0.7f; auto yb = master(mix, rate, block, [&](size_t) { return b; }).out;
          const double r2 = residualDb(component(yb, false), component(yz, false)); std::printf("MEASURE C Mid component with SIDE D7 vs SIDE D0: %.1f dB\n", r2);
          check(r2 < -100.0, "C: SIDE DRIVE does not affect the MID component");
          md::MasterSettings both; both.midSide = true; both.banks[1].drive = 0.5f; both.banks[2].drive = 0.5f; auto rb = master(mix, rate, block, [&](size_t) { return both; });
          std::printf("MEASURE C M/S MID D5 + SIDE D5 simultaneously: MID GR %.2f dB, SIDE GR %.2f dB, STEREO bank GR %.2f dB (idle)\n", meanGr(rb.gr[1], settle / block), meanGr(rb.gr[2], settle / block), meanGr(rb.gr[0], settle / block));
          check(meanGr(rb.gr[1], settle / block) > 0.1 && meanGr(rb.gr[2], settle / block) > 0.1, "C: MID and SIDE are processed simultaneously, each with its own detector");
          auto st0 = master(mix, rate, block, [](size_t) { return stereoDrive(0); }).out;
          const double rec = residualDb(yz, st0); std::printf("MEASURE C M/S all DRIVE 0 vs STEREO neutral: %.1f dB\n", rec); check(rec < -100.0, "C: M/S reconstruction without DRIVE equals STEREO neutral"); }
    }

    // ================= S. SIDE calibration measurements (report)
    {
        const double rate = 48000; const auto mats = materials(rate, 14.0); const size_t settle = (size_t)(6 * rate);
        for (int mi : { 0, 1 }) { const Sig& mix = mats[(size_t) mi].sig; const Sig sc = component(mix, true), mc = component(mix, false);
            std::printf("MEASURE S %s at -18 dBFS RMS L/R: Mid %.1f dBFS RMS, Side %.1f dBFS RMS\n", mats[(size_t) mi].name.c_str(), rmsDb(mc), rmsDb(sc));
            for (double sideTrim : { -10.0, 0.0, 10.0 }) { Sig in = mix; for (size_t i = 0; i < in[0].size(); ++i) { const float m = (mix[0][i] + mix[1][i]) * .70710678f, s = (mix[0][i] - mix[1][i]) * .70710678f * (float) std::pow(10, sideTrim / 20); in[0][i] = (m + s) * .70710678f; in[1][i] = (m - s) * .70710678f; }
                md::MasterSettings z; z.midSide = true; auto ref = master(in, rate, block, [&](size_t) { return z; }).out; const Sig sRef = component(ref, true);
                for (float offset : { 0.0f, 10.0f, 15.0f, 20.0f }) { std::printf("MEASURE S   side %+5.1f dB (Side %.1f dBFS RMS) offset %+4.0f dB:", sideTrim, rmsDb(component(in, true)), offset);
                    for (int step : { 3, 5, 7, 10 }) { md::MasterSettings s = z; s.banks[2].drive = step / 10.0f;
                        auto r = master(in, rate, block, [&](size_t) { return s; }, [offset](md::MasterEngine& e) { e.getBankForLab(2).setDriveOffsetDbForLab(offset); });
                        const Sig so = component(r.out, true); const double crest = db(peakOf(so, settle)) - rmsDb(so, settle), crest0 = db(peakOf(sRef, settle)) - rmsDb(sRef, settle);
                        std::printf("  D%-2d GR %4.2f lvl %+5.2f dCrest %+5.2f", step, meanGr(r.gr[2], settle / block), rmsDb(so, settle) - rmsDb(sRef, settle), crest - crest0); }
                    std::printf("\n"); } } }
        // tails, silence and noise with the shipped SIDE offset
        Sig x = mats[0].sig; const size_t n = x[0].size(); for (auto& c : x) c.resize(n + (size_t)(6 * rate), 0.0f);
        md::MasterSettings s; s.midSide = true; s.banks[1].drive = 0.7f; s.banks[2].drive = 1.0f; auto r = master(x, rate, block, [&](size_t) { return s; });
        const Sig tail = [&] { Sig t; for (int c = 0; c < 2; ++c) t[(size_t) c].assign(r.out[(size_t) c].begin() + (long)(n + (size_t)(2 * rate)), r.out[(size_t) c].end()); return t; }();
        std::printf("MEASURE S tail after programme (M/S, MID D7, SIDE D10): peak 2-6 s after end %.1f dBFS\n", db(peakOf(tail)));
        check(peakOf(tail) == 0.0, "S: digital silence after the programme stays silent with the SIDE offset");
        Sig noise; uint32_t seed = 9; for (auto& c : noise) c.resize((size_t)(8 * rate)); for (size_t i = 0; i < noise[0].size(); ++i) { seed = seed * 1664525u + 1013904223u; const float w = (float)((seed / 4294967296.0 * 2 - 1) * std::pow(10, -66.0 / 20)); const bool hit = (i % (size_t) rate) < (size_t)(0.03 * rate); const float t = hit ? (float)(0.3 * std::sin(2 * pi * 2000 * (double) i / rate)) : 0.0f; noise[0][i] = w + t; noise[1][i] = -w - t; }
        md::MasterSettings sn; sn.midSide = true; sn.banks[2].drive = 1.0f; auto rn = master(noise, rate, block, [&](size_t) { return sn; }); md::MasterSettings s0; s0.midSide = true; auto r0 = master(noise, rate, block, [&](size_t) { return s0; });
        double lo = 1e9, hi = -1e9; for (size_t sec = 2; sec < 7; ++sec) { const size_t a = sec * (size_t) rate + (size_t)(0.2 * rate), b = sec * (size_t) rate + (size_t)(0.95 * rate); Sig w1, w0; for (int c = 0; c < 2; ++c) { w1[(size_t) c].assign(rn.out[(size_t) c].begin() + (long) a, rn.out[(size_t) c].begin() + (long) b); w0[(size_t) c].assign(r0.out[(size_t) c].begin() + (long) a, r0.out[(size_t) c].begin() + (long) b); }
            const double g = rmsDb(w1) - rmsDb(w0); lo = std::min(lo, g); hi = std::max(hi, g); }
        std::printf("MEASURE S Side noise floor (-66 dBFS) between hard Side bursts, SIDE D10: level vs D0 from %+.2f to %+.2f dB (POST GAIN +10 dB)\n", lo, hi);
        check(hi - lo < 1.0 && hi < 10.5, "S: no noise pumping or noise boost beyond POST GAIN on the SIDE bank");
    }

    // ================= D. Automation and state
    {
        const double rate = 48000; const auto mats = materials(rate, 10.0); const Sig& mix = mats[0].sig;
        { const size_t nb = mix[0].size() / block; auto r = master(mix, rate, block, [&](size_t b) { md::MasterSettings s; s.banks[0].drive = std::min(1.0f, (float) b / (float)(nb * 0.8)); return s; });
          int changes = 0; bool monotonic = true; for (size_t b = 1; b < r.steps.size(); ++b) { if (r.steps[b] != r.steps[b - 1]) { ++changes; monotonic = monotonic && r.steps[b] == r.steps[b - 1] + 1; } }
          check(changes == 10 && monotonic && r.steps.back() == 10, "D: legacy continuous ramp 0..100 maps to steps 0..10 once each, monotonic (" + std::to_string(changes) + " changes)"); }
        for (float centre : { 0.25f, 0.35f, 0.75f }) { auto r = master(mix, rate, block, [&](size_t b) { md::MasterSettings s; s.banks[0].drive = centre + ((b % 2) ? 0.003f : -0.003f); return s; });
          int changes = 0; for (size_t b = 1; b < r.steps.size(); ++b) changes += r.steps[b] != r.steps[b - 1];
          check(changes == 0, "D: automation jitter of +-0.3 around " + std::to_string(centre * 100).substr(0, 4) + " never toggles the step"); }
        const size_t seg = (size_t)(1.0 * rate) / block; const int seq[] { 0, 10, 0, 3, 4, 3, 7, 8, 7 };
        auto stepAt = [&](size_t b) { return seq[std::min<size_t>(8, b / seg)]; };
        auto ya = master(mix, rate, block, [&](size_t b) { return stereoDrive(stepAt(b)); }).out; double worst = 0;
        for (int k = 1; k < 9; ++k) { const size_t at = (size_t) k * seg * block, a = at - (size_t)(0.02 * rate), b = at + (size_t)(0.02 * rate);
            // Reference = the larger of DRIVE held at the source and at the destination step:
            // a ramp between two levels is not a click; a discontinuity would exceed both.
            auto yh = master(mix, rate, block, [&](size_t) { return stereoDrive(seq[k]); }).out; auto ys = master(mix, rate, block, [&](size_t) { return stereoDrive(seq[k - 1]); }).out;
            const double ratio = d2Max(ya, a, b) / std::max(std::max(d2Max(yh, a, b), d2Max(ys, a, b)), 1e-9); worst = std::max(worst, ratio);
            std::printf("MEASURE D %2d -> %2d: max|2nd diff| automated / max(held source, held destination) %.3f\n", seq[k - 1], seq[k], ratio); }
        check(worst < 1.5, "D: DRIVE 0>10>0, TC5>TC3 (3>4) and TC3>TC2 (7>8) changes are click-free on music");
        md::MasterSettings st; st.banks = { md::HarmoSettings { 0.3f, 0.2f, 0.5f }, md::HarmoSettings { 0.3f, 0.2f, 0.5f }, md::HarmoSettings { 0.1f, 0.1f, 0.5f } };
        auto modeAt = [&](size_t b) { md::MasterSettings s = st; s.midSide = (b / seg) % 2 == 1; return s; };
        auto ym = master(mix, rate, block, modeAt).out; md::MasterSettings sm = st; sm.midSide = true; auto yms = master(mix, rate, block, [&](size_t) { return sm; }).out; auto yss = master(mix, rate, block, [&](size_t) { return st; }).out;
        double wm = 0; for (int k = 1; k < 8; ++k) { const size_t at = (size_t) k * seg * block, a = at - (size_t)(0.02 * rate), b = at + (size_t)(0.03 * rate); wm = std::max(wm, d2Max(ym, a, b) / std::max(std::max(d2Max(yms, a, b), d2Max(yss, a, b)), 1e-9)); }
        std::printf("MEASURE D STEREO<->M/S switching during playback: max|2nd diff| switched/held %.3f\n", wm);
        check(wm < 1.5, "D: STEREO <-> M/S switching during playback is click-free (banks stay warm)");
        { HarmoHitAudioProcessor a; for (auto [id, v] : { std::pair<const char*, float> { "stereo.drive", 70.0f }, { "mid.drive", 40.0f }, { "side.drive", 90.0f }, { "processingMode", 1.0f } }) { auto* prm = a.parameters.getParameter(id); prm->setValueNotifyingHost(prm->convertTo0to1(v)); }
          a.prepareToPlay(rate, block); juce::MemoryBlock state; a.getStateInformation(state);
          HarmoHitAudioProcessor b; b.setStateInformation(state.getData(), (int) state.getSize()); b.prepareToPlay(rate, block);
          Sig y = mix; juce::MidiBuffer midi; bool finite = true; for (size_t o = 0; o + block <= y[0].size(); o += block) { float* ch[] { y[0].data() + o, y[1].data() + o }; juce::AudioBuffer<float> buf(ch, 2, block); b.processBlock(buf, midi); }
          for (auto& c : y) for (float v : c) finite = finite && std::isfinite(v);
          check(finite && b.getEngine().getBank(0).getDriveStep() == 7 && b.getEngine().getBank(1).getDriveStep() == 4 && b.getEngine().getBank(2).getDriveStep() == 9, "D: session restore brings back the three DRIVE banks (7/4/9) and runs cleanly"); }
    }

    // ================= E. Robustness, CPU, headroom
    for (double rate : { 44100.0, 48000.0, 96000.0 }) {
        const auto mats = materials(rate, 6.0); const Sig& mix = mats[0].sig; md::MasterSettings s; s.midSide = true; s.banks = { md::HarmoSettings { 0.4f, 0.3f, 0.6f }, md::HarmoSettings { 0.4f, 0.3f, 0.6f }, md::HarmoSettings { 0.2f, 0.2f, 0.8f } };
        auto ref = master(mix, rate, 256, [&](size_t) { return s; }).out; bool same = true;
        for (int blk : { 1, 37, 4096 }) same = same && maxDiff(master(mix, rate, blk, [&](size_t) { return s; }).out, ref) == 0.0;
        check(same, "E: block-size independent (1/37/4096 vs 256), M/S with H/E/DRIVE @" + std::to_string((int) rate));
        bool silent = true; Sig z; for (auto& c : z) c.assign((size_t) rate, 0.0f);
        for (int step : { 0, 3, 7, 10 }) for (bool msMode : { false, true }) { md::MasterSettings q; q.midSide = msMode; for (auto& b : q.banks) b = { 0.5f, 0.5f, step / 10.0f }; auto y = master(z, rate, 256, [&](size_t) { return q; }).out; silent = silent && peakOf(y) == 0.0; }
        check(silent, "E: digital silence in -> digital silence out (all modes, H/E on, DRIVE 0/3/7/10) @" + std::to_string((int) rate));
        Sig bad = mix; bad[0][1000] = NAN; bad[1][2000] = INFINITY; bad[0][3000] = -INFINITY; auto yb = master(bad, rate, 256, [&](size_t) { return s; }).out; bool finite = true; for (auto& c : yb) for (float v : c) finite = finite && std::isfinite(v);
        check(finite, "E: NaN/Inf input -> finite output @" + std::to_string((int) rate));
        { md::MasterEngine e; e.prepare(rate, 512, s); std::vector<float> l(512), r(512); allocations = 0; counting = true;
          for (int b = 0; b < 400; ++b) { for (int i = 0; i < 512; ++i) l[(size_t) i] = r[(size_t) i] = 0.3f * std::sin(0.01f * (float)(b * 512 + i)); md::MasterSettings q = s; q.midSide = (b / 50) % 2; q.banks[0].drive = (float)((b / 20) % 11) / 10.0f; q.banks[2].drive = (float)((b / 30) % 11) / 10.0f; float* p[] { l.data(), r.data() }; e.process(p, 2, 512, q); }
          counting = false; check(allocations == 0, "E: no heap allocation on the audio path with DRIVE/mode automation @" + std::to_string((int) rate));
          check(e.getLatencySamples() == 6, "E: reported latency still 6 samples @" + std::to_string((int) rate)); }
    }
    { // CPU and headroom at 48 kHz
        const double rate = 48000; const auto mats = materials(rate, 6.0);
        for (int step : { 0, 5 }) { md::MasterSettings q; q.midSide = false; for (auto& b : q.banks) b = { 0.5f, 0.5f, step / 10.0f }; md::MasterEngine e; e.prepare(rate, 256, q); std::vector<float> l(256), r(256); double best = 1e9;
            for (int rep = 0; rep < 3; ++rep) { auto t0 = std::chrono::steady_clock::now(); for (int b = 0; b < (int)(60 * rate / 256); ++b) { for (int i = 0; i < 256; ++i) { l[(size_t) i] = 0.2f * std::sin(0.031f * (float)(b * 256 + i)); r[(size_t) i] = 0.8f * l[(size_t) i]; } float* p[] { l.data(), r.data() }; e.process(p, 2, 256, q); }
                best = std::min(best, std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count()); }
            std::printf("MEASURE E CPU 60 s stereo @48 kHz, H/E 50, DRIVE %d on all banks (3 banks always run): %.3f s = %.2f %% of one core\n", step, best, 100 * best / 60); }
        const Sig hot = toRms(mats[0].sig, -10); auto y0 = master(hot, rate, 256, [](size_t) { return stereoDrive(10); }).out;
        auto y12 = master(hot, rate, 256, [](size_t) { md::MasterSettings s = stereoDrive(10); s.trimDb = -12.0f; return s; }).out;
        const double pk = peakOf(y0, (size_t) rate), pk12 = peakOf(y12, (size_t) rate);
        std::printf("MEASURE E headroom: -10 dBFS RMS mix at DRIVE 10 peaks %+.2f dBFS before OUTPUT TRIM; with trim -12 dB %+.2f dBFS (difference %.3f dB)\n", db(pk), db(pk12), db(pk) - db(pk12));
        check(pk > 1.0 && std::abs(db(pk) - db(pk12) - 12.0) < 0.01, "E: values above 0 dBFS survive unclipped and OUTPUT TRIM -12 dB lowers them exactly");
    }
    std::printf("%s (%d failures)\n", failures ? "FAILED" : "ALL PASSED", failures);
    return failures ? 1 : 0;
}
