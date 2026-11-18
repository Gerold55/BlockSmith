#include "ChunkMesh.h"
#include "ChunkData.h"
#include "Block.h"
#include "Types.h"
#include "Fluids.h"
#include "Light.h"
#include "DoorShape.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace {

constexpr float kPlantInset = 0.15f;

// Water surface height for source water and waterlogged plants: 8/9 of a block, and each
// flow level 1/9 lower, the same heights as Minecraft.
constexpr float kWaterSurfaceY = 8.0f / 9.0f;

// Face order matches the neighbor slots: 0=+X, 1=-X, 2=+Y, 3=-Y, 4=+Z, 5=-Z.
constexpr float kFaceShading[6] = { 0.8f, 0.8f, 1.0f, 0.5f, 0.7f, 0.7f };
constexpr int   kFaceOffset[6][3] = {
    { 1,  0,  0}, {-1,  0,  0},
    { 0,  1,  0}, { 0, -1,  0},
    { 0,  0,  1}, { 0,  0, -1}
};

struct FaceStyle {
    float r, g, b;
    float baseLayer;
    float overlayLayer;
    float tintBase;
    float tintOverlay;
    float shade;
};

inline float tintFlag(bool on) { return on ? 1.0f : 0.0f; }

inline Vertex makeVertex(float x, float y, float z, float u, float v, const FaceStyle& s) {
    Vertex out{x, y, z, s.r, s.g, s.b, u, v,
               s.baseLayer, s.overlayLayer, s.tintBase, s.tintOverlay, s.shade};
    return out;
}

inline void setLight(Vertex& v, float s, float r, float g, float b) {
    v.lightSky = s; v.lightR = r; v.lightG = g; v.lightB = b;
}

// How dark a corner is with 0..3 solid blocks around it (Minecraft-style ambient occlusion)
constexpr float kAoLevels[4] = { 0.50f, 0.68f, 0.84f, 1.0f };

// Two triangles: (0,1,2) and (0,2,3).
inline void pushQuad(std::vector<Vertex>& buf, const Vertex (&q)[4]) {
    buf.push_back(q[0]); buf.push_back(q[1]); buf.push_back(q[2]);
    buf.push_back(q[0]); buf.push_back(q[2]); buf.push_back(q[3]);
}

// Same quad, wound both ways, so plants are visible from either side.
inline void pushDoubleSided(std::vector<Vertex>& buf, const Vertex (&q)[4]) {
    pushQuad(buf, q);
    buf.push_back(q[0]); buf.push_back(q[2]); buf.push_back(q[1]);
    buf.push_back(q[0]); buf.push_back(q[3]); buf.push_back(q[2]);
}

// Corner positions for a cube face. Corners go in the same order as the UVs
// (0,1), (1,1), (1,0), (0,0).
void faceCorners(int f, float x, float y, float z, float topY, float out[4][3]) {
    auto set = [&](int i, float px, float py, float pz) {
        out[i][0] = px; out[i][1] = py; out[i][2] = pz;
    };
    switch (f) {
    case 0: // +X
        set(0, x + 1, y,    z + 1);
        set(1, x + 1, y,    z);
        set(2, x + 1, topY, z);
        set(3, x + 1, topY, z + 1);
        break;
    case 1: // -X
        set(0, x, y,    z);
        set(1, x, y,    z + 1);
        set(2, x, topY, z + 1);
        set(3, x, topY, z);
        break;
    case 2: // +Y (top)
        set(0, x,     topY, z + 1);
        set(1, x + 1, topY, z + 1);
        set(2, x + 1, topY, z);
        set(3, x,     topY, z);
        break;
    case 3: // -Y (bottom)
        set(0, x,     y, z);
        set(1, x + 1, y, z);
        set(2, x + 1, y, z + 1);
        set(3, x,     y, z + 1);
        break;
    case 4: // +Z
        set(0, x,     y,    z + 1);
        set(1, x + 1, y,    z + 1);
        set(2, x + 1, topY, z + 1);
        set(3, x,     topY, z + 1);
        break;
    default: // -Z
        set(0, x + 1, y,    z);
        set(1, x,     y,    z);
        set(2, x,     topY, z);
        set(3, x + 1, topY, z);
        break;
    }
}

