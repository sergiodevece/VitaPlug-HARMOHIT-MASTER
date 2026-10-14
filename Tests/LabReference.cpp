// Reference renderer: the APPROVED DRIVE 670 LAB R0.2 engine, compiled from the LAB
// project sources (read only) in its own translation unit. Drive670Core.h there is
// byte-identical to MASTER's copy (checked by the Drive670CoreIsVerbatim test), so
// the shared inline definitions satisfy the ODR.
#include "Drive670Engine.h"
#include <vector>

void labRender(float* left, float* right, int frames, double rate, int block, const std::vector<int>& stepPerBlock)
{
    vitaplug::drive670::Drive670Engine e;
    e.setDrive(stepPerBlock.empty() ? 0 : stepPerBlock.front());   // LAB plugin: setDrive(param) then prepare
    e.prepare(rate, block);
    for (int o = 0, b = 0; o < frames; o += block, ++b)
    {
        const int n = std::min(block, frames - o);
        e.setDrive(stepPerBlock[static_cast<size_t>(std::min<int>(b, static_cast<int>(stepPerBlock.size()) - 1))]);
        float* p[] { left + o, right + o };
        e.process(p, 2, n);
    }
}
