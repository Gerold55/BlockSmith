#include "Light.h"
#include "ChunkData.h"
#include "Block.h"
#include "Fluids.h"
#include <algorithm>
#include <cmath>
#include <string>

namespace light {

namespace {
BlockLight g_table[256];
bool g_built = false;

bool has(const std::string& s, const char* part) { return s.find(part) != std::string::npos; }

// Light sources for games that didn't say (register_block's `light` wins). Colour, then level.
bool guessEmission(const std::string& name, uint8_t out[3]) {
    struct Guess { const char* part; int level; float r, g, b; };
    static const Guess guesses[] = {
        { "lava",       15, 1.00f, 0.60f, 0.25f },
        { "torch",      14, 1.00f, 0.85f, 0.60f },
        { "lantern",    15, 1.00f, 0.88f, 0.65f },
        { "glowstone",  15, 1.00f, 0.90f, 0.70f },
        { "fire",       15, 1.00f, 0.70f, 0.35f },
        { "sea_lantern",15, 0.80f, 0.95f, 1.00f },
    };
    for (const auto& g : guesses) {
        if (!has(name, g.part)) continue;
        out[0] = static_cast<uint8_t>(std::lround(g.level * g.r));
        out[1] = static_cast<uint8_t>(std::lround(g.level * g.g));
        out[2] = static_cast<uint8_t>(std::lround(g.level * g.b));
        return true;
    }
    return false;
}
} // namespace

const BlockLight& blockLight(uint8_t id) {
    if (!g_built) refreshBlockLights();
    return g_table[id];
}

void invalidateBlockLights() { g_built = false; }

void refreshBlockLights() {
    auto& registry = BlockRegistry::getInstance();
    for (int i = 0; i < 256; ++i) g_table[i] = BlockLight{};
    for (const BlockProperty& p : registry.getAllBlocks()) {
        if (p.id == 0) continue;
        BlockLight& l = g_table[p.id];
        const bool fluid = FluidRegistry::get().forBlock(p.id) != nullptr;
        const bool plant = p.pattern == "plantlike";
        l.opaque = !p.isTransparent && !p.isCutout && !plant && !fluid && p.pattern == "cube";

        if (p.lightAbsorb >= 0) l.absorb = static_cast<uint8_t>(std::min(p.lightAbsorb, 15));
        else if (fluid || (p.isCutout && has(p.name, "leaves"))) l.absorb = 1;   // water, leaves

        for (int c = 0; c < 3; ++c) {
            l.filter[c] = static_cast<uint8_t>(std::lround(std::clamp(p.lightFilter[c], 0.0f, 1.0f) * 15.0f));
        }
        if (p.lightSet) {
            for (int c = 0; c < 3; ++c) l.emit[c] = std::min<uint8_t>(p.lightEmit[c], 15);
        } else {
            guessEmission(p.name, l.emit);
        }
    }
    g_built = true;
}

} // namespace light

using namespace light;

namespace {
// Direction 0..5 = +X, -X, +Y, -Y, +Z, -Z (the same order as block faces)
constexpr int kDir[6][3] = { {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1} };
constexpr int kDown = 3;
}

bool LightEngine::step(ChunkData*& c, int& x, int& y, int& z, int dir) {
    x += kDir[dir][0]; y += kDir[dir][1]; z += kDir[dir][2];
    if (y < 0 || y >= CHUNK_HEIGHT) return false;
    if (x < 0)                 { x += CHUNK_WIDTH; c = c->neighbors[1]; }
    else if (x >= CHUNK_WIDTH) { x -= CHUNK_WIDTH; c = c->neighbors[0]; }
    else if (z < 0)            { z += CHUNK_WIDTH; c = c->neighbors[5]; }
    else if (z >= CHUNK_WIDTH) { z -= CHUNK_WIDTH; c = c->neighbors[4]; }
    return c != nullptr && c->hasLight();
}