void setupVertexAttributes() {
    const GLsizei stride = sizeof(Vertex);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(Vertex, posX));          glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(Vertex, tintR));         glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(Vertex, texCoordX));     glEnableVertexAttribArray(2);
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(Vertex, baseLayer));     glEnableVertexAttribArray(3);
    glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(Vertex, overlayLayer));  glEnableVertexAttribArray(4);
    glVertexAttribPointer(5, 1, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(Vertex, tintBase));      glEnableVertexAttribArray(5);
    glVertexAttribPointer(6, 1, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(Vertex, tintOverlay));   glEnableVertexAttribArray(6);
    glVertexAttribPointer(7, 1, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(Vertex, faceShading));   glEnableVertexAttribArray(7);
    glVertexAttribPointer(8, 4, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(Vertex, lightSky));      glEnableVertexAttribArray(8);
}

void uploadBuffer(GLuint& vao, GLuint& vbo, const std::vector<Vertex>& verts) {
    if (vao == 0) {
        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);
    }
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 verts.size() * sizeof(Vertex),
                 verts.empty() ? nullptr : verts.data(),
                 GL_STATIC_DRAW);
    setupVertexAttributes();
}

// The fluid a block belongs to (water, lava, modded fluids), or nullptr. See Fluids.h.
inline const FluidDef* fluidOf(uint8_t id) {
    return FluidRegistry::get().forBlock(id);
}

inline bool isPlantLike(const BlockProperty& p) {
    return p.pattern == "plantlike";
}

inline bool isCutout(const BlockProperty& p) {
    return p.isCutout;
}

inline bool canWaterlog(const BlockProperty& p) {
    // Doors use their metadata for facing/open, not for "waterlogged"
    return (isPlantLike(p) || p.isTransparent) && !fluidOf(p.id) && p.pattern != "door";
}

// Fluids never hide what's next to them (even opaque ones like lava: their surface is
// lower than a full block, so the faces behind must still be drawn)
inline bool isOpaqueSolid(const BlockProperty& p) {
    return !p.isTransparent && !isPlantLike(p) && !isCutout(p) && !fluidOf(p.id);
}

// A cell of `fluid` (local coords, may be in a neighbouring chunk): is it that fluid, and
// how high is its surface (0..1)? Waterlogged plants count as water sources.
bool fluidSurfaceAt(const ChunkData& data, const BlockRegistry& registry, const FluidDef& fluid,
                    int x, int y, int z, float& height, bool& source) {
    const uint8_t id = data.getBlock(x, y, z);
    if (id == 0) return false;
    const uint8_t meta = data.getMetadata(x, y, z);
    if (const FluidDef* f = fluidOf(id)) {
        if (f->name != fluid.name) return false;
        const FluidCell c = FluidRegistry::get().cell(id, meta);
        height = FluidRegistry::surfaceHeight(c);
        source = c.source || c.falling;   // falling weighs like a source in the corner average
        return true;
    }
    if (fluid.name == "water" && canWaterlog(registry.getBlock(id)) && (meta & 1) != 0) {
        height = kWaterSurfaceY;
        source = true;
        return true;
    }
    return false;
}

