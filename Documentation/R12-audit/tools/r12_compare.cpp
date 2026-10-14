// R1.2 comparison harness: MIX DRIVE (unmodified MIX source), MASTER R1 DRIVE
// (frozen copy in r1_frozen/, namespace masterdsp_r1) and MASTER R1.2 DRIVE
// (current Source/DSP). Usage: r12_compare <evidencias dir>
#include "HarmoCore.h"
#include "MasterEngineR1.h"
#include "MasterEngine.h"
#include <cstdio>
#include <cstdlib>
#include <complex>
#include <vector>
#include <string>
#include <memory>
#include <functional>
using namespace std;
namespace mix = vitaplug::harmohit::dsp;
namespace r1 = vitaplug::harmohit::masterdsp_r1;
namespace r12 = vitaplug::harmohit::masterdsp;
constexpr double PI = 3.14159265358979323846; constexpr int BLOCK = 256;
using Sig = array<vector<float>, 2>;
static double db(double x) { return 20 * log10(max(x, 1e-15)); }
static double FS = 48000.0;

Sig loadWav(const string& path) { FILE* f = fopen(path.c_str(), "rb"); if (!f) { perror(path.c_str()); exit(1); }
    vector<uint8_t> b; uint8_t t[65536]; size_t n; while ((n = fread(t, 1, sizeof t, f)) > 0) b.insert(b.end(), t, t + n); fclose(f);
    Sig s; size_t i = 12; while (i + 8 <= b.size()) { string id((char*)&b[i], 4); uint32_t sz; memcpy(&sz, &b[i+4], 4);
        if (id == "data") for (size_t k = 0; k < sz / 6; ++k) for (int c = 0; c < 2; ++c) { const uint8_t* q = &b[i+8+k*6+c*3]; int32_t v = q[0] | q[1] << 8 | q[2] << 16; if (v & 0x800000) v |= ~0xFFFFFF; s[c].push_back((float)(v / 8388608.0)); }
        i += 8 + sz + (sz & 1); } return s; }

// ---------- processors (settings may change per sample-block via a schedule) ----------
using Schedule = function<float(size_t)>; // sample index -> drive 0..1
template <class F> void blocks(Sig& io, int block, F&& f) { const int N = (int) io[0].size(); for (int o = 0; o < N; o += block) { int n = min(block, N - o); float* p[2] { io[0].data() + o, io[1].data() + o }; f(p, n, (size_t) o); } }
enum Kind { MIX, R1, R12 };
const char* kindName[] { "MIX", "R1", "R1.2" };
// mode 0 STEREO, 1 M/S drive on Mid, 2 M/S drive on Side
Sig run(Kind k, Sig io, Schedule drive, int mode = 0, int block = BLOCK) {
    if (k == MIX) { mix::HarmoCore c(mix::ProductVariant::mix); c.prepare(FS, block, 2); blocks(io, block, [&](float** p, int n, size_t o) { mix::HarmoSettings s; s.drive = drive(o); c.process(p, 2, n, s); }); return io; }
    if (k == R1) { auto e = make_unique<r1::MasterEngine>(); e->prepare(FS, block); blocks(io, block, [&](float** p, int n, size_t o) { r1::MasterSettings s; s.midSide = mode != 0; s.banks[(size_t) mode].drive = drive(o); e->process(p, 2, n, s); }); return io; }
    auto e = make_unique<r12::MasterEngine>(); e->prepare(FS, block); blocks(io, block, [&](float** p, int n, size_t o) { r12::MasterSettings s; s.midSide = mode != 0; s.banks[(size_t) mode].drive = drive(o); e->process(p, 2, n, s); }); return io; }
Sig runC(Kind k, const Sig& in, float d, int mode = 0) { return run(k, in, [d](size_t) { return d; }, mode); }