// A cell's light changed: its chunk needs redrawing, and so do neighbours whose faces and
// smooth lighting read this cell (cells on a chunk's edge)
void LightEngine::touch(ChunkData* c, int x, int z) {
    m_touched.insert(c);
    const bool w = x == 0, e = x == CHUNK_WIDTH - 1, n = z == 0, s = z == CHUNK_WIDTH - 1;
    if (!(w || e || n || s)) return;
    ChunkData* cx = w ? c->neighbors[1] : e ? c->neighbors[0] : nullptr;
    ChunkData* cz = n ? c->neighbors[5] : s ? c->neighbors[4] : nullptr;
    if (cx) m_touched.insert(cx);
    if (cz) m_touched.insert(cz);
    if (cx && (n || s)) {
        if (ChunkData* d = n ? cx->neighbors[5] : cx->neighbors[4]) m_touched.insert(d);
    }
}

std::vector<ChunkData*> LightEngine::takeTouched() {
    std::vector<ChunkData*> out(m_touched.begin(), m_touched.end());
    m_touched.clear();
    return out;
}

// Light flows outward from every cell in m_add: each step loses 1 (plus the block's
// absorb, if more: water and leaves lose 1 like air, as in Minecraft), sunlight going straight
// down through clear blocks loses nothing
void LightEngine::spread() {
    for (size_t i = 0; i < m_add.size(); ++i) {
        const Node n = m_add[i];
        const uint16_t v = n.c->getLocalLight(n.x, n.y, n.z);
        const int s = sky(v), r = red(v), g = green(v), b = blue(v);
        if (s <= 1 && r <= 1 && g <= 1 && b <= 1) continue;

        for (int dir = 0; dir < 6; ++dir) {
            ChunkData* c = n.c;
            int x = n.x, y = n.y, z = n.z;
            if (!step(c, x, y, z, dir)) continue;
            const BlockLight& bl = blockLight(c->getLocalBlock(x, y, z));
            if (bl.opaque) continue;

            const uint16_t cur = c->getLocalLight(x, y, z);
            int ns = sky(cur), nr = red(cur), ng = green(cur), nb = blue(cur);
            bool changed = false;

            if (s > 1) {
                const int cand = (dir == kDown && s == 15) ? 15 - bl.absorb : s - std::max(1, (int)bl.absorb);
                if (cand > ns) { ns = cand; changed = true; }
            }
            const int in[3] = { r, g, b };
            int* outc[3] = { &nr, &ng, &nb };
            for (int k = 0; k < 3; ++k) {
                if (in[k] <= 1) continue;
                const int cand = (in[k] * bl.filter[k]) / 15 - std::max(1, (int)bl.absorb);
                if (cand > *outc[k]) { *outc[k] = cand; changed = true; }
            }
            if (!changed) continue;
            c->setLocalLight(x, y, z, pack(ns, nr, ng, nb));
            touch(c, x, z);
            m_add.push_back(Node{c, static_cast<int16_t>(x), static_cast<int16_t>(y), static_cast<int16_t>(z)});
        }
    }
    m_add.clear();
}

// Takes away one channel's light at a cell and all the light that came from it. Cells lit
// from elsewhere (as bright or brighter) go into m_add, to flow back in afterwards.
void LightEngine::remove(int ch, ChunkData& start, int sx, int sy, int sz) {
    const uint16_t v0 = start.getLocalLight(sx, sy, sz);
    const int level0 = channel(v0, ch);
    if (level0 == 0) return;
    start.setLocalLight(sx, sy, sz, withChannel(v0, ch, 0));
    touch(&start, sx, sz);
    m_remove.clear();
    m_remove.push_back(RemoveNode{&start, static_cast<int16_t>(sx), static_cast<int16_t>(sy), static_cast<int16_t>(sz),
                                  static_cast<uint8_t>(level0)});

    for (size_t i = 0; i < m_remove.size(); ++i) {
        const RemoveNode n = m_remove[i];
        for (int dir = 0; dir < 6; ++dir) {
            ChunkData* c = n.c;
            int x = n.x, y = n.y, z = n.z;
            if (!step(c, x, y, z, dir)) continue;
            const uint16_t cur = c->getLocalLight(x, y, z);
            const int level = channel(cur, ch);
            if (level == 0) continue;
            const bool skyColumn = ch == 0 && dir == kDown && n.level == 15 && level == 15;
            if (level < n.level || skyColumn) {
                c->setLocalLight(x, y, z, withChannel(cur, ch, 0));
                touch(c, x, z);
                m_remove.push_back(RemoveNode{c, static_cast<int16_t>(x), static_cast<int16_t>(y), static_cast<int16_t>(z),
                                              static_cast<uint8_t>(level)});
                // A light source keeps its own light
                if (ch > 0) {
                    const int own = blockLight(c->getLocalBlock(x, y, z)).emit[ch - 1];
                    if (own > 0) {
                        c->setLocalLight(x, y, z, withChannel(c->getLocalLight(x, y, z), ch, own));
                        m_add.push_back(Node{c, static_cast<int16_t>(x), static_cast<int16_t>(y), static_cast<int16_t>(z)});
                    }
                }
            } else {
                m_add.push_back(Node{c, static_cast<int16_t>(x), static_cast<int16_t>(y), static_cast<int16_t>(z)});
            }
        }
    }
    m_remove.clear();
}