// Height (0..1 above the block) of one top corner of a fluid block. Corner (cx, cz) is
// shared by the 4 cells around it; their surfaces are averaged, sources weighted 10x and
// open space pulling the corner down, as in Minecraft. Same fluid above any: full height.
float fluidCornerHeight(const ChunkData& data, const BlockRegistry& registry, const FluidDef& fluid,
                        int x, int y, int z, int cx, int cz) {
    float sum = 0.0f, weight = 0.0f;
    for (int dx = cx - 1; dx <= cx; ++dx) {
        for (int dz = cz - 1; dz <= cz; ++dz) {
            // A chunk that isn't loaded yet tells us nothing (it would read as air and make
            // the surface dip); the corner is redrawn when it loads
            if (!data.hasColumn(x + dx, z + dz)) continue;
            float h = 0.0f, hAbove = 0.0f;
            bool src = false, srcAbove = false;
            if (fluidSurfaceAt(data, registry, fluid, x + dx, y + 1, z + dz, hAbove, srcAbove)) return 1.0f;
            if (fluidSurfaceAt(data, registry, fluid, x + dx, y, z + dz, h, src)) {
                const float w = src ? 10.0f : 1.0f;
                sum += h * w;
                weight += w;
            } else if (data.getBlock(x + dx, y, z + dz) == 0) {
                weight += 1.0f;   // open air beside it: the surface slopes down (shores, edges)
            }
            // Anything else (solid ground, a plant or other block standing in the water,
            // another fluid) doesn't pull the surface down
        }
    }
    return weight > 0.0f ? sum / weight : kWaterSurfaceY;
}

// Face culling rules preventing clipping artifacts between water and waterlogged foliage
bool isFaceHidden(const BlockProperty& self, uint8_t selfId, const FluidDef* renderingFluid,
                  const BlockProperty& nb, uint8_t nbId, bool nbWaterlogged) {
    if (isPlantLike(nb) && !nbWaterlogged) return false; // dry plants do not occlude

    if (renderingFluid) {
        // Hidden against the same fluid (one surface) and solid blocks; a different fluid
        // (water next to lava) shows its face
        const FluidDef* nf = fluidOf(nbId);
        return (nf && nf->name == renderingFluid->name)
            || (nbWaterlogged && renderingFluid->name == "water")
            || isOpaqueSolid(nb);
    }
    if (isCutout(self)) {
        return isOpaqueSolid(nb);
    }
    if (self.isTransparent) {
        return isOpaqueSolid(nb) || nbId == selfId;
    }
    return isOpaqueSolid(nb);
}

} // namespace

bool ChunkMesh::smoothLighting = true;
bool ChunkMesh::ambientOcclusion = true;

ChunkMesh::~ChunkMesh() {
    if (opaqueVAO != 0)      glDeleteVertexArrays(1, &opaqueVAO);
    if (opaqueVBO != 0)      glDeleteBuffers(1, &opaqueVBO);
    if (transparentVAO != 0) glDeleteVertexArrays(1, &transparentVAO);
    if (transparentVBO != 0) glDeleteBuffers(1, &transparentVBO);
}

