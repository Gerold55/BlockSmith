#ifndef RAYCAST_H
#define RAYCAST_H

#include <glm/glm.hpp>
#include <cmath>
#include "World.h"
#include "Block.h" // Make sure Block.h is included for BlockRegistry
#include "DoorShape.h"
#include "SignShape.h"
#include "BlockShape.h"
#include <algorithm>

// Does the ray hit this box (world coordinates)? Slab test.
inline bool rayHitsBox(const glm::vec3& o, const glm::vec3& d, const glm::vec3& mn, const glm::vec3& mx) {
    float t0 = 0.0f, t1 = 1e30f;
    for (int a = 0; a < 3; ++a) {
        if (std::abs(d[a]) < 1e-8f) {
            if (o[a] < mn[a] || o[a] > mx[a]) return false;
            continue;
        }
        float ta = (mn[a] - o[a]) / d[a], tb = (mx[a] - o[a]) / d[a];
        if (ta > tb) std::swap(ta, tb);
        t0 = std::max(t0, ta);
        t1 = std::min(t1, tb);
        if (t0 > t1) return false;
    }
    return true;
}

// Where the ray enters the box (t along d) and through which face; false if it misses
inline bool rayEntersBox(const glm::vec3& o, const glm::vec3& d, const glm::vec3& mn, const glm::vec3& mx,
                         float& tHit, glm::ivec3& n) {
    float t0 = 0.0f, t1 = 1e30f;
    int axis = -1;
    for (int a = 0; a < 3; ++a) {
        if (std::abs(d[a]) < 1e-8f) {
            if (o[a] < mn[a] || o[a] > mx[a]) return false;
            continue;
        }
        float ta = (mn[a] - o[a]) / d[a], tb = (mx[a] - o[a]) / d[a];
        if (ta > tb) std::swap(ta, tb);
        if (ta > t0) { t0 = ta; axis = a; }
        t1 = std::min(t1, tb);
        if (t0 > t1) return false;
    }
    tHit = t0;
    n = glm::ivec3(0);
    if (axis >= 0) n[axis] = d[axis] > 0.0f ? -1 : 1;
    return true;
}

struct RaycastResult {
    bool hit = false;
    glm::ivec3 blockPos{0};
    glm::ivec3 faceNormal{0}; // Direction adjacent for block placement
    glm::vec3 hitPoint{0.0f}; // where the ray met the block (world coordinates)
};

inline RaycastResult raycastBlock(const glm::vec3& rayOrigin, const glm::vec3& rayDir, float maxDistance, const World& world) {
    RaycastResult result;

    int x = static_cast<int>(std::floor(rayOrigin.x));
    int y = static_cast<int>(std::floor(rayOrigin.y));
    int z = static_cast<int>(std::floor(rayOrigin.z));

    int stepX = (rayDir.x >= 0) ? 1 : -1;
    int stepY = (rayDir.y >= 0) ? 1 : -1;
    int stepZ = (rayDir.z >= 0) ? 1 : -1;

    float tMaxX = (stepX > 0) ? (std::floor(rayOrigin.x) + 1.0f - rayOrigin.x) / rayDir.x : (rayOrigin.x - std::floor(rayOrigin.x)) / -rayDir.x;
    float tMaxY = (stepY > 0) ? (std::floor(rayOrigin.y) + 1.0f - rayOrigin.y) / rayDir.y : (rayOrigin.y - std::floor(rayOrigin.y)) / -rayDir.y;
    float tMaxZ = (stepZ > 0) ? (std::floor(rayOrigin.z) + 1.0f - rayOrigin.z) / rayDir.z : (rayOrigin.z - std::floor(rayOrigin.z)) / -rayDir.z;

    float tDeltaX = std::abs(1.0f / rayDir.x);
    float tDeltaY = std::abs(1.0f / rayDir.y);
    float tDeltaZ = std::abs(1.0f / rayDir.z);

    float distance = 0.0f;
    glm::ivec3 normal(0);

    while (distance <= maxDistance) {
        uint8_t blockId = world.getBlockAt({x, y, z});
        if (blockId != 0) {
            const auto& prop = BlockRegistry::getInstance().getBlock(blockId);
            
            // Only register a hit if the block is selectable (skips water, air, etc.).
            // Doors only where their panel is: you can reach past an open door.
            bool hitShape = true;
            glm::ivec3 shapeNormal = normal;
            float shapeT = distance;
            if (prop.isSelectable && shape::isPartial(prop)) {
                // Slabs and stairs: the nearest of their boxes, and the face of it the ray met
                shape::Box bs[shape::kMaxBoxes];
                auto get = [&](int dx, int dy, int dz, uint8_t& id, uint8_t& m) {
                    id = world.getBlockAt({x + dx, y + dy, z + dz});
                    m = world.getMetadataAt({x + dx, y + dy, z + dz});
                    return true;
                };
                const int nb = shape::boxes(prop, world.getMetadataAt({x, y, z}), get, bs);
                const glm::vec3 cell(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z));
                hitShape = false;
                float best = 1e30f;
                for (int i = 0; i < nb; ++i) {
                    float t; glm::ivec3 n;
                    if (rayEntersBox(rayOrigin, rayDir, cell + bs[i].mn, cell + bs[i].mx, t, n) && t < best) {
                        best = t; shapeNormal = n; hitShape = true;
                    }
                }
                if (hitShape) {
                    shapeT = best;
                    if (shapeNormal == glm::ivec3(0)) shapeNormal = normal;   // (started inside it)
                }
            } else if (prop.isSelectable && prop.pattern == "door") {
                glm::vec3 mn, mx;
                door::box(world.getMetadataAt({x, y, z}), mn, mx);
                const glm::vec3 cell(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z));
                hitShape = rayHitsBox(rayOrigin, rayDir, cell + mn, cell + mx);
            } else if (prop.isSelectable && (prop.pattern == "sign" || prop.pattern == "wall_sign")) {
                glm::vec3 mn, mx;
                sign::bounds(prop.pattern == "wall_sign", world.getMetadataAt({x, y, z}), mn, mx);
                const glm::vec3 cell(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z));
                hitShape = rayHitsBox(rayOrigin, rayDir, cell + mn, cell + mx);
            }
            if (prop.isSelectable && hitShape) {
                result.hit = true;
                result.blockPos = glm::ivec3(x, y, z);
                result.faceNormal = shapeNormal;
                result.hitPoint = rayOrigin + rayDir * shapeT;
                return result;
            }
        }

        if (tMaxX < tMaxY) {
            if (tMaxX < tMaxZ) {
                x += stepX;
                distance = tMaxX;
                tMaxX += tDeltaX;
                normal = glm::ivec3(-stepX, 0, 0);
            } else {
                z += stepZ;
                distance = tMaxZ;
                tMaxZ += tDeltaZ;
                normal = glm::ivec3(0, 0, -stepZ);
            }
        } else {
            if (tMaxY < tMaxZ) {
                y += stepY;
                distance = tMaxY;
                tMaxY += tDeltaY;
                normal = glm::ivec3(0, -stepY, 0);
            } else {
                z += stepZ;
                distance = tMaxZ;
                tMaxZ += tDeltaZ;
                normal = glm::ivec3(0, 0, -stepZ);
            }
        }
    }

    return result;
}

#endif // RAYCAST_H