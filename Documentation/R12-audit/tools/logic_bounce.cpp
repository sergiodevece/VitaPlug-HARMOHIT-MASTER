// R1.2: analyse Logic bounces (automation per bar). Finds a dry reference by
// locating each bounce window inside the earlier A/B capture (same mix, loop).
#include <cstdio>
#include <cstdint>
#include <cmath>
#include <cstring>
#include <vector>
#include <string>
#include <algorithm>
using namespace std;
struct W { vector<double> L, R; };
W load(const char* p) { FILE* f = fopen(p, "rb"); vector<uint8_t> b(40000000); b.resize(fread(b.data(), 1, b.size(), f)); fclose(f);
  W w; size_t i = 12; int ch = 2, bits = 24; while (i + 8 <= b.size()) { string id((char*)&b[i], 4); uint32_t sz; memcpy(&sz, &b[i+4], 4);
    if (id == "fmt ") { ch = b[i+10]; bits = b[i+22]; }
    if (id == "data") { int by = bits / 8; for (size_t k = 0; k < sz / (ch * by); ++k) for (int c = 0; c < 2; ++c) { const uint8_t* q = &b[i+8+(k*ch+c)*by]; double v;
        if (by == 3) { int32_t x = q[0] | q[1] << 8 | q[2] << 16; if (x & 0x800000) x |= ~0xFFFFFF; v = x / 8388608.0; } else if (by == 4) { float fl; memcpy(&fl, q, 4); v = fl; } else { int16_t s; memcpy(&s, q, 2); v = s / 32768.0; }
        (c ? w.R : w.L).push_back(v); } }
    i += 8 + sz + (sz & 1); } return w; }
double db(double x) { return 20 * log10(max(x, 1e-15)); }
int main(int, char** v) { W a = load(v[1]), d = load(v[2]), h = load(v[3]); const double bar = 48000 * 60.0 / 112 * 4;
  printf("frames AB %zu drive %zu harm %zu\n", a.L.size(), d.L.size(), h.L.size());
  for (auto [name, w] : { pair{"DRIVE", &d}, pair{"HARMONICS", &h} }) { printf("\n%s per bar: rms peak crest\n", name);
    for (int k = 0; k * bar < w->L.size(); ++k) { size_t s = lround(k * bar), e = min(w->L.size(), (size_t) lround((k + 1) * bar)); double r = 0, p = 0; for (size_t i = s; i < e; ++i) { r += w->L[i]*w->L[i] + w->R[i]*w->R[i]; p = max({p, fabs(w->L[i]), fabs(w->R[i])}); }
      r = sqrt(r / (2.0 * (e - s))); printf(" bar %d  %7.2f %7.2f %6.2f  clipped>=0dBFS %ld\n", k + 1, db(r), db(p), db(p) - db(r), (long) count_if(w->L.begin() + s, w->L.begin() + e, [](double x) { return fabs(x) >= 0.9999; })); } }
  // drive vs harmonics bar 1 identity
  { double e = 0, r = 0; for (size_t i = 2000; i < (size_t) bar - 2000; ++i) { double x = d.L[i] - h.L[i]; e += x * x; r += d.L[i] * d.L[i]; } printf("\nbar1 DRIVE vs HARMONICS bounce residual %.1f dB\n", 10 * log10(max(e, 1e-30) / r)); }
  // find bar-1 (DRIVE 0) segment of drive bounce in AB capture
  size_t ref = 24000, len = 48000; double best = 1e9; long bl = 0;
  for (long o = 0; o + len < a.L.size(); ++o) { double e = 0, r = 0; for (size_t i = 0; i < len; i += 16) { double x = a.L[o + i] - d.L[ref + i]; e += x * x; r += d.L[ref+i] * d.L[ref+i]; if (e > r) break; } if (e < best * r || o == 0) { best = e / r; bl = o; } }
  printf("best AB offset for drive-bounce t=0.5s: %ld (%.4f s) coarse rel %.1f dB\n", bl, bl / 48000.0, 10 * log10(max(best, 1e-30)));
}
