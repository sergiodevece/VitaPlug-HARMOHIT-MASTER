// R12 independent DRIVE audit harness. Compiles the UNMODIFIED MIX HarmoCore.h
// and MASTER MasterEngine.h/MasterChannelCore.h, plus (a) an exact replica of
// MASTER driveResidual with switchable transient protection and (b) the
// audit-only prototype in ProtoDrive.h. Usage: r12_drive_audit <wav> > out.txt
#include "HarmoCore.h"          // MIX  (vitaplug::harmohit::dsp)
#include "MasterEngineR1.h"     // MASTER R1 frozen (vitaplug::harmohit::masterdsp_r1)
#include "ProtoDrive.h"
#include <cstdio>
#include <cstdlib>
#include <complex>
#include <vector>
#include <string>
#include <memory>
#include <functional>
using namespace std;
namespace mix = vitaplug::harmohit::dsp;
namespace mst = vitaplug::harmohit::masterdsp_r1;
constexpr double FS = 48000.0; constexpr int BLOCK = 256; constexpr double PI = 3.14159265358979323846;
using Sig = array<vector<float>, 2>;
static double db(double x) { return 20 * log10(max(x, 1e-15)); }

// ---------- WAV (24-bit PCM) ----------
Sig loadWav(const char* path) {
    FILE* f = fopen(path, "rb"); if (!f) { perror(path); exit(1); }
    vector<uint8_t> b; uint8_t t[65536]; size_t n; while ((n = fread(t, 1, sizeof t, f)) > 0) b.insert(b.end(), t, t + n); fclose(f);
    Sig s; size_t i = 12; int ch = 2, bits = 0;
    while (i + 8 <= b.size()) { string id((char*)&b[i], 4); uint32_t sz; memcpy(&sz, &b[i+4], 4);
        if (id == "fmt ") { ch = b[i+10] | b[i+11] << 8; bits = b[i+22] | b[i+23] << 8; }
        if (id == "data") for (size_t k = 0; k < sz / (size_t)(ch * 3); ++k) for (int c = 0; c < 2; ++c) {
            const uint8_t* q = &b[i + 8 + (k * ch + c) * 3]; int32_t v = q[0] | q[1] << 8 | q[2] << 16; if (v & 0x800000) v |= ~0xFFFFFF;
            s[c].push_back((float)(v / 8388608.0)); }
        i += 8 + sz + (sz & 1); }
    if (bits != 24) { fprintf(stderr, "need 24-bit\n"); exit(1); }
    return s;
}

// ---------- processors ----------
struct Proc { virtual ~Proc() = default; virtual void run(Sig& io) = 0; };
template <class F> void blocks(Sig& io, F&& f) {
    const int N = (int) io[0].size();
    for (int o = 0; o < N; o += BLOCK) { int n = min(BLOCK, N - o); float* p[2] { io[0].data() + o, io[1].data() + o }; f(p, n); }
}
struct MixProc : Proc { float d; MixProc(float d) : d(d) {}
    void run(Sig& io) override { mix::HarmoCore c(mix::ProductVariant::mix); c.prepare(FS, BLOCK, 2);
        mix::HarmoSettings s; s.drive = d; blocks(io, [&](float** p, int n) { c.process(p, 2, n, s); }); } };
// mode 0 STEREO(bank0), 1 M/S drive on Mid (bank1), 2 M/S drive on Side (bank2)
struct MasterProc : Proc { float d; int mode; bool bypass; MasterProc(float d, int m = 0, bool b = false) : d(d), mode(m), bypass(b) {}
    void run(Sig& io) override { auto e = make_unique<mst::MasterEngine>(); e->prepare(FS, BLOCK);
        mst::MasterSettings s; s.midSide = mode != 0; s.banks[(size_t) mode].drive = d; s.bypass = bypass;
        blocks(io, [&](float** p, int n) { e->process(p, 2, n, s); }); } };

