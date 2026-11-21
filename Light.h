#pragma once
// Block lighting: Minecraft-style voxel light with coloured block light.
//
// Every cell of a loaded chunk holds four light levels (0..15): sunlight, and red, green and
// blue block light. Sunlight comes straight down from the sky at full strength and spreads
// sideways, losing 1 per block. Block light spreads out from light sources (torches, lava,
// anything registered with `light`) the same way, each colour on its own, so a red and a blue
// lamp side by side make purple where they overlap. Opaque blocks stop light; water and
// leaves dim sunlight coming straight down (light_absorb); stained glass can colour it (light_filter).
//
// The world calls this when chunks load and blocks change. Only the area a change can reach
// (15 blocks) is recalculated, and every chunk whose light changed is redrawn.
// The game (client) has this on; the dedicated server doesn't need light and keeps it off.

#include <cstdint>
#include <unordered_set>
#include <vector>
#include <glm/glm.hpp>

class ChunkData;

namespace light {

// Packed cell value: bits 12-15 sun, 8-11 red, 4-7 green, 0-3 blue
inline int sky(uint16_t v) { return (v >> 12) & 15; }
inline int red(uint16_t v) { return (v >> 8) & 15; }
inline int green(uint16_t v) { return (v >> 4) & 15; }
inline int blue(uint16_t v) { return v & 15; }
inline int channel(uint16_t v, int c) { return (v >> (12 - 4 * c)) & 15; }   // 0 sky, 1 r, 2 g, 3 b
inline uint16_t withChannel(uint16_t v, int c, int level) {
    const int shift = 12 - 4 * c;
    return static_cast<uint16_t>((v & ~(15 << shift)) | ((level & 15) << shift));
}
inline uint16_t pack(int s, int r, int g, int b) {
    return static_cast<uint16_t>(((s & 15) << 12) | ((r & 15) << 8) | ((g & 15) << 4) | (b & 15));
}

// What a block does to light (worked out from its BlockProperty)
struct BlockLight {
    bool opaque = false;          // stops all light
    uint8_t absorb = 0;           // light lost per block passing through (at least 1; water, leaves = 1)
    uint8_t emit[3] = {0, 0, 0};  // light it gives off (r, g, b)
    uint8_t filter[3] = {15, 15, 15};   // colour let through, in 15ths
    bool clear() const { return !opaque && absorb == 0 && filter[0] == 15 && filter[1] == 15 && filter[2] == 15; }
    bool emits() const { return emit[0] || emit[1] || emit[2]; }
};

// The light table for every block id (rebuilt from the block registry when blocks change)
const BlockLight& blockLight(uint8_t id);
void refreshBlockLights();
// The blocks changed completely (the mods were loaded again): worked out afresh when next needed
void invalidateBlockLights();

} // namespace light

class LightEngine {
public:
    // A chunk just arrived (generated, loaded, or from the server): light it, and let light
    // flow between it and its loaded neighbours
    void chunkAdded(ChunkData& chunk);
    // A block changed at a chunk-local position (light is recalculated around it)
    void blockChanged(ChunkData& chunk, int x, int y, int z);

    // Chunks whose light changed since the last call (they need redrawing)
    std::vector<ChunkData*> takeTouched();
    // Forget a chunk that's being unloaded (so takeTouched never returns it)
    void forget(ChunkData* chunk) { m_touched.erase(chunk); }

private:
    struct Node { ChunkData* c; int16_t x; int16_t y; int16_t z; };
    struct RemoveNode { ChunkData* c; int16_t x; int16_t y; int16_t z; uint8_t level; };

    void spread();                                   // m_add queue -> light flows outward
    void remove(int channel, ChunkData& c, int x, int y, int z);   // -> m_add gets the edges
    void touch(ChunkData* c, int x, int z);
    static bool step(ChunkData*& c, int& x, int& y, int& z, int dir);

    std::vector<Node> m_add;
    std::vector<RemoveNode> m_remove;
    std::unordered_set<ChunkData*> m_touched;
};
