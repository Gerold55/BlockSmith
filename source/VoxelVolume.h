#pragma once
// The world around the camera as a 3D texture, for tracing light through it on the GPU: a ray
// from a lit point to a light steps through the blocks in between, one cell at a time (the way a
// path is walked through the grid), and whatever it passes through dims it. That's how
// spotlights and lamps cast exact shadows, and beams stop at walls (Spotlights.h).
//
//   256 x 256 x 256 cells (the world's whole height, so mountain tops and deep caves are always
//   in it), one byte each: how much light a block stops (0 = none, 255 = all). The active area
//   is 128 blocks wide normally and 256 with traced lighting enabled, centered on the camera. Across,
//   the volume wraps around (cell & 255), and only chunk columns that come into range are filled.

#include "World.h"
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <cstdint>
#include <set>
#include <utility>
#include <vector>

class VoxelVolume {
public:
    static constexpr int kSize = 256;          // backing texture size across X and Z (wraps with & 255)
    static constexpr int kStandardSize = 128;  // Standard lighting keeps its existing range
    static constexpr int kHeight = CHUNK_HEIGHT;   // cells up: the whole world (256)
    // Leaves and other cut-out blocks are marked with this value: rays go through their gaps
    // (a 4x4x4 pattern of small cells in each block, about a third open), so sunlight comes
    // through a tree in spots, the way it does through real leaves
    static constexpr uint8_t kFoliage = 128;
    // Water is marked with this value (nothing else uses it): light gets through it a little
    // dimmer each block, and the shaders know where water is - for caustics on things under it,
    // sun glints on its surface and light shafts in it
    static constexpr uint8_t kWater = 24;
    // A slab or stairs holding water (waterlogged): stops light like the block, and counts as water
    static constexpr uint8_t kWaterlogged = 201;

    ~VoxelVolume();
    // The camera's position and the chunks whose blocks changed (re-meshed) since last time
    void update(const World& world, const glm::vec3& camera, const std::vector<std::pair<int, int>>& changed,
                bool extendedRange = false);
    // Binds the texture to `unit` and sets the shader's u_Voxels / u_VoxOrigin / u_VoxEnabled
    // (enabled = false: no tracing - the lighting quality setting is Off)
    void bind(GLuint program, int unit, bool enabled = true) const;
    void clear();                              // (a new world: everything filled again)
    bool ready() const { return m_tex != 0; }

    // How much light a block stops, 0..255 (shared with tests)
    static uint8_t opacityOf(uint8_t blockId);
    // The blocks changed (a reload): work out how much light each stops again
    static void invalidateOpacity();

private:
    GLuint m_tex = 0;
    glm::ivec3 m_origin{ 0 };                  // the lowest cell inside (a multiple of 16 on X and Z; Y is 0)
    int m_activeSize = kStandardSize;
    bool m_haveOrigin = false;
    std::vector<uint8_t> m_cells;              // mirror of the texture, in its (wrapped) layout
    std::set<std::pair<int, int>> m_dirty;     // chunk columns to fill
    void fillColumn(const World& world, int cx, int cz);
    void markAll();
};

