#pragma once
// Block shapes and rotation, shared by the mesher (drawing), PlayerMovement and the entities
// (collision, also on the server) and the raycast (what you're pointing at), so all of them
// always agree. A shape is a few boxes inside the block's cell (0..1 on each axis).
//
// Patterns (register_block's `pattern`) and what their metadata means:
//
//   "slab"    bits 0-1: 0 bottom half, 1 top half, 2 double (a full block)
//   "stairs"  bits 0-1: facing - the side the tall step is on, the way the player looked when
//             placing it: 0 north (-Z), 1 east (+X), 2 south (+Z), 3 west (-X)
//             bit 2: upside down (the step hangs from the top)
//             bits 3-5: the corner shape - 0 automatic (worked out from the stairs around it:
//             stairs meeting another flight at a right angle become an inner or outer corner),
//             or set: 1 straight, 2 inner left, 3 inner right, 4 outer left, 5 outer right.
//             Left and right are as you look from the low side toward the step (the way the
//             stairs face): an outer left corner keeps the step's left quarter.
//             register_block's `stairs_connect = false`: never automatic (straight unless set).
//   "door"    DoorShape.h
//   Slabs and stairs can be waterlogged (bit 7): they hold a water source, which flows out of
//   them like any source (not down through a solid bottom). A double slab is a full block and
//   holds none. register_block's `waterloggable = false` turns it off for a block.
//
// Rotation (register_block's `rotation = "axis"`), for logs and other blocks with ends:
//
//   bits 0-1: 0 upright (Y), 1 along X, 2 along Z - a log lying on its side (placed: along
//   the face it was put against). Its `top` and `bottom` pictures are its ends, turned with it.

#include "Block.h"
#include "DoorShape.h"
#include <cmath>
#include <cstdint>
#include <string>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace shape {

struct Box { glm::vec3 mn, mx; };
constexpr int kMaxBoxes = 4;

enum Rotation : uint8_t { kRotNone = 0, kRotAxis = 3 };

// slab metadata
constexpr uint8_t kSlabBottom = 0, kSlabTop = 1, kSlabDouble = 2;
// stairs metadata
constexpr uint8_t kStairFacing = 0x3, kStairUpsideDown = 0x4;
constexpr uint8_t kStairShapeMask = 0x38, kStairShapeShift = 3;
enum StairShape : int { kShapeAuto = 0, kShapeStraight = 1, kShapeInnerLeft = 2, kShapeInnerRight = 3,
                        kShapeOuterLeft = 4, kShapeOuterRight = 5 };
inline const char* stairShapeName(int s) {
    static const char* n[6] = { "auto", "straight", "inner_left", "inner_right", "outer_left", "outer_right" };
    return (s >= 0 && s < 6) ? n[s] : "auto";
}
inline int stairShapeFrom(const std::string& name) {
    for (int i = 0; i < 6; ++i) if (name == stairShapeName(i)) return i;
    return -1;
}

inline bool isSlab(const BlockProperty& p) { return p.pattern == "slab"; }
inline bool isStairs(const BlockProperty& p) { return p.pattern == "stairs"; }
// Shapes that aren't a full block but are drawn and collided with boxes
inline bool isPartial(const BlockProperty& p) { return isSlab(p) || isStairs(p); }

// ---- Waterlogging (slabs, stairs) ----
constexpr uint8_t kWaterlogged = 0x80;
inline bool canWaterlog(const BlockProperty& p, uint8_t meta) {
    return isPartial(p) && p.waterloggable && !(isSlab(p) && (meta & 3) == kSlabDouble);
}
inline bool isWaterlogged(const BlockProperty& p, uint8_t meta) {
    return (meta & kWaterlogged) != 0 && canWaterlog(p, meta);
}
// Is the bottom of the block solid all over (water held in it can't fall out)?
inline bool fullBottom(const BlockProperty& p, uint8_t meta) {
    if (isSlab(p)) return (meta & 3) != kSlabTop;
    if (isStairs(p)) return (meta & kStairUpsideDown) == 0;
    return true;
}

// Direction (x, z) for a facing 0..3: north, east, south, west
inline glm::ivec2 dir4(int f) {
    switch (f & 3) {
    case 0:  return { 0, -1 };
    case 1:  return { 1, 0 };
    case 2:  return { 0, 1 };
    default: return { -1, 0 };
    }
}
inline int turnLeft(int f) { return (f + 3) & 3; }    // counter-clockwise (seen from above)
inline int turnRight(int f) { return (f + 1) & 3; }
inline int opposite(int f) { return (f + 2) & 3; }