// Generic 4x core with the same JUCE oversampler configuration as MIX/MASTER.
struct OSCore {
    juce::dsp::Oversampling<float> os; int ch;
    OSCore(int ch) : os((size_t) ch, 2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true), ch(ch) { os.initProcessing(BLOCK); os.reset(); }
    template <class F> void process(float** p, int n, F&& frame) {
        array<const float*, 2> cp { p[0], ch > 1 ? p[1] : nullptr };
        juce::dsp::AudioBlock<const float> in(cp.data(), (size_t) ch, (size_t) n);
        auto up = os.processSamplesUp(in);
        for (size_t i = 0; i < up.getNumSamples(); ++i) { float x[2]; for (int c = 0; c < ch; ++c) x[c] = up.getChannelPointer((size_t) c)[i];
            frame(x); for (int c = 0; c < ch; ++c) up.getChannelPointer((size_t) c)[i] = x[c]; }
        juce::dsp::AudioBlock<float> out(p, (size_t) ch, (size_t) n); os.processSamplesDown(out);
    }
};
// Exact replica of MasterChannelCore::driveResidual (+shapeIntensity) with switchable protection.
struct ReplicaProc : Proc { float d; bool protect; double protSum = 0; long protN = 0; double transSum = 0;
    ReplicaProc(float d, bool p) : d(d), protect(p) {}
    void run(Sig& io) override {
        const double fs = FS * 4; float fa = 1.0f - exp(-1.0f / (float)(fs * 0.0005)), fr = 1.0f - exp(-1.0f / (float)(fs * 0.020));
        float sa = 1.0f - exp(-1.0f / (float)(fs * 0.015)), sr = 1.0f - exp(-1.0f / (float)(fs * 0.080));
        const float x0 = clamp(d, 0.f, 1.f); const float amount = x0 * (0.38f + 0.62f * x0 * x0) * 1.75f;
        float fe[2] {}, se[2] {}; OSCore core(2); long idx = 0; const long warm = (long)(fs * 1.0);
        blocks(io, [&](float** p, int n) { core.process(p, n, [&](float* x) {
            for (int c = 0; c < 2; ++c) { const float m = fabs(x[c]);
                fe[c] += (m > fe[c] ? fa : fr) * (m - fe[c]); se[c] += (m > se[c] ? sa : sr) * (m - se[c]);
                const float I = clamp(amount / 1.75f, 0.f, 1.f); if (I <= 0) continue;
                const float tr = clamp((fe[c] - se[c]) / max(fe[c], 1e-6f), 0.f, 1.f);
                const float prot = protect ? 0.85f * (1 - I) * tr : 0.f; const float mixw = 0.72f * I * (1 - prot);
                const float g = 1 + 5 * I; x[c] += (tanh(x[c] * g) / g - x[c]) * mixw;
                if (idx > warm) { protSum += prot; transSum += tr; ++protN; } }
            ++idx; }); }); } };
// Prototype A. mode 0 stereo-linked; 1 M/S with drive on Mid only; 2 on Side only.
struct ProtoProc : Proc { float d; int mode; ProtoProc(float d, int m = 0) : d(d), mode(m) {}
    void run(Sig& io) override {
        ProtoDrive pd; pd.prepare(FS * 4);
        if (mode == 0) { OSCore core(2); array<ProtoState, 2> st {}; blocks(io, [&](float** p, int n) { core.process(p, n, [&](float* x) { pd.processFrame(x, 2, d, st); }); }); return; }
        OSCore cm(1), cs(1); array<ProtoState, 2> sm {}, ss {}; const float k = 0.70710678f; vector<float> M(BLOCK), S(BLOCK);
        blocks(io, [&](float** p, int n) {
            for (int i = 0; i < n; ++i) { M[i] = (p[0][i] + p[1][i]) * k; S[i] = (p[0][i] - p[1][i]) * k; }
            float* pm[1] { M.data() }; float* ps[1] { S.data() };
            cm.process(pm, n, [&](float* x) { pd.processFrame(x, 1, mode == 1 ? d : 0.f, sm); });
            cs.process(ps, n, [&](float* x) { pd.processFrame(x, 1, mode == 2 ? d : 0.f, ss); });
            for (int i = 0; i < n; ++i) { p[0][i] = (M[i] + S[i]) * k; p[1][i] = (M[i] - S[i]) * k; } }); } };

Sig runP(Proc& p, Sig in) { p.run(in); return in; }

// ---------- metrics ----------
void fft(vector<complex<double>>& a) { const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i) { size_t bit = n >> 1; for (; j & bit; bit >>= 1) j ^= bit; j ^= bit; if (i < j) swap(a[i], a[j]); }
    for (size_t len = 2; len <= n; len <<= 1) { const auto w = polar(1.0, -2 * PI / (double) len);
        for (size_t i = 0; i < n; i += len) { complex<double> u(1); for (size_t k = 0; k < len / 2; ++k) { auto x = a[i+k], y = a[i+k+len/2] * u; a[i+k] = x + y; a[i+k+len/2] = x - y; u *= w; } } } }
