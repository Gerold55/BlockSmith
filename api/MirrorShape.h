#pragma once
// Mirrors (blocks registered with mirror = true, which gives them pattern = "mirror"): a thin
// panel against the back of its block, like a picture on a wall. Its front shows the room
// reflected (main.cpp: the reflection pass), the player too.
//
//   metadata bits 0-1: the way the front faces: 0 north (-Z), 1 east (+X), 2 south (+Z),
//   3 west (-X) (the same as wall signs). Placed by hand, the front faces away from the block
//   it was put against (on a floor or ceiling: toward the player).
//
// The block's picture is the frame: where it's see-through (alpha under 0.5) the glass shows;
// where it's drawn, the frame (or anything painted on the glass) is. A picture with nothing
// drawn on it is a frameless mirror; a round frame makes a round mirror.
//
// Mirrors side by side on one wall, facing the same way, are one mirror (one flat surface), and
// their frames join: on a side where another one carries on, the middle of the picture is
// stretched to the edge (a plain frame then goes round the whole group, not every block).
//
// Shared by the mesher (drawing), the raycast (what you're pointing at) and the renderer (where
// the reflecting surface is), so all three agree.

#include <cstdint>
#include <glm/glm.hpp>

namespace mirror {

constexpr float kThickness = 1.0f / 16.0f;

inline glm::ivec3 normalOf(int facing) {
    switch (facing & 3) {
    case 0:  return { 0, 0, -1 };
    case 1:  return { 1, 0, 0 };
    case 2:  return { 0, 0, 1 };
    default: return { -1, 0, 0 };
    }
}

// The facing for a front that points along n (a side face's normal)
inline int facingOf(const glm::ivec3& n) {
    return n.z < 0 ? 0 : n.x > 0 ? 1 : n.z > 0 ? 2 : 3;
}

// The panel's box inside its block (0..1)
inline void bounds(uint8_t meta, glm::vec3& mn, glm::vec3& mx) {
    const glm::ivec3 n = normalOf(meta & 3);
    mn = glm::vec3(0.0f);
    mx = glm::vec3(1.0f);
    if (n.x > 0) mx.x = kThickness;
    else if (n.x < 0) mn.x = 1.0f - kThickness;
    else if (n.z > 0) mx.z = kThickness;
    else mn.z = 1.0f - kThickness;
}

// The reflecting surface of a mirror in cell c, as a plane: dot(n, p) + d = 0, n out of the front
inline glm::vec4 plane(const glm::ivec3& c, uint8_t meta) {
    const glm::vec3 n = glm::vec3(normalOf(meta & 3));
    glm::vec3 mn, mx;
    bounds(meta, mn, mx);
    const glm::vec3 centre = glm::vec3(c) + (mn + mx) * 0.5f + n * (kThickness * 0.5f);
    return glm::vec4(n, -glm::dot(n, centre));
}

// The four corners of the front (world space), counter-clockwise seen from the front
inline void frontCorners(const glm::ivec3& c, uint8_t meta, glm::vec3 out[4]) {
    const glm::vec3 n = glm::vec3(normalOf(meta & 3));
    glm::vec3 mn, mx;
    bounds(meta, mn, mx);
    const glm::vec3 centre = glm::vec3(c) + (mn + mx) * 0.5f + n * (kThickness * 0.5f);
    const glm::vec3 up(0.0f, 1.0f, 0.0f);
    const glm::vec3 right = glm::cross(up, n);      // (as someone facing the mirror sees it)
    out[0] = centre - right * 0.5f - up * 0.5f;
    out[1] = centre + right * 0.5f - up * 0.5f;
    out[2] = centre + right * 0.5f + up * 0.5f;
    out[3] = centre - right * 0.5f + up * 0.5f;
}

// Which sides of a mirror another one facing the same way carries on from (as someone facing it
// sees it): bit 0 left, 1 right, 2 below, 3 above. isMirror(pos, facing) says if one's there.
template <class IsMirror>
inline int joinedSides(const glm::ivec3& c, int facing, IsMirror isMirror) {
    const glm::ivec3 n = normalOf(facing);
    const glm::ivec3 right(n.z, 0, -n.x);          // cross(up, n)
    int mask = 0;
    if (isMirror(c - right, facing)) mask |= 1;
    if (isMirror(c + right, facing)) mask |= 2;
    if (isMirror(c - glm::ivec3(0, 1, 0), facing)) mask |= 4;
    if (isMirror(c + glm::ivec3(0, 1, 0), facing)) mask |= 8;
    return mask;
}

} // namespace mirror
