// R1.2 CPU comparison: 60 s stereo 48 kHz, block 256, STEREO bank, DRIVE 5 (and M/S both banks).
#include "MasterEngineR1.h"
#include "MasterEngine.h"
#include <chrono>
#include <cstdio>
#include <vector>
#include <cmath>
template <class E, class S> double bench(bool ms) { E e; e.prepare(48000, 256); S s; s.midSide = ms; s.banks[0].drive = s.banks[1].drive = s.banks[2].drive = 0.5f;
    std::vector<float> l(256), r(256); double best = 1e9;
    for (int rep = 0; rep < 3; ++rep) { auto t0 = std::chrono::steady_clock::now(); for (int b = 0; b < 48000 * 60 / 256; ++b) { for (int i = 0; i < 256; ++i) { l[i] = 0.1f * std::sin(0.03f * (b * 256 + i)); r[i] = 0.9f * l[i]; } float* p[] { l.data(), r.data() }; e.process(p, 2, 256, s); }
        best = std::min(best, std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count()); }
    return best; }
int main() { namespace a = vitaplug::harmohit::masterdsp_r1; namespace b = vitaplug::harmohit::masterdsp;
    for (bool ms : { false, true }) { double t1 = bench<a::MasterEngine, a::MasterSettings>(ms), t2 = bench<b::MasterEngine, b::MasterSettings>(ms);
        std::printf("%s: R1 %.3f s, R1.2 %.3f s per 60 s audio (ratio %.2f; realtime load R1.2 %.2f %%)\n", ms ? "M/S" : "STEREO", t1, t2, t2 / t1, 100 * t2 / 60); } }