struct Lvl { double rms, peak, crest; };
Lvl level(const Sig& s, size_t a, size_t b) { double e = 0, p = 0; for (int c = 0; c < 2; ++c) for (size_t i = a; i < b; ++i) { e += (double) s[c][i] * s[c][i]; p = max(p, (double) fabs(s[c][i])); }
    const double r = sqrt(e / (2.0 * (b - a))); return { db(r), db(p), db(p) - db(r) }; }
double residualDb(const Sig& x, const Sig& ref, size_t a, size_t b, int lag = 0) { double e = 0, r = 0;
    for (int c = 0; c < 2; ++c) for (size_t i = a; i < b; ++i) { const double d = x[c][i + lag] - ref[c][i]; e += d * d; r += (double) ref[c][i] * ref[c][i]; }
    return 10 * log10(max(e, 1e-30) / max(r, 1e-30)); }
constexpr size_t NFFT = 65536; constexpr int K1K = 1365; // 999.76 Hz, bin-exact
struct Spec { double thd, h2, h3, h5, nonHarm; };
Spec spectrum(const vector<float>& y, size_t start, int k) {
    vector<complex<double>> a(NFFT); for (size_t i = 0; i < NFFT; ++i) a[i] = y[start + i]; fft(a);
    auto P = [&](long b) { return norm(a[(size_t) b]); }; const double f = P(k); double h = 0, all = 0, harm = f;
    for (long n = 2; n * k < (long) NFFT / 2; ++n) { h += P(n * k); harm += P(n * k); }
    for (size_t b = 3; b < NFFT / 2; ++b) all += norm(a[b]);
    auto hd = [&](int n) { return (long) n * k < (long) NFFT / 2 ? 10 * log10(max(P(n * k), 1e-30) / f) : -999.0; };
    return { 100 * sqrt(h / f), hd(2), hd(3), hd(5), 10 * log10(max(all - harm, 1e-30) / f) }; }
Sig tone(int k, double peakDb, double seconds) { Sig s; const size_t n = (size_t)(FS * seconds); const double a = pow(10, peakDb / 20);
    for (int c = 0; c < 2; ++c) { s[c].resize(n); for (size_t i = 0; i < n; ++i) s[c][i] = (float)(a * sin(2 * PI * k * (double) i / NFFT)); } return s; }
Sig scaled(const Sig& s, double g) { Sig o = s; for (auto& c : o) for (auto& v : c) v *= (float) g; return o; }
// Synthetic drums (documented as synthetic): kick on beats 1/3, snare on 2/4, 120 BPM, 4 bars.
vector<size_t> drumOnsets;
Sig drums(double peakDb) { const size_t n = (size_t)(FS * 8.5); Sig s; s[0].assign(n, 0); uint32_t rng = 1;
    drumOnsets.clear(); for (int b = 0; b < 16; ++b) { const size_t on = (size_t)(FS * (0.25 + 0.5 * b)); drumOnsets.push_back(on); const bool kick = b % 2 == 0; double ph = 0;
        for (size_t i = 0; i < (size_t)(FS * 0.45) && on + i < n; ++i) { const double t = i / FS; double v;
            if (kick) { const double f = 48 + 90 * exp(-t / 0.025); ph += 2 * PI * f / FS; v = sin(ph) * exp(-t / 0.18) + 0.25 * exp(-t / 0.002) * ((rng = rng * 1664525u + 1013904223u) / 4294967296.0 * 2 - 1); }
            else { ph += 2 * PI * 190 / FS; const double nz = (rng = rng * 1664525u + 1013904223u) / 4294967296.0 * 2 - 1; v = 0.55 * sin(ph) * exp(-t / 0.06) + 0.6 * nz * exp(-t / 0.09); }
            s[0][on + i] += (float) v; } }
    double p = 0; for (float v : s[0]) p = max(p, (double) fabs(v)); const double g = pow(10, peakDb / 20) / p; for (auto& v : s[0]) v *= (float) g; s[1] = s[0]; return s; }
Sig impulses(double peakDb) { const size_t n = (size_t)(FS * 3); Sig s; s[0].assign(n, 0); for (size_t i = (size_t) FS; i < n; i += (size_t)(FS / 10)) s[0][i] = (float) pow(10, peakDb / 20); s[1] = s[0]; return s; }