// GLSL for tracing (used by the world shader and the beams): include in a shader's source.
//   voxOpacity(cell) -> 0..1, voxTransmittance(from, to, maxSteps) -> how much light gets from
//   `from` to `to` (1 = nothing in the way). The cell `to` is in (the light's own block) is skipped.
#define BLOCKSMITH_VOXEL_TRACE_GLSL R"(
uniform sampler3D u_Voxels;
uniform ivec3 u_VoxOrigin;
uniform int u_VoxSize;
uniform int u_VoxEnabled;
uniform int u_VoxColValid[256];  // per chunk column (16 x 16, wrapped): 1 = filled for where it is now
float voxOpacity(ivec3 c) {
    if (u_VoxEnabled == 0) return 0.0;
    ivec3 r = c - u_VoxOrigin;
    if (r.x < 0 || r.y < 0 || r.z < 0 || r.x >= u_VoxSize || r.y >= 256 || r.z >= u_VoxSize) return 0.0;
    // a column that just came into range still holds the blocks from the far side it wrapped
    // from: empty until it's filled (otherwise that old terrain shades far-off land)
    if (u_VoxColValid[((c.x >> 4) & 15) + ((c.z >> 4) & 15) * 16] == 0) return 0.0;
    return texelFetch(u_Voxels, ivec3(c.x & 255, c.y, c.z & 255), 0).r;
}
// 1 while lighting a leaf itself: other leaves let through their average (about half), not the
// exact gaps - the gaps are for the dappled light on things below; on the leaves they'd be noise
float g_VoxSoftFoliage = 0.0;
float voxTransmittance(vec3 from, vec3 to, int maxSteps) {
    if (u_VoxEnabled == 0) return 1.0;
    vec3 d = to - from;
    float len = length(d);
    if (len < 0.001) return 1.0;
    d /= len;
    ivec3 cell = ivec3(floor(from));
    ivec3 last = ivec3(floor(to));
    ivec3 stp = ivec3(sign(d));
    vec3 inv = 1.0 / max(abs(d), vec3(1e-6));
    vec3 nextB = vec3(cell) + max(vec3(stp), vec3(0.0));
    vec3 tMax = abs(nextB - from) * inv;
    vec3 tDelta = inv;
    float T = 1.0;
    for (int i = 0; i < maxSteps; ++i) {
        float tIn = min(min(tMax.x, tMax.y), tMax.z);      // where the ray enters the next cell
        if (tMax.x < tMax.y && tMax.x < tMax.z) { cell.x += stp.x; tMax.x += tDelta.x; }
        else if (tMax.y < tMax.z)              { cell.y += stp.y; tMax.y += tDelta.y; }
        else                                   { cell.z += stp.z; tMax.z += tDelta.z; }
        if (cell == last) break;
        if (min(min(tMax.x, tMax.y), tMax.z) > len + 1.0) break;
        // left the volume: a straight ray doesn't come back, and nothing's known out there
        ivec3 r = cell - u_VoxOrigin;
        if (r.x < -1 || r.y < -1 || r.z < -1 || r.x > u_VoxSize || r.y > 256 || r.z > u_VoxSize) break;
        float o = voxOpacity(cell);
        if (abs(o - 0.094118) < 0.002) o = 0.06;           // water: each block a little dimmer
        else if (abs(o - 0.788235) < 0.002) o = 0.784;     // waterlogged slab, stairs: as the block
        else if (abs(o - 0.50196) < 0.002) {
            // foliage: is the ray's path through this block in a gap between the leaves?
            float tOut = min(min(tMax.x, tMax.y), tMax.z);
            vec3 mid = from + d * (0.5 * (tIn + tOut));
            vec3 sub = floor(mid * 4.0);
            float h = fract(sin(dot(sub, vec3(12.9898, 78.233, 37.719))) * 43758.5453);
            o = g_VoxSoftFoliage > 0.5 ? 0.45 : (h < 0.36 ? 0.0 : 0.92);
        }
        T *= 1.0 - o;
        if (T < 0.02) return 0.0;
    }
    return T;
}
// Are this place's blocks in (traced lighting on, in range, its chunk column filled)?
bool voxColumnReady(ivec3 c) {
    if (u_VoxEnabled == 0) return false;
    ivec3 r = c - u_VoxOrigin;
    if (r.x < 0 || r.z < 0 || r.x >= u_VoxSize || r.z >= u_VoxSize) return false;
    return u_VoxColValid[((c.x >> 4) & 15) + ((c.z >> 4) & 15) * 16] != 0;
}
// Is there water in this cell? (water, or a waterlogged slab or stairs)
bool voxIsWater(ivec3 c) {
    float o = voxOpacity(c);
    return abs(o - 0.094118) < 0.002 || abs(o - 0.788235) < 0.002;
}
// How deep a point is under water (blocks of water straight above it, up to 12; 0 = not in water)
float voxWaterDepth(vec3 p) {
    ivec3 c = ivec3(floor(p));
    if (!voxIsWater(c)) return 0.0;
    float d = float(c.y + 1) - p.y;
    for (int i = 1; i <= 12; ++i) {
        if (!voxIsWater(c + ivec3(0, i, 0))) break;
        d += 1.0;
    }
    return d;
}
)"