void ChunkMesh::generateMeshData(const ChunkData& data) {
    cpuOpaqueVertices.clear();
    cpuTransparentVertices.clear();

    auto& registry = BlockRegistry::getInstance();
    FluidRegistry::get().refresh();   // block ids by name: never stale
    const FluidDef* waterFluid = FluidRegistry::get().byName("water");

    // ---- Light (Light.h) ----
    auto opaqueAt = [&](int x, int y, int z) {
        const uint8_t id = data.getBlock(x, y, z);
        return id != 0 && light::blockLight(id).opaque;
    };
    // Plants and other see-through shapes: the light of their own cell
    auto lightCell = [&](Vertex* verts, int count, int x, int y, int z) {
        const uint16_t l = data.getLight(x, y, z);
        const light::BlockLight& own = light::blockLight(data.getLocalBlock(x, y, z));
        for (int i = 0; i < count; ++i) {
            setLight(verts[i], light::sky(l) / 15.0f,
                     std::max(light::red(l), (int)own.emit[0]) / 15.0f,
                     std::max(light::green(l), (int)own.emit[1]) / 15.0f,
                     std::max(light::blue(l), (int)own.emit[2]) / 15.0f);
        }
    };
    // Cube faces: each corner averages the light of the (up to) 4 cells in front of the face
    // that touch it, and is darkened by solid blocks around it (ambient occlusion)
    auto lightFace = [&](Vertex (&q)[4], int f, int x, int y, int z) {
        const int axis = (f < 2) ? 0 : (f < 4) ? 1 : 2;
        const int t1 = (axis == 0) ? 1 : 0;
        const int t2 = (axis == 2) ? 1 : 2;
        int front[3] = { x + kFaceOffset[f][0], y + kFaceOffset[f][1], z + kFaceOffset[f][2] };
        const int self[3] = { x, y, z };
        const bool frontSolid = opaqueAt(front[0], front[1], front[2]);
        const light::BlockLight& own = light::blockLight(data.getLocalBlock(x, y, z));
        // Faces that touch a solid block (rare: only shown for see-through blocks) use their own cell
        const int* base = frontSolid ? self : front;
        for (Vertex& v : q) {
            const float pos[3] = { v.posX, v.posY, v.posZ };
            int d1 = (pos[t1] - self[t1] > 0.5f) ? 1 : -1;
            int d2 = (pos[t2] - self[t2] > 0.5f) ? 1 : -1;
            int c1[3] = { base[0], base[1], base[2] }; c1[t1] += d1;
            int c2[3] = { base[0], base[1], base[2] }; c2[t2] += d2;
            int cc[3] = { base[0], base[1], base[2] }; cc[t1] += d1; cc[t2] += d2;

            const bool s1 = opaqueAt(c1[0], c1[1], c1[2]);
            const bool s2 = opaqueAt(c2[0], c2[1], c2[2]);
            const bool sc = opaqueAt(cc[0], cc[1], cc[2]);

            float sum[4] = {0, 0, 0, 0};
            int n = 0;
            auto add = [&](const int* p) {
                const uint16_t l = data.getLight(p[0], p[1], p[2]);
                sum[0] += light::sky(l); sum[1] += light::red(l); sum[2] += light::green(l); sum[3] += light::blue(l);
                ++n;
            };
            add(base);
            if (smoothLighting && !frontSolid) {
                if (!s1) add(c1);
                if (!s2) add(c2);
                if (!sc && !(s1 && s2)) add(cc);
            }
            const float k = 1.0f / (15.0f * static_cast<float>(n));
            setLight(v, sum[0] * k,
                     std::max(sum[1] * k, own.emit[0] / 15.0f),
                     std::max(sum[2] * k, own.emit[1] / 15.0f),
                     std::max(sum[3] * k, own.emit[2] / 15.0f));
            if (ambientOcclusion && !frontSolid) {
                const int level = (s1 && s2) ? 0 : 3 - (int(s1) + int(s2) + int(sc));
                v.faceShading *= kAoLevels[level];
            }
        }
    };

    // Biome colours (ChunkData's column tints): a vertex takes the average colour of the up to
    // 4 columns around its corner, so colours blend smoothly from block to block
    auto applyBiomeTint = [&](Vertex* verts, int count, int channel) {
        if (channel < 0 || !data.hasTint(channel)) return;
        for (int i = 0; i < count; ++i) {
            const int cx = static_cast<int>(std::lround(verts[i].posX));
            const int cz = static_cast<int>(std::lround(verts[i].posZ));
            float r = 0, g = 0, b = 0;
            int n = 0;
            for (int dx = -1; dx <= 0; ++dx) {
                for (int dz = -1; dz <= 0; ++dz) {
                    const uint32_t t = data.getColumnTint(cx + dx, cz + dz, channel);
                    if (t == ChunkData::kNoTint) continue;
                    r += static_cast<float>((t >> 16) & 0xFF);
                    g += static_cast<float>((t >> 8) & 0xFF);
                    b += static_cast<float>(t & 0xFF);
                    ++n;
                }
            }
            if (n == 0) continue;
            const float k = 1.0f / (255.0f * static_cast<float>(n));
            verts[i].tintR = r * k;
            verts[i].tintG = g * k;
            verts[i].tintB = b * k;
        }
    };

    auto emitPlant = [&](int x, int y, int z, const BlockProperty& prop) {
        const FaceStyle style{
            prop.defaultTint.r, prop.defaultTint.g, prop.defaultTint.b,
            static_cast<float>(prop.faces[0].baseLayer >= 0 ? prop.faces[0].baseLayer
                                                            : prop.faces[2].baseLayer),
            prop.faces[0].overlayLayer >= 0 ? static_cast<float>(prop.faces[0].overlayLayer) : -1.0f,
            tintFlag(prop.faces[0].tintBase),
            tintFlag(prop.faces[0].tintOverlay),
            1.0f
        };

        const float fx = static_cast<float>(x);
        const float fy = static_cast<float>(y);
        const float fz = static_cast<float>(z);
        const float minX = fx + kPlantInset, maxX = fx + 1.0f - kPlantInset;
        const float minZ = fz + kPlantInset, maxZ = fz + 1.0f - kPlantInset;
        const float minY = fy, maxY = fy + 1.0f;

        Vertex p1[4] = {
            makeVertex(minX, minY, minZ, 0.0f, 1.0f, style),
            makeVertex(maxX, minY, maxZ, 1.0f, 1.0f, style),
            makeVertex(maxX, maxY, maxZ, 1.0f, 0.0f, style),
            makeVertex(minX, maxY, minZ, 0.0f, 0.0f, style),
        };
        Vertex p2[4] = {
            makeVertex(minX, minY, maxZ, 0.0f, 1.0f, style),
            makeVertex(maxX, minY, minZ, 1.0f, 1.0f, style),
            makeVertex(maxX, maxY, minZ, 1.0f, 0.0f, style),
            makeVertex(minX, maxY, maxZ, 0.0f, 0.0f, style),
        };

        applyBiomeTint(p1, 4, prop.biomeTint);
        applyBiomeTint(p2, 4, prop.biomeTint);
        lightCell(p1, 4, x, y, z);
        lightCell(p2, 4, x, y, z);
        pushDoubleSided(cpuOpaqueVertices, p1);
        pushDoubleSided(cpuOpaqueVertices, p2);
    };

    // Doors: a thin panel along one side (DoorShape.h), every face drawn. The upper half uses
    // the block's top texture, the lower half its bottom texture; the thin edges show a strip
    // of the same picture. Across the panel the picture is measured from the hinge edge, so the
    // handle side of the picture is always the edge away from the hinges, like a real door: seen
    // from the back it's mirrored, and it stays right when the door swings open or is hinged on
    // the other side.
    auto emitDoor = [&](int x, int y, int z, const BlockProperty& prop) {
        const uint8_t meta = data.getLocalMetadata(x, y, z);
        const bool upper = (meta & door::kUpper) != 0;
        int layer = upper ? prop.faces[2].baseLayer : prop.faces[3].baseLayer;
        for (int f : { 3, 2, 4, 0, 1, 5 }) if (layer < 0) layer = prop.faces[f].baseLayer;
        if (layer < 0) return;
        glm::vec3 mn, mx;
        door::box(meta, mn, mx);

        // Where the hinges are (x, z inside the block): the placer's left or right edge of the
        // closed position, which stays put when the door opens
        const glm::ivec2 fd = door::facingDir(meta & door::kFacingMask);
        const glm::ivec2 left(fd.y, -fd.x);
        const glm::ivec2 hingeSide = (meta & door::kHingeRight) ? -left : left;
        const float hingeX = 0.5f - 0.5f * fd.x + 0.5f * hingeSide.x;
        const float hingeZ = 0.5f - 0.5f * fd.y + 0.5f * hingeSide.y;
        const bool alongX = (mx.x - mn.x) > (mx.z - mn.z);   // the panel's long side runs along x

        const FaceStyle style{ prop.defaultTint.r, prop.defaultTint.g, prop.defaultTint.b,
                               static_cast<float>(layer), -1.0f, 0.0f, 0.0f, 1.0f };
        for (int f = 0; f < 6; ++f) {
            float unit[4][3];
            faceCorners(f, 0.0f, 0.0f, 0.0f, 1.0f, unit);
            const bool bigFace = alongX ? (f == 4 || f == 5) : (f == 0 || f == 1);
            Vertex q[4];
            for (int i = 0; i < 4; ++i) {
                const float px = mn.x + unit[i][0] * (mx.x - mn.x);
                const float py = mn.y + unit[i][1] * (mx.y - mn.y);
                const float pz = mn.z + unit[i][2] * (mx.z - mn.z);
                float u, v;
                if (bigFace) {
                    u = alongX ? std::fabs(px - hingeX) : std::fabs(pz - hingeZ);   // 0 at the hinges
                    v = 1.0f - py;
                } else {
                    switch (f) {
                    case 0:  u = 1.0f - pz; v = 1.0f - py; break;   // +X edge
                    case 1:  u = pz;        v = 1.0f - py; break;   // -X edge
                    case 4:  u = px;        v = 1.0f - py; break;   // +Z edge
                    case 5:  u = 1.0f - px; v = 1.0f - py; break;   // -Z edge
                    case 2:  u = px;        v = pz;        break;   // top
                    default: u = px;        v = 1.0f - pz; break;   // bottom
                    }
                }
                FaceStyle st = style;
                st.shade = kFaceShading[f];
                q[i] = makeVertex(x + px, y + py, z + pz, u, v, st);
            }
            lightCell(q, 4, x, y, z);
            pushQuad(cpuOpaqueVertices, q);
        }
    };

    auto emitCube = [&](int x, int y, int z, uint8_t blockId, const BlockProperty& selfProp,
                        const BlockProperty& renderProp, const FluidDef* renderingFluid) {
        const bool renderingWater = renderingFluid != nullptr;   // any fluid
        const float fx = static_cast<float>(x);
        const float fy = static_cast<float>(y);
        const float fz = static_cast<float>(z);

        // Water top height. Source water and waterlogged plants sit 1 pixel below the
        // block top. Flowing water (levels 1-7) is lower. Each top corner is the average of
        // the water around that corner (sources count most), so neighbouring water blocks
        // join into smooth slopes, like Minecraft. Water under water reaches the top.
        float topY = fy + 1.0f;
        float cornerTop[2][2] = {{0, 0}, {0, 0}};   // [x side][z side], 0..1 above fy
        if (renderingWater) {
            topY = fy + kWaterSurfaceY;
            for (int cx = 0; cx < 2; ++cx)
                for (int cz = 0; cz < 2; ++cz)
                    cornerTop[cx][cz] = fluidCornerHeight(data, registry, *renderingFluid, x, y, z, cx, cz);
        }

        for (int f = 0; f < 6; ++f) {
            const int nx = x + kFaceOffset[f][0];
            const int ny = y + kFaceOffset[f][1];
            const int nz = z + kFaceOffset[f][2];

            const uint8_t neighborId = data.getBlock(nx, ny, nz);
            if (neighborId != 0) {
                const auto& nProp = registry.getBlock(neighborId);
                const uint8_t neighborMeta = data.getMetadata(nx, ny, nz);   // looks across chunk borders
                const bool nWaterlogged = canWaterlog(nProp) && ((neighborMeta & 1) != 0);

                if (isFaceHidden(selfProp, blockId, renderingFluid, nProp, neighborId, nWaterlogged)) {
                    continue;
                }
            }

            // A face with no texture of its own uses the top's (or any face that has one),
            // e.g. water whose game only gave it a top texture: its sides show in holes
            // and waterfalls, and would otherwise be black
            int texFace = f;
            // Water: every face looks like the surface (as in Minecraft). A game's water
            // side/bottom faces may have no texture, an overlay or a tint meant only for
            // underwater views - those drew black in holes, waterfalls and along shores.
            if (renderingWater && renderProp.faces[2].baseLayer >= 0) texFace = 2;
            if (renderProp.faces[texFace].baseLayer < 0) {
                for (int g : { 2, 4, 0, 1, 5, 3 }) {
                    if (renderProp.faces[g].baseLayer >= 0) { texFace = g; break; }
                }
            }
            const auto& faceProp = renderProp.faces[texFace];
            const FaceStyle style{
                renderProp.defaultTint.r, renderProp.defaultTint.g, renderProp.defaultTint.b,
                static_cast<float>(faceProp.baseLayer),
                faceProp.overlayLayer >= 0 ? static_cast<float>(faceProp.overlayLayer) : -1.0f,
                tintFlag(faceProp.tintBase),
                tintFlag(faceProp.tintOverlay),
                kFaceShading[f]
            };

            float fv[4][3];
            faceCorners(f, fx, fy, fz, topY, fv);
            if (renderingWater) {
                for (auto& v : fv) {
                    if (v[1] != topY) continue;   // bottom corner
                    v[1] = fy + cornerTop[v[0] > fx ? 1 : 0][v[2] > fz ? 1 : 0];
                }
            }

            Vertex q[4] = {
                makeVertex(fv[0][0], fv[0][1], fv[0][2], 0.0f, 1.0f, style),
                makeVertex(fv[1][0], fv[1][1], fv[1][2], 1.0f, 1.0f, style),
                makeVertex(fv[2][0], fv[2][1], fv[2][2], 1.0f, 0.0f, style),
                makeVertex(fv[3][0], fv[3][1], fv[3][2], 0.0f, 0.0f, style),
            };

            const bool blended = renderProp.isTransparent && !isCutout(renderProp);
            auto& buf = blended ? cpuTransparentVertices : cpuOpaqueVertices;
            if (!renderingWater) applyBiomeTint(q, 4, renderProp.biomeTint);
            if (selfProp.pattern == "cube" || renderingWater) lightFace(q, f, x, y, z);
            else lightCell(q, 4, x, y, z);
            pushQuad(buf, q);
        }
    };

    for (int x = 0; x < CHUNK_WIDTH; ++x) {
        for (int y = 0; y < CHUNK_HEIGHT; ++y) {
            for (int z = 0; z < CHUNK_WIDTH; ++z) {
                const uint8_t blockId = data.getLocalBlock(x, y, z);
                if (blockId == 0) continue;

                const auto& prop = registry.getBlock(blockId);
                const bool waterlogged = canWaterlog(prop) && ((data.getLocalMetadata(x, y, z) & 1) != 0);

                if (prop.pattern == "door") {
                    emitDoor(x, y, z, prop);
                    continue;
                }
                if (isPlantLike(prop)) {
                    emitPlant(x, y, z, prop);
                    if (!waterlogged) continue;
                }

                // Fluids are drawn with their source block's textures (games often give the
                // flowing block none of its own); waterlogged plants with water's
                const FluidDef* fluid = fluidOf(blockId);
                if (!fluid && waterlogged) fluid = waterFluid;
                const BlockProperty& renderProp = (fluid && fluid->sourceId != 0) ? registry.getBlock(fluid->sourceId) : prop;
                emitCube(x, y, z, blockId, prop, renderProp, fluid);
            }
        }
    }

    meshReadyForUpload = true;
}

