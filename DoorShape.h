#pragma once
// Doors (blocks registered with pattern = "door"): a thin panel along one side of the block.
// Where it sits comes from the block's metadata, which the doors mod (Lua) sets:
//
//   bits 0-1  facing: the way the player looked when placing it (0 north -Z, 1 east +X,
//             2 south +Z, 3 west -X). Closed, the panel is on the side nearest that player.
//   bit 2     open: the panel swings 90 degrees to the side of its hinge
//   bit 3     upper half (doors are two blocks tall; each half is a block of its own)
//   bit 4     hinge on the right (seen by the player who placed it) instead of the left
//
// Shared by the mesher (drawing), PlayerMovement (collision, also on the server) and the
// raycast (what you're pointing at), so all three always agree.

#include <cstdint>
#include <glm/glm.hpp>

namespace door {

constexpr uint8_t kFacingMask = 0x3;
constexpr uint8_t kOpen       = 0x4;
constexpr uint8_t kUpper      = 0x8;
constexpr uint8_t kHingeRight = 0x10;
constexpr float   kThickness  = 3.0f / 16.0f;

// Direction (x, z) for a facing 0..3
inline glm::ivec2 facingDir(int facing) {
    switch (facing & 3) {
    case 0:  return { 0, -1 };   // north
    case 1:  return { 1, 0 };    // east
    case 2:  return { 0, 1 };    // south
    default: return { -1, 0 };   // west
    }
}

// The side of the block (x, z) the panel is against
inline glm::ivec2 panelSide(uint8_t meta) {
    const glm::ivec2 f = facingDir(meta & kFacingMask);
    if (!(meta & kOpen)) return -f;                     // closed: the side toward the placer
    const glm::ivec2 left(f.y, -f.x);                   // open: swung to the hinge side
    return (meta & kHingeRight) ? -left : left;
}

// The panel as a box inside the block, 0..1 on each axis
inline void box(uint8_t meta, glm::vec3& mn, glm::vec3& mx) {
    const glm::ivec2 s = panelSide(meta);
    mn = glm::vec3(0.0f);
    mx = glm::vec3(1.0f);
    if (s.x > 0) mn.x = 1.0f - kThickness;
    if (s.x < 0) mx.x = kThickness;
    if (s.y > 0) mn.z = 1.0f - kThickness;
    if (s.y < 0) mx.z = kThickness;
}

} // namespace door
