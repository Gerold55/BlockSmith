#pragma once
// Signs (blocks registered with pattern = "sign" or "wall_sign"): a board, on a post or against a
// wall, with text on both faces (the block's BlockText, ChunkData.h). Where the board is comes
// from the block's metadata, which the signs library (Lua) sets when it's placed:
//
//   "sign"       bits 0-3: rotation, 0..15 in steps of 22.5 degrees. 0: the front faces south (+Z),
//                4 east (+X), 8 north (-Z), 12 west (-X).
//   "wall_sign"  bits 0-1: the way the front faces: 0 north (-Z), 1 east (+X), 2 south (+Z),
//                3 west (-X). The board is against the opposite side of the block.
//
// Shared by the mesher (drawing), the raycast (what you're pointing at) and the text drawing, so
// all three agree.

#include <cmath>
#include <cstdint>
#include <glm/glm.hpp>

namespace sign {

constexpr float kWidth     = 1.0f;          // the board, in blocks
constexpr float kHeight    = 0.5f;
constexpr float kThickness = 1.0f / 12.0f;
constexpr float kPostWidth = 1.0f / 12.0f;
constexpr float kPostTop   = 7.0f / 12.0f;  // standing signs: the board sits on the post

struct Board {
    glm::vec3 center;    // inside the block (0..1)
    glm::vec3 right;     // along the board, as someone reading the front sees it
    glm::vec3 up;
    glm::vec3 normal;    // out of the front
};

inline glm::vec3 wallNormal(int facing) {
    switch (facing & 3) {
    case 0:  return { 0, 0, -1 };
    case 1:  return { 1, 0, 0 };
    case 2:  return { 0, 0, 1 };
    default: return { -1, 0, 0 };
    }
}

inline Board board(bool wall, uint8_t meta) {
    Board b;
    b.up = glm::vec3(0, 1, 0);
    if (wall) {
        b.normal = wallNormal(meta & 3);
        b.center = glm::vec3(0.5f, 0.5f, 0.5f) - b.normal * (0.5f - kThickness * 0.5f);
    } else {
        const float a = static_cast<float>(meta & 15) * (3.14159265f / 8.0f);
        b.normal = glm::vec3(std::sin(a), 0.0f, std::cos(a));
        b.center = glm::vec3(0.5f, kPostTop + kHeight * 0.5f, 0.5f);
    }
    b.right = glm::cross(b.up, b.normal);
    return b;
}

// A box around the whole sign inside its block (pointing at it, the selection outline)
inline void bounds(bool wall, uint8_t meta, glm::vec3& mn, glm::vec3& mx) {
    const Board b = board(wall, meta);
    const glm::vec3 half = glm::abs(b.right) * (kWidth * 0.5f) + glm::abs(b.up) * (kHeight * 0.5f) +
                           glm::abs(b.normal) * (kThickness * 0.5f);
    mn = glm::max(b.center - half, glm::vec3(0.0f));
    mx = glm::min(b.center + half, glm::vec3(1.0f));
    if (!wall) mn.y = 0.0f;   // (and its post)
}

} // namespace sign