// ---------- metrics ----------
struct Lvl { double rms, peak, crest; };
Lvl level(const Sig& s, size_t a, size_t b) { double e = 0, p = 0; for (int c = 0; c < 2; ++c) for (size_t i = a; i < b; ++i) { e += (double) s[c][i] * s[c][i]; p = max(p, (double) fabs(s[c][i])); } const double r = sqrt(e / (2.0 * (b - a))); return { db(r), db(p), db(p) - db(r) }; }
double resid(const Sig& y, const Sig& ref, size_t a, size_t b) { double e = 0, r = 0; for (int c = 0; c < 2; ++c) for (size_t i = a; i < b; ++i) { double d = y[c][i] - ref[c][i]; e += d * d; r += (double) ref[c][i] * ref[c][i]; } return 10 * log10(max(e, 1e-30) / max(r, 1e-30)); }
// level-matched: least-squares gain on y first
pair<double, double> residMatched(const Sig& y, const Sig& ref, size_t a, size_t b) { double xy = 0, yy = 0; for (int c = 0; c < 2; ++c) for (size_t i = a; i < b; ++i) { xy += (double) y[c][i] * ref[c][i]; yy += (double) y[c][i] * y[c][i]; }
    const double g = xy / max(yy, 1e-30); double e = 0, r = 0; for (int c = 0; c < 2; ++c) for (size_t i = a; i < b; ++i) { double d = g * y[c][i] - ref[c][i]; e += d * d; r += (double) ref[c][i] * ref[c][i]; } return { 10 * log10(max(e, 1e-30) / max(r, 1e-30)), -db(g) }; }
void fft(vector<complex<double>>& a) { const size_t n = a.size(); for (size_t i = 1, j = 0; i < n; ++i) { size_t bit = n >> 1; for (; j & bit; bit >>= 1) j ^= bit; j ^= bit; if (i < j) swap(a[i], a[j]); }
    for (size_t len = 2; len <= n; len <<= 1) { const auto w = polar(1.0, -2 * PI / (double) len); for (size_t i = 0; i < n; i += len) { complex<double> u(1); for (size_t k = 0; k < len / 2; ++k) { auto x = a[i+k], y = a[i+k+len/2] * u; a[i+k] = x + y; a[i+k+len/2] = x - y; u *= w; } } } }
constexpr size_t NFFT = 65536;
struct Spec { double thd; array<double, 8> h; double nonHarm; };
Spec spectrum(const vector<float>& y, size_t start, int k) { vector<complex<double>> a(NFFT); for (size_t i = 0; i < NFFT; ++i) a[i] = y[start + i]; fft(a);
    auto P = [&](long b) { return norm(a[(size_t) b]); }; const double f = P(k); double hsum = 0, all = 0, harm = f; Spec s {}; s.h.fill(-999);
    for (long n = 2; n * k < (long) NFFT / 2; ++n) { hsum += P(n * k); harm += P(n * k); if (n < 8) s.h[(size_t) n] = 10 * log10(max(P(n * k), 1e-30) / f); }
    for (size_t b = 3; b < NFFT / 2; ++b) all += norm(a[b]); s.thd = 100 * sqrt(hsum / f); s.nonHarm = 10 * log10(max(all - harm, 1e-30) / f); return s; }
Sig tone(int k, double peakDb, double sec) { Sig s; const size_t n = (size_t)(FS * sec); const double a = pow(10, peakDb / 20); for (int c = 0; c < 2; ++c) { s[c].resize(n); for (size_t i = 0; i < n; ++i) s[c][i] = (float)(a * sin(2 * PI * k * (double) i / NFFT)); } return s; }
Sig scaled(const Sig& s, double g) { Sig o = s; for (auto& c : o) for (auto& v : c) v *= (float) g; return o; }
Sig silence(double sec) { Sig s; s[0].assign((size_t)(FS * sec), 0); s[1] = s[0]; return s; }
void append(Sig& a, const Sig& b) { for (int c = 0; c < 2; ++c) a[c].insert(a[c].end(), b[c].begin(), b[c].end()); }
uint32_t rng = 1; double noise() { rng = rng * 1664525u + 1013904223u; return rng / 4294967296.0 * 2 - 1; }
Sig kick(double peakDb) { Sig s; size_t n = (size_t)(FS * 0.45); s[0].resize(n); double ph = 0; for (size_t i = 0; i < n; ++i) { double t = i / FS; double f = 48 + 90 * exp(-t / 0.025); ph += 2 * PI * f / FS; s[0][i] = (float)(sin(ph) * exp(-t / 0.18) + 0.25 * exp(-t / 0.002) * noise()); }
    double p = 0; for (float v : s[0]) p = max(p, (double) fabs(v)); for (auto& v : s[0]) v *= (float)(pow(10, peakDb / 20) / p); s[1] = s[0]; return s; }