void ChunkMesh::uploadMesh() {
    if (!meshReadyForUpload) return;

    opaqueVertexCount      = static_cast<GLsizei>(cpuOpaqueVertices.size());
    transparentVertexCount = static_cast<GLsizei>(cpuTransparentVertices.size());

    uploadBuffer(opaqueVAO,      opaqueVBO,      cpuOpaqueVertices);
    uploadBuffer(transparentVAO, transparentVBO, cpuTransparentVertices);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    // The GPU has its own copy now; free the CPU-side vertices
    std::vector<Vertex>().swap(cpuOpaqueVertices);
    std::vector<Vertex>().swap(cpuTransparentVertices);

    meshReadyForUpload = false;
    everUploaded = true;
}

void ChunkMesh::drawOpaque() const {
    if (opaqueVertexCount == 0 || opaqueVAO == 0) return;
    glBindVertexArray(opaqueVAO);
    glDrawArrays(GL_TRIANGLES, 0, opaqueVertexCount);
    glBindVertexArray(0);
}

void ChunkMesh::drawTransparent() const {
    if (transparentVertexCount == 0 || transparentVAO == 0) return;
    glBindVertexArray(transparentVAO);
    glDrawArrays(GL_TRIANGLES, 0, transparentVertexCount);
    glBindVertexArray(0);
}
