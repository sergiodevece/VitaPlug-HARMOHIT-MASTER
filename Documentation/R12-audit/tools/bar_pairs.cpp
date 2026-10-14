// R1.2: residual between bars of one bounce, best lag +-64 around bar grid, after
// optional least-squares gain. If two bars carry the same source audio, this
// isolates processing differences.
#include <cstdio>
#include <cstdint>
#include <cmath>
#include <cstring>
#include <vector>
#include <string>
#include <algorithm>
using namespace std;
vector<double> L, R;
void load(const char* p) { FILE* f = fopen(p, "rb"); vector<uint8_t> b(40000000); b.resize(fread(b.data(), 1, b.size(), f)); fclose(f); size_t i = 12;
  while (i + 8 <= b.size()) { string id((char*)&b[i], 4); uint32_t sz; memcpy(&sz, &b[i+4], 4); if (id == "data") for (size_t k = 0; k < sz / 6; ++k) for (int c = 0; c < 2; ++c) { const uint8_t* q = &b[i+8+k*6+c*3]; int32_t x = q[0] | q[1] << 8 | q[2] << 16; if (x & 0x800000) x |= ~0xFFFFFF; (c ? R : L).push_back(x / 8388608.0); } i += 8 + sz + (sz & 1); } }
int main(int, char** v) { load(v[1]); const double bar = 48000 * 60.0 / 112 * 4; const size_t skip = 4800, len = (size_t) bar - 2 * skip - 200;
  printf("pair   lag   resid_dB  gainMatched_resid_dB  gain_dB\n");
  for (int i = 0; i < 5; ++i) for (int j = i + 1; j < 5; ++j) { long base = lround((j - i) * bar); double best = 1e9; long bl = 0;
    for (long o = base - 64; o <= base + 64; ++o) { size_t s = lround(i * bar) + skip; double e = 0, r = 0; for (size_t n = s; n < s + len; n += 4) { double d = L[n + o] - L[n]; e += d * d; r += L[n] * L[n]; } if (e / r < best) { best = e / r; bl = o; } }
    size_t s = lround(i * bar) + skip; double xy = 0, yy = 0, xx = 0; for (size_t n = s; n < s + len; ++n) for (int c = 0; c < 2; ++c) { auto& C = c ? R : L; xy += C[n] * C[n + bl]; yy += C[n + bl] * C[n + bl]; xx += C[n] * C[n]; }
    double g = xy / yy, e = 0, e0 = 0; for (size_t n = s; n < s + len; ++n) for (int c = 0; c < 2; ++c) { auto& C = c ? R : L; double d0 = C[n + bl] - C[n], d1 = g * C[n + bl] - C[n]; e0 += d0 * d0; e += d1 * d1; }
    printf("%d-%d %+5ld %9.1f %12.1f %16.2f\n", i + 1, j + 1, bl - base, 10 * log10(e0 / xx), 10 * log10(e / xx), 20 * log10(g)); }
}
