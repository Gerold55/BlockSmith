#pragma once
// What this computer's graphics card can do, worked out once at startup, so Settings only offers
// what won't overload it: options it can't run are greyed out there with the reason, and the
// engine refuses them (from Settings, settings.txt or mods alike).
//
//   - Software rendering (no graphics card driver: llvmpipe, "GDI Generic", SwiftShader...):
//     no traced lighting, no bloom.
//   - Otherwise a short test: the GPU traces rays through a block volume, the same work as the
//     lighting (4 lights and the sun on every pixel, the worst case), timed (CPU clock around
//     glFinish) and scaled to 1920x1080. Plain OpenGL 3.3 shader work: the same test, and the
//     same lighting, on any graphics card (NVIDIA, AMD, Intel) - judged by speed, never by brand. Fast enough: High; a bit slower: Low only; slower still: Off only.
//   settings.txt `gpu_check = off` skips the test and allows everything (at the player's risk).

#include <string>

struct GpuCaps {
    std::string renderer;      // GL_RENDERER
    bool software = false;
    float testMs = -1.0f;      // the test at 1920x1080 (ms), -1 = not run
    int maxLightQuality = 2;   // 0 off, 1 low, 2 high
    bool bloom = true;
    std::string lightReason;   // why lighting is limited ("" = it isn't)
    std::string bloomReason;
};

// Runs the check (needs the GL context). skipTest: settings.txt gpu_check = off.
GpuCaps detectGpuCaps(bool skipTest);
