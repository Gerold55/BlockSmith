#include "VoxelVolume.h"
#include "Block.h"
#include "BlockShape.h"
#include "ChunkData.h"
#include "Fluids.h"
#include <algorithm>
#include <cmath>

namespace {

inline int floorDivV(int a, int b) { return (a >= 0) ? a / b : -((-a + b - 1) / b); }

// How much light each block stops, worked out once per registry change
uint8_t g_opacity[256];
size_t g_opacityBlocks = 0;

void buildOpacity() {
    auto& reg = BlockRegistry::getInstance();
    const auto& all = reg.getAllBlocks();
    if (g_opacityBlocks == all.size()) return;
    g_opacityBlocks = all.size();
    for (int i = 0; i < 256; ++i) g_opacity[i] = 0;
    for (const BlockProperty& p : all) {
        if (p.id == 0) continue;
        uint8_t o = 0;
        if (const FluidDef* fl = FluidRegistry::get().forBlock(p.id)) {
            // water (and other clear liquids): light gets through, a little dimmer; glowing or
            // thick ones (lava) stop it
            o = (fl->name.find("water") != std::string::npos || (p.isTransparent && p.name.find("lava") == std::string::npos &&
                 p.lightEmit[0] + p.lightEmit[1] + p.lightEmit[2] == 0))
                ? VoxelVolume::kWater : 255;
        }
        else if (p.pattern == "plantlike" || p.pattern == "sign" || p.pattern == "wall_sign") o = 0;
        else if (p.pattern == "door") o = 0;                           // (a thin panel: let light past)
        else if (shape::isPartial(p)) o = 200;                         // slabs, stairs: mostly solid (201 waterlogged: fillColumn)
        else if (p.isCutout) o = VoxelVolume::kFoliage;                          // leaves: holes the light gets through
        else if (p.isTransparent) {
            // glass: clear lets nearly everything through; coloured glass less
            const float f = (p.lightFilter[0] + p.lightFilter[1] + p.lightFilter[2]) / 3.0f;
            o = static_cast<uint8_t>(std::clamp(10.0f + (1.0f - f) * 120.0f, 0.0f, 255.0f));
            if (o == VoxelVolume::kWater) o = VoxelVolume::kWater + 1;   // (that value means water)
        }
        else if (p.pattern == "cube") o = 255;
        else o = 127;                                                  // other shapes: half
        g_opacity[p.id] = o;
    }
}

} // namespace

void VoxelVolume::invalidateOpacity() { g_opacityBlocks = static_cast<size_t>(-1); }

uint8_t VoxelVolume::opacityOf(uint8_t blockId) {
    buildOpacity();
    return g_opacity[blockId];
}

// (No GL calls here: the global volume is destroyed after the window and its GL context are
// gone, and the texture goes with the context anyway)
VoxelVolume::~VoxelVolume() = default;

void VoxelVolume::clear() {
    m_haveOrigin = false;
    m_dirty.clear();
    std::fill(m_cells.begin(), m_cells.end(), 0);
}

void VoxelVolume::markAll() {
    const int cx0 = floorDivV(m_origin.x, CHUNK_WIDTH), cz0 = floorDivV(m_origin.z, CHUNK_WIDTH);
    const int n = kSize / CHUNK_WIDTH;
    for (int i = 0; i < n; ++i)
        for (int k = 0; k < n; ++k) m_dirty.insert({ cx0 + i, cz0 + k });
}

void VoxelVolume::update(const World& world, const glm::vec3& camera, const std::vector<std::pair<int, int>>& changed) {
    if (!m_tex) {
        m_cells.assign(static_cast<size_t>(kSize) * kHeight * kSize, 0);
        glGenTextures(1, &m_tex);
        glBindTexture(GL_TEXTURE_3D, m_tex);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_REPEAT);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage3D(GL_TEXTURE_3D, 0, GL_R8, kSize, kHeight, kSize, 0, GL_RED, GL_UNSIGNED_BYTE, m_cells.data());
        glBindTexture(GL_TEXTURE_3D, 0);
    }
    buildOpacity();

    // Where the volume is: centred on the camera across (on chunk lines); the whole height
    const int cx = floorDivV(static_cast<int>(std::floor(camera.x)), CHUNK_WIDTH) * CHUNK_WIDTH;
    const int cz = floorDivV(static_cast<int>(std::floor(camera.z)), CHUNK_WIDTH) * CHUNK_WIDTH;
    const glm::ivec3 want(cx - kSize / 2, 0, cz - kSize / 2);
    if (!m_haveOrigin) {
        // the first time: every column
        m_origin = want;
        m_haveOrigin = true;
        m_dirty.clear();
        markAll();
    } else if (want != m_origin) {
        // moved sideways: the columns that came into range
        const glm::ivec3 old = m_origin;
        m_origin = want;
        const int n = kSize / CHUNK_WIDTH;
        const int ox0 = floorDivV(old.x, CHUNK_WIDTH), oz0 = floorDivV(old.z, CHUNK_WIDTH);
        const int nx0 = floorDivV(m_origin.x, CHUNK_WIDTH), nz0 = floorDivV(m_origin.z, CHUNK_WIDTH);
        for (int i = 0; i < n; ++i)
            for (int k = 0; k < n; ++k) {
                const int x = nx0 + i, z = nz0 + k;
                if (x < ox0 || x >= ox0 + n || z < oz0 || z >= oz0 + n) m_dirty.insert({ x, z });
            }
        // (columns that left the range are dropped)
        for (auto it = m_dirty.begin(); it != m_dirty.end();) {
            if (it->first < nx0 || it->first >= nx0 + n || it->second < nz0 || it->second >= nz0 + n) it = m_dirty.erase(it);
            else ++it;
        }
    }
    for (const auto& c : changed) m_dirty.insert(c);

    // Fill some columns now, nearest the camera first (the rest next frames)
    if (m_dirty.empty()) return;
    const int pcx = floorDivV(cx, CHUNK_WIDTH), pcz = floorDivV(cz, CHUNK_WIDTH);
    std::vector<std::pair<int, int>> order(m_dirty.begin(), m_dirty.end());
    std::sort(order.begin(), order.end(), [&](const auto& a, const auto& b) {
        const int da = (a.first - pcx) * (a.first - pcx) + (a.second - pcz) * (a.second - pcz);
        const int db = (b.first - pcx) * (b.first - pcx) + (b.second - pcz) * (b.second - pcz);
        return da < db;
    });
    // (a handful a frame normally; many more right after joining or a teleport, so the traced
    // lighting is there within a few frames instead of filling in slowly)
    const int kColumnsPerFrame = order.size() > 16 ? 32 : 8;
    int done = 0;
    for (const auto& c : order) {
        if (done >= kColumnsPerFrame) break;
        if (!world.chunkData(c)) continue;          // not loaded yet: stays to fill later
        fillColumn(world, c.first, c.second);
        m_dirty.erase(c);
        ++done;
    }
    // columns out of range or long unloaded are kept until they load; give up on far ones
}