using Maker = function<unique_ptr<Proc>(float)>;
struct Named { const char* name; Maker make; };
const vector<Named> procs { { "MIX", [](float d) { return unique_ptr<Proc>(new MixProc(d)); } },
                            { "MASTER", [](float d) { return unique_ptr<Proc>(new MasterProc(d)); } },
                            { "PROTO_A", [](float d) { return unique_ptr<Proc>(new ProtoProc(d)); } } };
const float drives[] { 0.0f, 0.3f, 0.5f, 0.7f, 1.0f };
const double levels[] { -24, -18, -12, -6 };

int main(int argc, char** argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s wav\n", argv[0]); return 1; }
    const Sig wav = loadWav(argv[1]);
    const size_t loopLen = 411429; // two loop repeats (8.57 s) found by wav_ab
    Sig music; for (int c = 0; c < 2; ++c) music[c].assign(wav[c].begin(), wav[c].begin() + (long) loopLen);
    const auto ml = level(music, 0, loopLen);
    printf("# R12 DRIVE audit. DRIVE values are knob/10 (0.3 == DRIVE 3).\n# Music excerpt: first %zu samples of WAV, native RMS %.2f dBFS, peak %.2f dBFS, crest %.2f dB\n", loopLen, ml.rms, ml.peak, ml.crest);
    const size_t W0 = (size_t) FS; // skip 1 s (smoothers, detectors, filters)

    // E0 replica validation
    { auto real = runP(*make_unique<MasterProc>(1.0f), music); ReplicaProc rp(1.0f, true); auto rep = runP(rp, music);
      auto real5 = runP(*make_unique<MasterProc>(0.5f), music); ReplicaProc rp5(0.5f, true); auto rep5 = runP(rp5, music);
      double m = 0, m5 = 0; for (int c = 0; c < 2; ++c) for (size_t i = W0; i < loopLen; ++i) { m = max(m, (double) fabs(real[c][i] - rep[c][i])); m5 = max(m5, (double) fabs(real5[c][i] - rep5[c][i])); }
      printf("\n## E0 replica vs real MasterEngine (music, after 1 s): max |diff| DRIVE10 %.3g, DRIVE5 %.3g\n", m, m5); }

    // E1 neutral path vs dry, and spectrum check above 20 kHz
    { printf("\n## E1 neutral paths vs dry input (music). Best integer lag 0..32.\n");
      struct C { const char* n; Sig out; }; vector<C> cs { { "MASTER all-zero", runP(*make_unique<MasterProc>(0.f), music) }, { "MASTER bypass param", runP(*make_unique<MasterProc>(0.f, 0, true), music) }, { "MIX all-zero", runP(*make_unique<MixProc>(0.f), music) } };
      for (auto& c : cs) { double best = 1e9; int bl = 0; for (int L = 0; L <= 32; ++L) { double r = residualDb(c.out, music, W0, loopLen - 64, L); if (r < best) { best = r; bl = L; } }
        printf("%-22s best lag %2d  residual %.1f dB  (24-bit evidence residual was -123 dB)\n", c.n, bl, best); }
      auto band = [&](const vector<float>& y, double f0, double f1) { double e = 0, tot = 0; for (size_t s0 = W0; s0 + NFFT < loopLen; s0 += NFFT) { vector<complex<double>> a(NFFT); for (size_t i = 0; i < NFFT; ++i) a[i] = y[s0 + i] * (0.5 - 0.5 * cos(2 * PI * i / NFFT)); fft(a);
            for (size_t b = 1; b < NFFT / 2; ++b) { const double f = b * FS / NFFT; tot += norm(a[b]); if (f >= f0 && f < f1) e += norm(a[b]); } } return 10 * log10(max(e, 1e-30) / tot); };
      printf("band energy re total, L channel:   dry      neutral MASTER\n");
      for (auto [a, b] : { pair{16000., 20000.}, pair{20000., 21500.}, pair{21500., 23000.}, pair{23000., 24000.} })
          printf("  %5.0f-%5.0f Hz              %7.1f dB  %7.1f dB\n", a, b, band(music[0], a, b), band(cs[0].out[0], a, b)); }

    // E2 sine 1 kHz
    printf("\n## E2 sine 999.76 Hz, level = PEAK dBFS (RMS 3.01 dB lower). resid = output vs same processor at DRIVE 0.\n");
    printf("proc     lvl drive  outRMS  outPeak  crest   THD%%      H2dBc   H3dBc   H5dBc  resid_dB\n");
    for (auto& P : procs) for (double L : levels) { Sig in = tone(K1K, L, 3.0); auto ref = runP(*P.make(0.f), in);
        for (float d : drives) { auto y = runP(*P.make(d), in); auto lv = level(y, W0, W0 + NFFT); auto sp = spectrum(y[0], W0, K1K);
            printf("%-7s %4.0f  %4.1f %7.2f %7.2f %6.2f %9.4f %7.1f %7.1f %7.1f %8.1f\n", P.name, L, d * 10, lv.rms, lv.peak, lv.crest, sp.thd, sp.h2, sp.h3, sp.h5, d == 0 ? -999.0 : residualDb(y, ref, W0, W0 + NFFT)); } }

    // E3 music at RMS -24/-18/-12/-6
    printf("\n## E3 music excerpt scaled to programme RMS level (float, no clipping; -6 RMS puts peaks near +10 dBFS).\n");
    printf("proc     RMSin drive  outRMS  outPeak  crest  dCrest  resid_dB  top0.1%%_peakDelta_dB\n");
    for (auto& P : procs) for (double L : levels) { Sig in = scaled(music, pow(10, (L - ml.rms) / 20)); auto ref = runP(*P.make(0.f), in); auto l0 = level(ref, W0, loopLen);
        vector<float> mags; for (size_t i = W0; i < loopLen; ++i) mags.push_back(fabs(ref[0][i])); nth_element(mags.begin(), mags.end() - (long)(mags.size() / 1000), mags.end()); const float thr = *(mags.end() - (long)(mags.size() / 1000));
        for (float d : drives) { auto y = runP(*P.make(d), in); auto lv = level(y, W0, loopLen);
            double num = 0, den = 0; for (size_t i = W0; i < loopLen; ++i) if (fabs(ref[0][i]) >= thr) { num += fabs(y[0][i]); den += fabs(ref[0][i]); }
            printf("%-7s %5.0f  %4.1f %7.2f %7.2f %6.2f %6.2f %8.1f %8.2f\n", P.name, L, d * 10, lv.rms, lv.peak, lv.crest, lv.crest - l0.crest, d == 0 ? -999.0 : residualDb(y, ref, W0, loopLen), db(num / den)); } }

    // E4 drums
    printf("\n## E4 synthetic drums (kick/snare), level = PEAK dBFS. attack = peak in first 5 ms per hit vs DRIVE 0 (mean dB); body = RMS 20-200 ms vs DRIVE 0.\n");
    printf("proc     lvl drive  outPeak  crest  attack_dB  body_dB  resid_dB\n");
    for (auto& P : procs) for (double L : levels) { Sig in = drums(L); auto ref = runP(*P.make(0.f), in); const auto on = drumOnsets;
        for (float d : drives) { auto y = runP(*P.make(d), in); double att = 0, body = 0; int nh = 0;
            for (size_t o : on) { if (o < W0) continue; double pa = 0, pr = 0, ba = 0, br = 0; for (size_t i = o; i < o + 240 + 64; ++i) { pa = max(pa, (double) fabs(y[0][i])); pr = max(pr, (double) fabs(ref[0][i])); }
                for (size_t i = o + 960; i < o + 9600; ++i) { ba += (double) y[0][i] * y[0][i]; br += (double) ref[0][i] * ref[0][i]; } att += db(pa / pr); body += 10 * log10(ba / br); ++nh; }
            auto lv = level(y, W0, y[0].size()); printf("%-7s %4.0f  %4.1f %7.2f %6.2f %8.2f %8.2f %8.1f\n", P.name, L, d * 10, lv.peak, lv.crest, att / nh, body / nh, d == 0 ? -999.0 : residualDb(y, ref, W0, y[0].size())); } }

    // E5 impulses
    printf("\n## E5 single-sample impulses every 100 ms. Output peak (after oversampler) vs DRIVE 0.\n");
    printf("proc     lvl drive  outPeak  peakDelta_dB\n");
    for (auto& P : procs) for (double L : levels) { Sig in = impulses(L); auto ref = runP(*P.make(0.f), in); auto r0 = level(ref, W0, ref[0].size());
        for (float d : drives) { auto y = runP(*P.make(d), in); auto lv = level(y, W0, y[0].size()); printf("%-7s %4.0f  %4.1f %7.2f %8.2f\n", P.name, L, d * 10, lv.peak, lv.peak - r0.peak); } }

    // E6 aliasing: everything that is not DC, fundamental or an in-band harmonic, re fundamental
    printf("\n## E6 alias+noise residual (all non-harmonic bins, dBc), bin-exact tones, DRIVE 10 and 0.\n");
    printf("proc     f_Hz   lvl  drive  THD%%   nonharm_dBc\n");
    for (auto& P : procs) for (int k : { 9557, 15019 }) for (double L : { -12.0, -6.0 }) for (float d : { 0.0f, 1.0f }) { Sig in = tone(k, L, 3.0); auto y = runP(*P.make(d), in); auto sp = spectrum(y[0], W0, k);
        printf("%-7s %6.0f %5.0f  %4.1f %7.3f %8.1f\n", P.name, k * FS / NFFT, L, d * 10, sp.thd, sp.nonHarm); }
    // PROTO on music-like crest: drive the alias test with a tone riding at -18 RMS plus kick bursts is out of scope; see report.

    // E7 M/S
    { printf("\n## E7 M/S mode on music (native level). Component residuals vs same processor DRIVE 0.\n");
      double em = 0, es = 0; for (size_t i = 0; i < loopLen; ++i) { double m = (music[0][i] + music[1][i]) * .7071, s = (music[0][i] - music[1][i]) * .7071; em += m * m; es += s * s; }
      printf("music Mid RMS %.2f dBFS, Side RMS %.2f dBFS\n", db(sqrt(em / loopLen)), db(sqrt(es / loopLen)));
      auto comp = [&](const Sig& y, const Sig& r, bool side) { double e = 0, rr = 0; for (size_t i = W0; i < loopLen; ++i) { double a = side ? y[0][i] - y[1][i] : y[0][i] + y[1][i], b = side ? r[0][i] - r[1][i] : r[0][i] + r[1][i]; e += (a - b) * (a - b); rr += b * b; } return 10 * log10(max(e, 1e-30) / rr); };
      printf("proc     drive_on  drive  Mid_resid_dB  Side_resid_dB\n");
      for (int mode : { 1, 2 }) for (int which = 0; which < 2; ++which) { auto make = [&](float d) -> unique_ptr<Proc> { if (which == 0) return make_unique<MasterProc>(d, mode); return make_unique<ProtoProc>(d, mode); };
          auto ref = runP(*make(0.f), music); for (float d : { 0.5f, 1.0f }) { auto y = runP(*make(d), music); printf("%-8s %-5s     %4.1f   %9.1f   %9.1f\n", which ? "PROTO_A" : "MASTER", mode == 1 ? "MID" : "SIDE", d * 10, comp(y, ref, false), comp(y, ref, true)); } } }

    // E8 protection impact, E9 detector on stationary tones
    printf("\n## E8 MASTER transient protection on music (native level and +12 dB). resid vs DRIVE 0 with/without protection.\n");
    printf("gain  drive  resid_prot  resid_noprot  lost_dB  mean_transient  mean_protection\n");
    for (double g : { 0.0, 12.0 }) { Sig in = scaled(music, pow(10, g / 20)); ReplicaProc z(0.f, true); auto ref = runP(z, in);
        for (float d : { 0.3f, 0.5f, 0.7f, 1.0f }) { ReplicaProc a(d, true), b(d, false); auto ya = runP(a, in), yb = runP(b, in); double ra = residualDb(ya, ref, W0, loopLen), rb = residualDb(yb, ref, W0, loopLen);
            printf("+%-4.0f %4.1f %10.1f %12.1f %8.1f %12.3f %14.3f\n", g, d * 10, ra, rb, rb - ra, a.transSum / max(1L, a.protN), a.protSum / max(1L, a.protN)); } }
    printf("\n## E9 MASTER detector on STATIONARY sines (-12 dBFS peak), DRIVE 5: mean 'transient' value (should be ~0 for a steady tone).\n");
    for (int k : { 55, 137, 1365, 6826 }) { ReplicaProc a(0.5f, true); runP(a, tone(k, -12, 3)); printf("  %7.1f Hz  mean transient %.3f  mean protection %.3f\n", k * FS / NFFT, a.transSum / a.protN, a.protSum / a.protN); }

    // E10 audibility anchor: HARMONICS / EXCITER residuals (user confirmed audible in Logic).
    printf("\n## E10 anchor: MASTER HARMONICS/EXCITER residual vs all-zero on music (native level).\n");
    { auto ref = runP(*make_unique<MasterProc>(0.f), music);
      for (int which = 0; which < 2; ++which) for (float v : { 0.3f, 0.5f, 1.0f }) { Sig y = music; auto e = make_unique<mst::MasterEngine>(); e->prepare(FS, BLOCK); mst::MasterSettings s; (which ? s.banks[0].exciter : s.banks[0].harmonics) = v;
          blocks(y, [&](float** p, int n) { e->process(p, 2, n, s); }); auto lv = level(y, W0, loopLen); printf("%-9s %4.1f  resid %.1f dB  outRMS %.2f\n", which ? "EXCITER" : "HARMONICS", v * 10, residualDb(y, ref, W0, loopLen), lv.rms); } }
    if (getenv("R12_VARIANT_A2")) {
      protoVariant = 1; printf("\n## E11 calibration variant A2 (knee 20..5 dB, floor -40 dBFS): music by RMS, drums & impulses by peak\n");
      for (double L : levels) { Sig in = scaled(music, pow(10, (L - ml.rms) / 20)); ProtoProc z(0.f); auto ref = runP(z, in); auto l0 = level(ref, W0, loopLen);
        for (float d : drives) { ProtoProc q(d); auto y = runP(q, in); auto lv = level(y, W0, loopLen); printf("music RMS %4.0f d %4.1f resid %7.1f dCrest %6.2f peakDelta %6.2f\n", L, d * 10, d == 0 ? -999.0 : residualDb(y, ref, W0, loopLen), lv.crest - l0.crest, lv.peak - l0.peak); } }
      for (double L : { -24.0, -6.0 }) { Sig in = drums(L); ProtoProc z(0.f); auto ref = runP(z, in); const auto on = drumOnsets;
        for (float d : drives) { ProtoProc q(d); auto y = runP(q, in); double att = 0, body = 0; int nh = 0;
          for (size_t o : on) { if (o < W0) continue; double pa = 0, pr = 0, ba = 0, br = 0; for (size_t i = o; i < o + 304; ++i) { pa = max(pa, (double) fabs(y[0][i])); pr = max(pr, (double) fabs(ref[0][i])); }
            for (size_t i = o + 960; i < o + 9600; ++i) { ba += (double) y[0][i] * y[0][i]; br += (double) ref[0][i] * ref[0][i]; } att += db(pa / pr); body += 10 * log10(ba / br); ++nh; }
          printf("drums pk %4.0f d %4.1f attack %6.2f body %6.2f resid %7.1f\n", L, d * 10, att / nh, body / nh, d == 0 ? -999.0 : residualDb(y, ref, W0, y[0].size())); } }
      for (float d : drives) { Sig in = impulses(-12); ProtoProc z(0.f); auto ref = runP(z, in); ProtoProc q(d); auto y = runP(q, in); printf("impulse -12 d %4.1f peakDelta %6.2f\n", d * 10, level(y, W0, y[0].size()).peak - level(ref, W0, ref[0].size()).peak); }
      for (double L : { -24.0, -6.0 }) for (float d : { 0.5f, 1.0f }) { Sig in = tone(K1K, L, 3.0); ProtoProc q(d); auto y = runP(q, in); auto sp = spectrum(y[0], W0, K1K); printf("sine pk %4.0f d %4.1f THD %.3f%% H2 %.1f H3 %.1f\n", L, d * 10, sp.thd, sp.h2, sp.h3); }
      { ProtoProc z(0.f); auto ref = runP(z, music); for (float d : { 0.5f, 1.0f }) { ProtoProc q(d, 2); auto y = runP(q, music); double e = 0, r = 0; for (size_t i = W0; i < loopLen; ++i) { double a = y[0][i] - y[1][i], b = ref[0][i] - ref[1][i]; e += (a - b) * (a - b); r += b * b; } printf("M/S side-only d %4.1f side resid %.1f dB\n", d * 10, 10 * log10(e / r)); } }
      for (int k : { 9557, 15019 }) { ProtoProc q(1.f); auto y = runP(q, tone(k, -6, 3)); printf("alias f %.0f d 10 nonharm %.1f dBc\n", k * FS / NFFT, spectrum(y[0], W0, k).nonHarm); }
    }
    return 0;
}