Sig snare(double peakDb) { Sig s; size_t n = (size_t)(FS * 0.35); s[0].resize(n); double ph = 0; for (size_t i = 0; i < n; ++i) { double t = i / FS; ph += 2 * PI * 190 / FS; double a = min(1.0, t / 0.0003); s[0][i] = (float)(a * (0.55 * sin(ph) * exp(-t / 0.06) + 0.6 * noise() * exp(-t / 0.09))); }
    double p = 0; for (float v : s[0]) p = max(p, (double) fabs(v)); for (auto& v : s[0]) v *= (float)(pow(10, peakDb / 20) / p); s[1] = s[0]; return s; }
Sig drumLoop(double peakDb) { Sig s = silence(0.25); for (int b = 0; b < 16; ++b) { Sig h = b % 2 ? snare(0) : kick(0); Sig gap = silence(0.5 - h[0].size() / FS); append(s, h); append(s, gap); } double p = 0; for (float v : s[0]) p = max(p, (double) fabs(v)); return scaled(s, pow(10, peakDb / 20) / p); }
double onsetPeakDelta(const Sig& y, const Sig& ref, size_t on, double ms = 5) { double a = 0, b = 0; for (size_t i = on; i < on + (size_t)(FS * ms / 1000) + 16 && i < y[0].size(); ++i) for (int c = 0; c < 2; ++c) { a = max(a, (double) fabs(y[c][i])); b = max(b, (double) fabs(ref[c][i])); } return db(a / b); }
double bodyDelta(const Sig& y, const Sig& ref, size_t on) { double a = 0, b = 0; for (size_t i = on + (size_t)(FS * 0.02); i < on + (size_t)(FS * 0.2) && i < y[0].size(); ++i) for (int c = 0; c < 2; ++c) { a += (double) y[c][i] * y[c][i]; b += (double) ref[c][i] * ref[c][i]; } return 10 * log10(max(a, 1e-30) / max(b, 1e-30)); }

const float drives[] { 0.0f, 0.3f, 0.5f, 0.7f, 1.0f };
const Kind kinds[] { MIX, R1, R12 };