void LightEngine::blockChanged(ChunkData& chunk, int x, int y, int z) {
    if (!chunk.hasLight() || y < 0 || y >= CHUNK_HEIGHT) return;
    for (int ch = 0; ch < 4; ++ch) remove(ch, chunk, x, y, z);

    // The block's own light, then everything around flows back in
    const BlockLight& bl = blockLight(chunk.getLocalBlock(x, y, z));
    if (bl.emits()) {
        chunk.setLocalLight(x, y, z, pack(0, bl.emit[0], bl.emit[1], bl.emit[2]));
        touch(&chunk, x, z);
        m_add.push_back(Node{&chunk, static_cast<int16_t>(x), static_cast<int16_t>(y), static_cast<int16_t>(z)});
    }
    if (!bl.opaque) {
        for (int dir = 0; dir < 6; ++dir) {
            ChunkData* c = &chunk;
            int nx = x, ny = y, nz = z;
            if (step(c, nx, ny, nz, dir)) {
                m_add.push_back(Node{c, static_cast<int16_t>(nx), static_cast<int16_t>(ny), static_cast<int16_t>(nz)});
            }
        }
        if (m_sky && y == CHUNK_HEIGHT - 1) {   // the top of the world is open sky
            chunk.setLocalLight(x, y, z, withChannel(chunk.getLocalLight(x, y, z), 0, std::max(0, 15 - bl.absorb)));
            m_add.push_back(Node{&chunk, static_cast<int16_t>(x), static_cast<int16_t>(y), static_cast<int16_t>(z)});
        }
    }
    spread();
}

