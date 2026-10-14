// R12: compare every loop repeat k against repeat 0 using exact per-repeat
// integer offsets (best of round(k*P)-3..+3), P = 2 bars at 112 BPM = 205714.2857 samples.
#include <cstdio>
#include <cstdint>
#include <cmath>
#include <cstring>
#include <vector>
#include <string>
using namespace std;
int main(int, char** v) { FILE* f = fopen(v[1], "rb"); vector<uint8_t> b(9000000); b.resize(fread(b.data(), 1, b.size(), f)); fclose(f);
  vector<double> L, R; size_t i = 12; while (i + 8 <= b.size()) { string id((char*)&b[i], 4); uint32_t sz; memcpy(&sz, &b[i+4], 4);
    if (id == "data") for (size_t k = 0; k < sz / 6; ++k) for (int c = 0; c < 2; ++c) { const uint8_t* q = &b[i+8+k*6+c*3]; int32_t x = q[0] | q[1] << 8 | q[2] << 16; if (x & 0x800000) x |= ~0xFFFFFF; (c ? R : L).push_back(x / 8388608.0); }
    i += 8 + sz + (sz & 1); }
  const double P = 48000.0 * 60.0 / 112.0 * 8.0; const size_t a0 = 24000, len = 205714 - 24000 - 8; // skip first 0.5 s of each repeat window start
  printf("repeat  start_s  best_offset  resid_dB_vs_repeat0  identical_%%\n");
  for (int k = 1; k * P + a0 + len + 3 < L.size(); ++k) { long base = lround(k * P); double best = 1e9; long bo = 0; double same = 0;
    for (long o = base - 3; o <= base + 3; ++o) { double e = 0, r = 0; size_t s = 0; for (size_t n = a0; n < a0 + len; ++n) { double d1 = L[o+n] - L[n], d2 = R[o+n] - R[n]; e += d1*d1 + d2*d2; r += L[n]*L[n] + R[n]*R[n]; s += (d1 == 0) + (d2 == 0); }
      double rel = 10 * log10(max(e, 1e-30) / r); if (rel < best) { best = rel; bo = o; same = 100.0 * s / (2.0 * len); } }
    printf("%6d %8.3f %12ld %14.1f %12.2f\n", k, k * P / 48000.0, bo, best, same); }
}