// The quarter of the upper (or lower) layer toward one horizontal direction, as x/z ranges
inline void halfToward(int f, glm::vec2& lo, glm::vec2& hi) {
    const glm::ivec2 d = dir4(f);
    lo = glm::vec2(0.0f); hi = glm::vec2(1.0f);
    if (d.x > 0) lo.x = 0.5f;
    if (d.x < 0) hi.x = 0.5f;
    if (d.y > 0) lo.y = 0.5f;
    if (d.y < 0) hi.y = 0.5f;
}

// `get(dx, dy, dz, id, meta)` reads a neighbour (relative to the block): false if unknown.
// The stairs' corner shape: 0 straight, 1 outer (only the corner step, toward `other`),
// 2 inner (the step plus the corner opposite, toward `other`). A shape set in the metadata
// wins; otherwise it's worked out from the stairs around it (unless the block doesn't connect).
template <class Get>
int stairShape(const BlockProperty& p, uint8_t meta, Get&& get, int& other) {
    auto& reg = BlockRegistry::getInstance();
    const int f = meta & kStairFacing;
    const int set = (meta & kStairShapeMask) >> kStairShapeShift;
    switch (set) {
    case kShapeStraight:   return 0;
    case kShapeInnerLeft:  other = turnLeft(f);  return 2;
    case kShapeInnerRight: other = turnRight(f); return 2;
    case kShapeOuterLeft:  other = turnLeft(f);  return 1;
    case kShapeOuterRight: other = turnRight(f); return 1;
    default: break;
    }
    if (!p.stairsConnect) return 0;
    const bool upside = (meta & kStairUpsideDown) != 0;
    auto stairAt = [&](int fdir, int& facing) {
        const glm::ivec2 d = dir4(fdir);
        uint8_t id = 0, m = 0;
        if (!get(d.x, 0, d.y, id, m) || id == 0) return false;
        if (!isStairs(reg.getBlock(id)) || (((m & kStairUpsideDown) != 0) != upside)) return false;
        facing = m & kStairFacing;
        return true;
    };
    // The corner only forms if the stairs beside it on that side don't
    // continue this flight (they'd face the same way)
    auto canTake = [&](int fdir) {
        int nf = 0;
        return !stairAt(fdir, nf) || nf != f;
    };
    int bf = 0;
    if (stairAt(f, bf) && (bf & 1) != (f & 1) && canTake(opposite(bf))) { other = bf; return 1; }
    int ff = 0;
    if (stairAt(opposite(f), ff) && (ff & 1) != (f & 1) && canTake(ff)) { other = ff; return 2; }
    return 0;
}

// The boxes of a block (0..1 in its cell). Returns how many (0 = nothing to touch).
template <class Get>
int boxes(const BlockProperty& p, uint8_t meta, Get&& get, Box out[kMaxBoxes]) {
    if (isSlab(p)) {
        const uint8_t half = meta & 3;
        if (half == kSlabDouble) { out[0] = { glm::vec3(0.0f), glm::vec3(1.0f) }; return 1; }
        if (half == kSlabTop) { out[0] = { glm::vec3(0.0f, 0.5f, 0.0f), glm::vec3(1.0f) }; return 1; }
        out[0] = { glm::vec3(0.0f), glm::vec3(1.0f, 0.5f, 1.0f) };
        return 1;
    }
    if (isStairs(p)) {
        const bool upside = (meta & kStairUpsideDown) != 0;
        const float b0 = upside ? 0.5f : 0.0f, b1 = upside ? 1.0f : 0.5f;   // the full half
        const float s0 = upside ? 0.0f : 0.5f, s1 = upside ? 0.5f : 1.0f;   // the step's layer
        out[0] = { glm::vec3(0.0f, b0, 0.0f), glm::vec3(1.0f, b1, 1.0f) };
        const int f = meta & kStairFacing;
        int other = 0;
        const int kind = stairShape(p, meta, get, other);
        glm::vec2 lo, hi;
        if (kind == 1) {                               // outer: one quarter, at the corner
            glm::vec2 l2, h2;
            halfToward(f, lo, hi);
            halfToward(other, l2, h2);
            lo = glm::max(lo, l2); hi = glm::min(hi, h2);
            out[1] = { glm::vec3(lo.x, s0, lo.y), glm::vec3(hi.x, s1, hi.y) };
            return 2;
        }
        halfToward(f, lo, hi);
        out[1] = { glm::vec3(lo.x, s0, lo.y), glm::vec3(hi.x, s1, hi.y) };
        if (kind == 2) {                               // inner: the step and one more quarter
            glm::vec2 l2, h2, l3, h3;
            halfToward(opposite(f), l2, h2);
            halfToward(other, l3, h3);
            l2 = glm::max(l2, l3); h2 = glm::min(h2, h3);
            out[2] = { glm::vec3(l2.x, s0, l2.y), glm::vec3(h2.x, s1, h2.y) };
            return 3;
        }
        return 2;
    }
    if (p.pattern == "door") {
        door::box(meta, out[0].mn, out[0].mx);
        return 1;
    }
    out[0] = { glm::vec3(0.0f), glm::vec3(1.0f) };
    return 1;
}

