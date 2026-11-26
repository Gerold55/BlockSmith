#pragma once
// Bloom: bright parts of the picture glow into their surroundings (lamps, spotlights, beams,
// lava, the sun). An option in Settings (off by default, and only where the graphics card can
// take it: GpuCheck.h). Drawn over the 3D view before the HUD: the frame is copied, its bright
// parts kept, spread out through smaller and smaller copies, and blended back on (a screen
// blend: glow brightens without washing anything out to flat white).

#include <glad/glad.h>

class Bloom {
public:
    // strength: how much glow (0..2); threshold: how bright a pixel must be to glow (0..1).
    // Only things nearer than the fog glow: fadeStart..fadeEnd (blocks) fades it out, so bright
    // fog and sky in the distance don't wash that whole direction out. nearZ/farZ: the projection's.
    void apply(int width, int height, float strength, float threshold, float fadeStart, float fadeEnd,
               float nearZ, float farZ);

private:
    // The glow is built in a chain of smaller and smaller copies (1/2, 1/4 ... 1/32 of the
    // screen), then added back up the chain: a tight bright core with a wide soft halo, the way
    // light glows in a camera lens or the eye, and cheap even at large sizes
    static constexpr int kLevels = 5;
    GLuint m_scene = 0, m_depth = 0, m_level[kLevels] = {}, m_fbo[kLevels] = {}, m_vao = 0;
    int m_lw[kLevels] = {}, m_lh[kLevels] = {};
    bool m_depthOk = true;     // (copying the depth buffer works on this driver)
    GLuint m_bright = 0, m_down = 0, m_up = 0, m_comp = 0;
    int m_w = 0, m_h = 0;
    bool m_failed = false;
    bool init();
    void resize(int w, int h);
};