void LightEngine::chunkAdded(ChunkData& chunk) {
    refreshBlockLights();   // blocks may have been registered or overridden since
    chunk.enableLight();
    chunk.clearLight();
    m_touched.insert(&chunk);

    // 1. Sunlight straight down each column. surface = the highest block that dims or stops it.
    int surface[CHUNK_WIDTH][CHUNK_WIDTH];
    for (int x = 0; x < CHUNK_WIDTH; ++x) {
        for (int z = 0; z < CHUNK_WIDTH; ++z) {
            int s = m_sky ? 15 : 0;   // (no skylight: the columns start dark)
            surface[x][z] = -1;
            for (int y = CHUNK_HEIGHT - 1; y >= 0; --y) {
                const BlockLight& bl = blockLight(chunk.getLocalBlock(x, y, z));
                if (!bl.clear() && surface[x][z] < 0) surface[x][z] = y;
                // Full sunlight goes straight down losing only what the block absorbs;
                // once dimmed, it loses 1 more per block like any other light
                if (bl.opaque) s = 0;
                else s = (s == 15) ? 15 - bl.absorb : std::max(0, s - std::max(1, (int)bl.absorb));
                if (s == 0 && bl.opaque) {
                    // everything below is dark until something lights it (the rest is 0 already)
                    break;
                }
                chunk.setLocalLight(x, y, z, pack(s, 0, 0, 0));
            }
        }
    }

    // Highest surface around a column (in this chunk or a loaded neighbour): below it,
    // sunlight can spread sideways into overhangs, caves and under trees
    auto surfaceAt = [&](int x, int z) -> int {
        if (x >= 0 && x < CHUNK_WIDTH && z >= 0 && z < CHUNK_WIDTH) return surface[x][z];
        if (!chunk.hasColumn(x, z)) return -1;
        for (int y = CHUNK_HEIGHT - 1; y >= 0; --y) {
            if (!blockLight(chunk.getBlock(x, y, z)).clear()) return y;
        }
        return -1;
    };

    // 2. Sunlit cells next to darker places start spreading
    for (int x = 0; x < CHUNK_WIDTH; ++x) {
        for (int z = 0; z < CHUNK_WIDTH; ++z) {
            int hi = surface[x][z];
            hi = std::max(hi, surfaceAt(x + 1, z));
            hi = std::max(hi, surfaceAt(x - 1, z));
            hi = std::max(hi, surfaceAt(x, z + 1));
            hi = std::max(hi, surfaceAt(x, z - 1));
            for (int y = 0; y <= hi + 1 && y < CHUNK_HEIGHT; ++y) {
                const int s = sky(chunk.getLocalLight(x, y, z));
                if (s <= 1) continue;
                // Only cells with a darker see-through cell beside them have anywhere to spread
                // (straight down was done above)
                bool darker = false;
                for (int dir : { 0, 1, 4, 5 }) {
                    ChunkData* c = &chunk;
                    int nx = x, ny = y, nz = z;
                    if (!step(c, nx, ny, nz, dir)) continue;
                    if (blockLight(c->getLocalBlock(nx, ny, nz)).opaque) continue;
                    if (sky(c->getLocalLight(nx, ny, nz)) < s - 1) { darker = true; break; }
                }
                if (darker) m_add.push_back(Node{&chunk, static_cast<int16_t>(x), static_cast<int16_t>(y), static_cast<int16_t>(z)});
            }
        }
    }

    // 3. Light sources in the chunk
    for (int x = 0; x < CHUNK_WIDTH; ++x) {
        for (int y = 0; y < CHUNK_HEIGHT; ++y) {
            for (int z = 0; z < CHUNK_WIDTH; ++z) {
                const uint8_t id = chunk.getLocalBlock(x, y, z);
                if (id == 0) continue;
                const BlockLight& bl = blockLight(id);
                if (!bl.emits()) continue;
                const uint16_t v = chunk.getLocalLight(x, y, z);
                chunk.setLocalLight(x, y, z, pack(sky(v), bl.emit[0], bl.emit[1], bl.emit[2]));
                m_add.push_back(Node{&chunk, static_cast<int16_t>(x), static_cast<int16_t>(y), static_cast<int16_t>(z)});
            }
        }
    }

    // 4. Light already in the loaded neighbours flows in across the borders
    struct Side { int slot; int x0, x1, z0, z1; };
    const Side sides[4] = {
        { 0, 0, 0, 0, CHUNK_WIDTH - 1 },                              // +X neighbour: its x = 0 column
        { 1, CHUNK_WIDTH - 1, CHUNK_WIDTH - 1, 0, CHUNK_WIDTH - 1 },  // -X: its x = 15
        { 4, 0, CHUNK_WIDTH - 1, 0, 0 },                              // +Z: its z = 0
        { 5, 0, CHUNK_WIDTH - 1, CHUNK_WIDTH - 1, CHUNK_WIDTH - 1 },  // -Z: its z = 15
    };
    for (const Side& sd : sides) {
        ChunkData* n = chunk.neighbors[sd.slot];
        if (!n || !n->hasLight()) continue;
        for (int x = sd.x0; x <= sd.x1; ++x) {
            for (int z = sd.z0; z <= sd.z1; ++z) {
                // The cell on our side of the border
                const int ox = (sd.slot == 0) ? CHUNK_WIDTH - 1 : (sd.slot == 1) ? 0 : x;
                const int oz = (sd.slot == 4) ? CHUNK_WIDTH - 1 : (sd.slot == 5) ? 0 : z;
                for (int y = 0; y < CHUNK_HEIGHT; ++y) {
                    const uint16_t v = n->getLocalLight(x, y, z);
                    if (v == 0) continue;
                    const uint16_t u = chunk.getLocalLight(ox, y, oz);
                    // Only if it would make our side brighter
                    if (sky(v) - 1 > sky(u) || red(v) - 1 > red(u) || green(v) - 1 > green(u) || blue(v) - 1 > blue(u)) {
                        m_add.push_back(Node{n, static_cast<int16_t>(x), static_cast<int16_t>(y), static_cast<int16_t>(z)});
                    }
                }
            }
        }
    }

    spread();
}