// The box around all of a block's boxes (the selection outline)
template <class Get>
bool bounds(const BlockProperty& p, uint8_t meta, Get&& get, glm::vec3& mn, glm::vec3& mx) {
    Box b[kMaxBoxes];
    const int n = boxes(p, meta, get, b);
    if (n == 0) return false;
    mn = b[0].mn; mx = b[0].mx;
    for (int i = 1; i < n; ++i) { mn = glm::min(mn, b[i].mn); mx = glm::max(mx, b[i].mx); }
    return true;
}

// The collision boxes of the block at (x, y, z) in world coordinates, for anything with
// getBlockAt / getMetadataAt (World). Walk-through blocks (air, fluids, plants, not walkable)
// have none. `isFluid(id)` says whether an id is a fluid.
template <class W, class FluidTest>
int worldBoxes(const W& world, int x, int y, int z, FluidTest&& isFluid, Box out[kMaxBoxes]) {
    const uint8_t id = world.getBlockAt({x, y, z});
    if (id == 0 || isFluid(id)) return 0;
    const BlockProperty& p = BlockRegistry::getInstance().getBlock(id);
    if (!p.isWalkable) return 0;
    const uint8_t meta = world.getMetadataAt({x, y, z});
    auto get = [&](int dx, int dy, int dz, uint8_t& nid, uint8_t& nmeta) {
        nid = world.getBlockAt({x + dx, y + dy, z + dz});
        nmeta = world.getMetadataAt({x + dx, y + dy, z + dz});
        return true;
    };
    const int n = boxes(p, meta, get, out);
    const glm::vec3 cell(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z));
    for (int i = 0; i < n; ++i) { out[i].mn += cell; out[i].mx += cell; }
    return n;
}

// ---- Rotation of full blocks ----

// How a rotated block is turned: its own (unturned) space to the world, about the cell's
// centre. Identity for meta 0 / no rotation.
inline glm::mat3 rotationMatrix(uint8_t mode, uint8_t meta) {
    auto rot = [](float deg, const glm::vec3& axis) { return glm::mat3(glm::rotate(glm::mat4(1.0f), glm::radians(deg), axis)); };
    glm::mat3 r(1.0f);
    if (mode == kRotAxis) {
        switch (meta & 3) {
        case 1: r = rot(-90.0f, { 0, 0, 1 }); break;   // along X
        case 2: r = rot(90.0f, { 1, 0, 0 }); break;    // along Z
        default: break;
        }
    }
    // exact quarter turns (no 0.99999 from the sines)
    for (int c = 0; c < 3; ++c)
        for (int k = 0; k < 3; ++k) r[c][k] = std::round(r[c][k]);
    return r;
}

inline bool isTurned(uint8_t mode, uint8_t meta) {
    return mode == kRotAxis && (meta & 3) != 0;
}

// Face index (0 +X, 1 -X, 2 +Y, 3 -Y, 4 +Z, 5 -Z) of a direction
inline int faceOf(const glm::vec3& n) {
    if (n.x > 0.5f) return 0;
    if (n.x < -0.5f) return 1;
    if (n.y > 0.5f) return 2;
    if (n.y < -0.5f) return 3;
    if (n.z > 0.5f) return 4;
    return 5;
}
inline glm::vec3 faceNormal(int f) {
    static const glm::vec3 n[6] = { {1,0,0}, {-1,0,0}, {0,1,0}, {0,-1,0}, {0,0,1}, {0,0,-1} };
    return n[f];
}

// Texture coordinates of a point on a face of the unturned block (p 0..1 in its cell) - the
// same mapping the mesher uses for every face, so turned blocks look like unturned ones
inline glm::vec2 faceUV(int f, const glm::vec3& p) {
    switch (f) {
    case 0:  return { 1.0f - p.z, 1.0f - p.y };
    case 1:  return { p.z, 1.0f - p.y };
    case 4:  return { p.x, 1.0f - p.y };
    case 5:  return { 1.0f - p.x, 1.0f - p.y };
    case 2:  return { p.x, p.z };
    default: return { p.x, 1.0f - p.z };
    }
}

// For a world face of a turned block: which of its own faces shows there, and the texture
// coordinates of a corner of it (p: 0..1 in the cell, world orientation)
inline int ownFace(const glm::mat3& r, int worldFace) {
    return faceOf(glm::transpose(r) * faceNormal(worldFace));
}
inline glm::vec2 turnedUV(const glm::mat3& r, int ownF, const glm::vec3& p) {
    const glm::vec3 local = glm::transpose(r) * (p - glm::vec3(0.5f)) + glm::vec3(0.5f);
    return faceUV(ownF, local);
}

} // namespace shape