void VoxelVolume::fillColumn(const World& world, int ccx, int ccz) {
    const ChunkData* data = world.chunkData({ ccx, ccz });
    if (!data) return;
    const int n = kSize / CHUNK_WIDTH;
    const int nx0 = floorDivV(m_origin.x, CHUNK_WIDTH), nz0 = floorDivV(m_origin.z, CHUNK_WIDTH);
    if (ccx < nx0 || ccx >= nx0 + n || ccz < nz0 || ccz >= nz0 + n) return;
    const int baseX = ccx * CHUNK_WIDTH, baseZ = ccz * CHUNK_WIDTH;
    const int tx0 = baseX & (kSize - 1), tz0 = baseZ & (kSize - 1);
    // the texture's layout is x + y*S + z*S*S, each world cell at (cell & 127)
    for (int lz = 0; lz < CHUNK_WIDTH; ++lz) {
        for (int wy = 0; wy < kHeight; ++wy) {
            uint8_t* row = &m_cells[static_cast<size_t>(tx0) + static_cast<size_t>(wy) * kSize +
                                    static_cast<size_t>(tz0 + lz) * kSize * kHeight];
            for (int lx = 0; lx < CHUNK_WIDTH; ++lx) {
                const uint8_t id = data->getLocalBlock(lx, wy, lz);
                uint8_t o = g_opacity[id];
                // a waterlogged slab or stairs: the shaders treat the cell as water too
                if (o == 200 && (data->getLocalMetadata(lx, wy, lz) & shape::kWaterlogged) &&
                    shape::isWaterlogged(BlockRegistry::getInstance().getBlock(id), data->getLocalMetadata(lx, wy, lz)))
                    o = kWaterlogged;
                row[lx] = o;
            }
        }
    }
    // Upload that 16 x 256 x 16 box
    glBindTexture(GL_TEXTURE_3D, m_tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, kSize);
    glPixelStorei(GL_UNPACK_IMAGE_HEIGHT, kHeight);
    glTexSubImage3D(GL_TEXTURE_3D, 0, tx0, 0, tz0, CHUNK_WIDTH, kHeight, CHUNK_WIDTH, GL_RED, GL_UNSIGNED_BYTE,
                    &m_cells[static_cast<size_t>(tx0) + static_cast<size_t>(tz0) * kSize * kHeight]);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glPixelStorei(GL_UNPACK_IMAGE_HEIGHT, 0);
    glBindTexture(GL_TEXTURE_3D, 0);
}

void VoxelVolume::bind(GLuint program, int unit, bool enabled) const {
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_3D, m_tex);
    glActiveTexture(GL_TEXTURE0);
    glUniform1i(glGetUniformLocation(program, "u_Voxels"), unit);
    glUniform3i(glGetUniformLocation(program, "u_VoxOrigin"), m_origin.x, m_origin.y, m_origin.z);
    glUniform1i(glGetUniformLocation(program, "u_VoxEnabled"), (enabled && m_tex && m_haveOrigin) ? 1 : 0);
    // Which chunk columns hold the blocks that are there now (not ones waiting to be filled)
    GLint valid[64];
    const int n = kSize / CHUNK_WIDTH;
    const int nx0 = floorDivV(m_origin.x, CHUNK_WIDTH), nz0 = floorDivV(m_origin.z, CHUNK_WIDTH);
    for (int i = 0; i < 64; ++i) valid[i] = 0;
    for (int i = 0; i < n; ++i)
        for (int k = 0; k < n; ++k) {
            const int cx = nx0 + i, cz = nz0 + k;
            if (m_dirty.count({ cx, cz })) continue;
            valid[(cx & 7) + (cz & 7) * 8] = 1;
        }
    glUniform1iv(glGetUniformLocation(program, "u_VoxColValid"), 64, valid);
}