int main(int argc, char** argv) {
    const string dir = argc > 1 ? argv[1] : "evidencias";
    if (argc > 2) FS = atof(argv[2]); // material is 48 kHz; at other rates it is replayed sample-for-sample
    printf("# processing sample rate %.0f Hz\n", FS);
    const Sig ab = loadWav(dir + "/HARMOHIT_MASTER_DRIVE_AB.wav");
    const Sig dr = loadWav(dir + "/harmohit_drive_stereo_30_50_80_100_por_compas.wav");
    const Sig hm = loadWav(dir + "/harmohit_harmonics_stereo_30_50_80_100_por_compas.wav");
    const double barS = 60.0 / 112 * 4; const size_t bar = (size_t) lround(48000 * barS);
    // Logic material: bar 1 of the DRIVE bounce is DRIVE 0 (source A); bar 2 is DRIVE 30 R1
    // (source B, R1 residual ~ -45 dB, used as near-dry proxy). Bars 1/3/5 and 2/4 share sources.
    Sig srcA, srcB; for (int c = 0; c < 2; ++c) { srcA[c].assign(dr[c].begin(), dr[c].begin() + (long) bar); srcB[c].assign(dr[c].begin() + (long) bar, dr[c].begin() + 2 * (long) bar); }
    Sig logicIn; for (int b = 0; b < 5; ++b) append(logicIn, b % 2 ? srcB : srcA);
    Sig abLoop; for (int c = 0; c < 2; ++c) abLoop[c].assign(ab[c].begin(), ab[c].begin() + 411429);
    printf("# R1.2 comparison. DRIVE shown as knob 0..10. resid = vs same processor at DRIVE 0; LM = after least-squares level match.\n");
    auto lv = level(logicIn, 0, logicIn[0].size()); printf("# Logic material (5 bars rebuilt from bounce): RMS %.2f dBFS, peak %.2f, crest %.2f\n", lv.rms, lv.peak, lv.crest);

    // L0 model check against Logic bounce: R1 with bar automation 0/30/50/80/100 on rebuilt input
    { printf("\n## L0 R1 model vs Logic bounce. Bar residual vs bar with same source at lower DRIVE.\n");
      const float autom[] { 0, 0.3f, 0.5f, 0.8f, 1.0f }; Schedule sch = [&](size_t i) { return autom[min<size_t>(4, i / bar)]; };
      for (Kind k : { R1, R12 }) { auto y = run(k, logicIn, sch); auto y0 = runC(k, logicIn, 0);
        printf("%-5s bar resid vs DRIVE0 (LM resid, level dB):", kindName[k]);
        for (int b = 0; b < 5; ++b) { size_t s = b * bar + 4800, e = (b + 1) * bar - 4800; auto [m, g] = residMatched(y, y0, s, e); printf("  b%d %.1f (%.1f, %+.2f)", b + 1, resid(y, y0, s, e), m, g); } printf("\n"); }
      printf("Logic bounce measured: bar3 vs bar1 (DRIVE50 vs 0) -32.1 dB (LM -36.6, -0.18 dB); bar5 vs bar1 (100 vs 0) -8.9 dB (LM -12.7, -2.85 dB)\n"); }

    // M1 music: Logic source A and AB loop at several RMS levels, all processors
    for (int which = 0; which < 2; ++which) { const Sig& base = which ? abLoop : logicIn; auto bl = level(base, 0, base[0].size());
      printf("\n## M1 %s music, programme RMS levels. Columns: outRMS peak crest dCrest resid LMresid LMgain top0.1%%peakDelta\n", which ? "A/B-loop" : "Logic 5-bar");
      for (double L : { -24.0, -18.0, -12.0, -6.0 }) { Sig in = scaled(base, pow(10, (L - bl.rms) / 20)); const size_t a = (size_t) FS, b = in[0].size();
        for (Kind k : kinds) { auto y0 = runC(k, in, 0); auto l0 = level(y0, a, b); vector<float> mags; for (size_t i = a; i < b; ++i) mags.push_back(fabs(y0[0][i])); nth_element(mags.begin(), mags.end() - (long)(mags.size() / 1000), mags.end()); float thr = *(mags.end() - (long)(mags.size() / 1000));
          for (float d : drives) { if (d == 0) continue; auto y = runC(k, in, d); auto l = level(y, a, b); auto [m, g] = residMatched(y, y0, a, b); double num = 0, den = 0; for (size_t i = a; i < b; ++i) if (fabs(y0[0][i]) >= thr) { num += fabs(y[0][i]); den += fabs(y0[0][i]); }
            printf("%-5s RMS%4.0f D%4.1f %7.2f %7.2f %6.2f %6.2f %7.1f %7.1f %+6.2f %7.2f\n", kindName[k], L, d * 10, l.rms, l.peak, l.crest, l.crest - l0.crest, resid(y, y0, a, b), m, g, db(num / den)); } } } }

    // T sines + harmonic spectrum
    printf("\n## T1 sine 999.8 Hz (peak level). THD and H2..H5 dBc\n");
    for (Kind k : kinds) for (double L : { -24.0, -12.0, -6.0 }) for (float d : { 0.3f, 0.5f, 0.7f, 1.0f }) { auto y = runC(k, tone(1365, L, 3), d); auto sp = spectrum(y[0], (size_t) FS, 1365); auto l = level(y, (size_t) FS, (size_t) FS + NFFT);
        printf("%-5s pk%4.0f D%4.1f THD %8.4f%%  H2 %6.1f H3 %6.1f H4 %6.1f H5 %6.1f  outPeak %6.2f\n", kindName[k], L, d * 10, sp.thd, sp.h[2], sp.h[3], sp.h[4], sp.h[5], l.peak); }

    // O onset scenarios
    printf("\n## O onset scenarios: attack = peak in first 5 ms vs DRIVE 0 of same processor; body = 20-200 ms energy delta\n");
    struct Sc { string name; Sig in; size_t on; };
    vector<Sc> sc;
    { Sig s = silence(2); size_t on = s[0].size(); Sig im = silence(0.5); im[0][0] = im[1][0] = (float) pow(10, -6 / 20.0); append(s, im); sc.push_back({ "impulse -6 after 2 s silence", s, on }); }
    { Sig s = silence(2); size_t on = s[0].size(); Sig im = silence(0.5); im[0][0] = im[1][0] = (float) pow(10, -20 / 20.0); append(s, im); sc.push_back({ "impulse -20 after 2 s silence", s, on }); }
    { Sig s = silence(1); size_t on = s[0].size(); append(s, kick(-6)); append(s, silence(0.5)); sc.push_back({ "kick -6 after silence", s, on }); }
    { Sig s = silence(1); size_t on = s[0].size(); append(s, snare(-6)); append(s, silence(0.5)); sc.push_back({ "snare -6 (0.3 ms rise) after silence", s, on }); }
    { auto bl = level(logicIn, 0, logicIn[0].size()); Sig s = scaled(logicIn, pow(10, (-18 - bl.rms) / 20)); s[0].resize((size_t)(FS * 2)); s[1].resize((size_t)(FS * 2)); append(s, silence(1)); size_t on = s[0].size(); append(s, kick(-6)); append(s, silence(0.5)); sc.push_back({ "kick -6 after 1 s pause in music(-18 RMS)", s, on }); }
    { auto bl = level(logicIn, 0, logicIn[0].size()); Sig s = scaled(logicIn, pow(10, (-18 - bl.rms) / 20)); s[0].resize((size_t)(FS * 2)); s[1].resize((size_t)(FS * 2)); append(s, silence(0.25)); size_t on = s[0].size(); append(s, kick(-6)); append(s, silence(0.5)); sc.push_back({ "kick -6 after 250 ms pause in music", s, on }); }
    { auto bl = level(logicIn, 0, logicIn[0].size()); Sig s = scaled(logicIn, pow(10, (-42 - bl.rms) / 20)); s[0].resize((size_t)(FS * 2)); s[1].resize((size_t)(FS * 2)); size_t on = s[0].size(); append(s, snare(-6)); append(s, silence(0.5)); sc.push_back({ "snare -6 after quiet music (-42 RMS)", s, on }); }
    { auto bl = level(logicIn, 0, logicIn[0].size()); Sig s = scaled(logicIn, pow(10, (-36 - bl.rms) / 20)); s[0].resize((size_t)(FS * 2)); s[1].resize((size_t)(FS * 2)); size_t on = s[0].size(); Sig loud = scaled(logicIn, pow(10, (-14 - bl.rms) / 20)); loud[0].resize((size_t)(FS * 2)); loud[1].resize((size_t)(FS * 2)); append(s, loud); sc.push_back({ "level step music -36 -> -14 RMS", s, on }); }
    { Sig s = drumLoop(-6); sc.push_back({ "dense drum loop -6 (hit at 4.25 s)", s, (size_t)(FS * 4.25) }); }
    printf("%-42s %-5s", "scenario", "proc"); for (float d : drives) if (d > 0) printf("   D%2.0f att/body", d * 10); printf("\n");
    for (auto& s : sc) for (Kind k : kinds) { auto y0 = runC(k, s.in, 0); printf("%-42s %-5s", s.name.c_str(), kindName[k]); for (float d : drives) if (d > 0) { auto y = runC(k, s.in, d); printf("  %+6.2f/%+5.2f", onsetPeakDelta(y, y0, s.on), bodyDelta(y, y0, s.on)); } printf("\n"); }
    printf("absolute residual (dBFS RMS) 5-200 ms after impulse -6, DRIVE 5/10 (even-term HP tail):");
    for (float d : { 0.5f, 1.0f }) { auto y0 = runC(R12, sc[0].in, 0), y = runC(R12, sc[0].in, d); double e = 0; size_t a = sc[0].on + 240, b = sc[0].on + 9600; for (size_t i = a; i < b; ++i) e += pow(y[0][i] - y0[0][i], 2); printf("  D%2.0f %.1f", d * 10, 10 * log10(e / (b - a))); } printf("\n");

    // N reverb tail / noise: decaying noise 0 -> -80 dBFS over 3 s; output/input level per window and residual absolute dBFS
    { printf("\n## N decaying tail (noise, -12 dBFS start, -27 dB/s). Per window: input dBFS | R1.2 D10: outRMS-inRMS dB, residual dBFS | R1 D10 residual dBFS\n");
      Sig s; s[0].resize((size_t)(FS * 3)); for (size_t i = 0; i < s[0].size(); ++i) s[0][i] = (float)(0.25 * noise() * pow(10, -27.0 * (i / FS) / 20)); s[1] = s[0]; Sig pre = silence(0.5); append(pre, s); s = pre;
      auto y0 = runC(R12, s, 0), y = runC(R12, s, 1), z0 = runC(R1, s, 0), z = runC(R1, s, 1);
      for (double t = 0.6; t < 3.2; t += 0.4) { size_t a = (size_t)(FS * t), b = (size_t)(FS * (t + 0.2)); double ei = 0, eo = 0, er = 0, er1 = 0; for (size_t i = a; i < b; ++i) { ei += (double) y0[0][i] * y0[0][i]; eo += (double) y[0][i] * y[0][i]; er += pow(y[0][i] - y0[0][i], 2); er1 += pow(z[0][i] - z0[0][i], 2); } const double n = (double)(b - a);
        printf("  t=%.1fs in %7.1f | out-in %+6.2f dB  resid %7.1f dBFS | R1 resid %7.1f dBFS\n", t, 10 * log10(ei / n), 10 * log10(eo / ei), 10 * log10(max(er / n, 1e-30)), 10 * log10(max(er1 / n, 1e-30))); } }

    // P pumping: short-term (20 ms) output/DRIVE0 level ratio over the level-step scenario
    { printf("\n## P pumping check on 'level step' and Logic music, R1.2: 20 ms output/DRIVE0 level ratio statistics (dB)\n");
      for (int which = 0; which < 2; ++which) { const Sig& in = which ? logicIn : sc[7].in; for (float d : { 0.5f, 1.0f }) { auto y0 = runC(R12, in, 0), y = runC(R12, in, d); vector<double> r; const size_t w = (size_t)(FS * 0.02);
          for (size_t a = (size_t) FS; a + w < in[0].size(); a += w) { double e0 = 0, e = 0; for (size_t i = a; i < a + w; ++i) for (int c = 0; c < 2; ++c) { e0 += (double) y0[c][i] * y0[c][i]; e += (double) y[c][i] * y[c][i]; } if (e0 > 1e-9) r.push_back(10 * log10(e / e0)); }
          sort(r.begin(), r.end()); double maxStep = 0; printf("  %-12s D%2.0f  20ms ratio p1 %+6.2f  median %+6.2f  p99 %+6.2f  max %+6.2f\n", which ? "Logic music" : "level step", d * 10, r[r.size() / 100], r[r.size() / 2], r[r.size() * 99 / 100], r.back()); (void) maxStep; } } }

    // A aliasing
    printf("\n## A alias+noise (non-harmonic bins) dBc, tones at -6 dBFS peak, DRIVE 10 (and 5)\n");
    for (Kind k : kinds) for (int b : { 9557, 15019, 19115 }) for (float d : { 0.5f, 1.0f }) { auto y = runC(k, tone(b, -6, 3), d); auto sp = spectrum(y[0], (size_t) FS, b); printf("%-5s %6.0f Hz D%2.0f  nonharm %7.1f dBc  THD %.3f%%\n", kindName[k], b * FS / NFFT, d * 10, sp.nonHarm, sp.thd); }
    { printf("A2 music HF: residual energy above 18 kHz vs DRIVE0, 5-bar Logic material at native level (dB re full-band DRIVE0)\n");
      for (Kind k : kinds) { auto y0 = runC(k, logicIn, 0); for (float d : { 0.5f, 1.0f }) { auto y = runC(k, logicIn, d); double hf = 0, tot = 0; for (size_t s0 = (size_t) FS; s0 + NFFT < y[0].size(); s0 += NFFT) { vector<complex<double>> a(NFFT), r(NFFT); for (size_t i = 0; i < NFFT; ++i) { double w = 0.5 - 0.5 * cos(2 * PI * i / NFFT); a[i] = (y[0][s0+i] - y0[0][s0+i]) * w; r[i] = y0[0][s0+i] * w; } fft(a); fft(r); for (size_t bb = 1; bb < NFFT / 2; ++bb) { tot += norm(r[bb]); if (bb * FS / NFFT > 18000) hf += norm(a[bb]); } }
        printf("  %-5s D%2.0f  %.1f dB\n", kindName[k], d * 10, 10 * log10(max(hf, 1e-30) / tot)); } } }

    // S M/S
    { printf("\n## S Mid/Side. Real mix (Logic source A) and synthetic independent M/S, plus very low Side.\n");
      auto comp = [&](const Sig& y, const Sig& r, bool side) { double e = 0, rr = 0; for (size_t i = (size_t) FS; i < y[0].size(); ++i) { double a = side ? y[0][i] - y[1][i] : y[0][i] + y[1][i], b = side ? r[0][i] - r[1][i] : r[0][i] + r[1][i]; e += (a - b) * (a - b); rr += b * b; } return 10 * log10(max(e, 1e-30) / max(rr, 1e-30)); };
      auto msLevels = [&](const Sig& s) { double em = 0, es = 0; for (size_t i = 0; i < s[0].size(); ++i) { double m = (s[0][i] + s[1][i]) * .7071, q = (s[0][i] - s[1][i]) * .7071; em += m * m; es += q * q; } printf("   Mid %.1f dBFS RMS, Side %.1f dBFS RMS\n", db(sqrt(em / s[0].size())), db(sqrt(es / s[0].size()))); };
      Sig indep; { auto t1 = tone(1365, -12, 4); Sig d = drumLoop(-12); d[0].resize(t1[0].size()); for (size_t i = 0; i < t1[0].size(); ++i) { double m = t1[0][i], q = d[0][i] * 0.3; indep[0].push_back((float)((m + q) * .7071)); indep[1].push_back((float)((m - q) * .7071)); } }
      Sig lowSide = srcA; for (size_t i = 0; i < lowSide[0].size(); ++i) { double m = (lowSide[0][i] + lowSide[1][i]) * .7071, q = (lowSide[0][i] - lowSide[1][i]) * .7071 * 0.03; lowSide[0][i] = (float)((m + q) * .7071); lowSide[1][i] = (float)((m - q) * .7071); }
      struct C { const char* n; const Sig* s; }; for (auto c : { C{ "real mix", &srcA }, C{ "independent (Mid sine, Side drums)", &indep }, C{ "real mix, Side -30 dB (very low)", &lowSide } }) {
        printf(" %s\n", c.n); msLevels(*c.s);
        for (Kind k : { R1, R12 }) for (int mode : { 1, 2 }) { auto y0 = run(k, *c.s, [](size_t) { return 0.f; }, mode); for (float d : { 0.3f, 0.5f, 1.0f }) { auto y = run(k, *c.s, [d](size_t) { return d; }, mode); printf("   %-5s drive on %-4s D%2.0f  Mid resid %7.1f  Side resid %7.1f\n", kindName[k], mode == 1 ? "MID" : "SIDE", d * 10, comp(y, y0, false), comp(y, y0, true)); } } }
      // stability: Side absent (pure mono) with DRIVE 10 on SIDE must output exactly mono
      { Sig mono = srcA; mono[1] = mono[0]; auto y = run(R12, mono, [](size_t) { return 1.f; }, 2); double m = 0; for (size_t i = 0; i < y[0].size(); ++i) m = max(m, (double) fabs(y[0][i] - y[1][i])); printf(" mono input, DRIVE 10 on SIDE: max |L-R| = %.3g\n", m); } }

    // C continuity: automation ramp and block-size independence
    { printf("\n## C continuity\n");
      const float autom[] { 0, 0.3f, 0.5f, 0.8f, 1.0f }; Schedule sch = [&](size_t i) { return autom[min<size_t>(4, i / bar)]; };
      auto y = run(R12, logicIn, sch), y0 = runC(R12, logicIn, 0);
      double maxD = 0, maxAt = 0; for (size_t i = 1; i < y[0].size(); ++i) for (int c = 0; c < 2; ++c) { double d1 = (y[c][i] - y0[c][i]) - (y[c][i-1] - y0[c][i-1]); if (fabs(d1) > maxD) { maxD = fabs(d1); maxAt = i / FS; } }
      double bnd = 0; for (int b = 1; b < 5; ++b) for (size_t i = b * bar - 480; i < b * bar + 4800; ++i) for (int c = 0; c < 2; ++c) bnd = max(bnd, (double) fabs((y[c][i] - y0[c][i]) - (y[c][i-1] - y0[c][i-1])));
      printf(" automation 0/30/50/80/100 on Logic material: max sample-to-sample change of the DRIVE residual %.5f (at %.2f s); within 100 ms of bar switches %.5f\n", maxD, maxAt, bnd);
      Sig dc; dc[0].assign((size_t)(FS * 2), 0.25f); dc[1] = dc[0]; Schedule jump = [](size_t i) { return i < 48000 ? 0.f : 1.f; }; auto yd = run(R12, dc, jump); double st = 0; for (size_t i = 24000; i < yd[0].size(); ++i) st = max(st, (double) fabs(yd[0][i] - yd[0][i-1])); printf(" DC 0.25 with DRIVE 0 -> 10 step: max output step %.6f\n", st);
      for (int blk : { 1, 37, 64, 512, 4096 }) { Schedule cst = [](size_t) { return 0.7f; }; auto a = run(R12, logicIn, cst, 0, blk); auto b = run(R12, logicIn, cst, 0, BLOCK); double m = 0; for (size_t i = 0; i < a[0].size(); ++i) for (int c = 0; c < 2; ++c) m = max(m, (double) fabs(a[c][i] - b[c][i])); printf(" block %4d vs 256, constant DRIVE 7: max |diff| %.3g\n", blk, m); }
      // click detector: max |2nd difference| of the output within +-50 ms of each bar switch vs. the same
      // positions one bar later/earlier where the automation does not change (same source pairs 1/3/5, 2/4)
      auto d2 = [&](const Sig& z, size_t a, size_t b) { double m = 0; for (size_t i = a; i < b; ++i) for (int c = 0; c < 2; ++c) m = max(m, (double) fabs(z[c][i] - 2 * z[c][i-1] + z[c][i-2])); return m; };
      Schedule fixedD = [&](size_t i) { return autom[min<size_t>(4, i / bar)]; };
      for (int b = 1; b < 5; ++b) { const size_t a0 = b * bar - (size_t)(FS * 0.05), a1 = b * bar + (size_t)(FS * 0.05);
          Schedule hold = [&, b](size_t i) { return autom[b]; }; auto yh = run(R12, logicIn, hold);
          printf(" switch to bar %d (DRIVE %2.0f): max |d2| automated %.5f vs DRIVE held %.5f\n", b + 1, autom[b] * 10, d2(y, a0, a1), d2(yh, a0, a1)); } }

    // H HARMONICS bounce for reference (level-matched, from Logic)
    printf("\n## H Logic HARMONICS bounce (bars same-source pairs, reference only): see bar_pairs output in report.\n");
    (void) hm;
    return 0;
}
