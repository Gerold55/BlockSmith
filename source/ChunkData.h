#pragma once
// A chunk's blocks and metadata - no graphics. Used by the world, saving, world generation
// and (later) the server and networking. The client builds a ChunkMesh from it to draw it.

#include <cstdint>
#include <map>
#include <string>
#include <vector>
#include <algorithm>
#include "Types.h"

// Text kept with a block (signs, labels, books on a lectern...): a front and a back side, each
// with its lines, colour and glow, and whether it's locked against editing ("waxed"). Saved with
// the chunk and sent to players with it; it goes when the block there changes.
struct BlockTextSide {
    std::vector<std::string> lines;
    uint32_t color = 0x000000;    // 0xRRGGBB
    bool glowing = false;         // drawn at full brightness, with an outline
    bool empty() const { for (const auto& l : lines) if (!l.empty()) return false; return true; }
};
struct BlockText {
    BlockTextSide front, back;
    bool waxed = false;           // nobody can edit it any more (a mod may still change it)
    static constexpr size_t kMaxLines = 8;
    static constexpr size_t kMaxLineBytes = 96;
};

class ChunkData {
public:
    ChunkData();

    // Terrain versions (World.h): the world generator's terrain_version this chunk was made with,
    // and whether anything but nature changed it since (a player, a bucket, a mod's structure...).
    // A chunk nobody built in, from an older terrain_version, is made again when it loads, so a
    // changed world generator doesn't leave old terrain standing next to new.
    uint16_t terrainVersion = 0;
    bool terrainKnown = false;   // false: saved before this was recorded (always kept as it is)
    bool built = false;

    // Coordinates are local to the chunk (0..15, 0..255, 0..15).
    void setBlock(int x, int y, int z, uint8_t blockId);
    void setBlockWithMeta(int x, int y, int z, uint8_t blockId, uint8_t meta);
    void setMetadata(int x, int y, int z, uint8_t meta);

    // getBlock / getMetadata also look into neighbouring chunks when x or z is just outside
    // this chunk (returns 0 if that neighbour isn't loaded). getLocal* never does.
    uint8_t getBlock(int x, int y, int z) const;
    // Whether (x, z) - possibly in a neighbouring chunk - is loaded, i.e. getBlock there
    // means something (an unloaded chunk reads as air)
    bool hasColumn(int x, int z) const { return resolve(x, z) != nullptr; }
    uint8_t getMetadata(int x, int y, int z) const;
    uint8_t getLocalBlock(int x, int y, int z) const;
    uint8_t getLocalMetadata(int x, int y, int z) const;

    // Biome colours per column (0xRRGGBB), for blocks with biome_tint (Minecraft's grass and
    // foliage colours). Channel 0 = grass, 1 = foliage. Set when the chunk is generated
    // (World::computeBiomeTint); saved and sent to players with the chunk.
    static constexpr int kTintChannels = 2;
    static constexpr uint32_t kNoTint = 0xFFFFFFFFu;
    void setColumnTint(int x, int z, uint32_t rgb, int channel = 0);
    // Looks into neighbouring chunks like getBlock; kNoTint if there's no colour there
    uint32_t getColumnTint(int x, int z, int channel = 0) const;
    bool hasTint(int channel = 0) const { return channel >= 0 && channel < kTintChannels && tintSet[channel]; }

    // ---- Light (see Light.h). Only kept when the world has lighting on (the game, not the
    //      dedicated server). Each cell: 4 bits sunlight, then 4 bits each red, green, blue.
    //      Worked out by the client from the blocks; never saved or sent. ----
    static constexpr uint16_t kFullSky = 0xF000;   // open sky, no block light
    bool hasLight() const { return !light.empty(); }
    void enableLight() { if (light.empty()) light.assign(CHUNK_WIDTH * CHUNK_HEIGHT * CHUNK_WIDTH, 0); }
    void clearLight() { std::fill(light.begin(), light.end(), uint16_t(0)); }
    static int lightIndex(int x, int y, int z) { return (x * CHUNK_HEIGHT + y) * CHUNK_WIDTH + z; }
    uint16_t getLocalLight(int x, int y, int z) const { return light[lightIndex(x, y, z)]; }
    void setLocalLight(int x, int y, int z, uint16_t v) { light[lightIndex(x, y, z)] = v; }
    // Looks into neighbouring chunks like getBlock. Above the world: open sky; below: dark;
    // a chunk that isn't loaded (or has no light): open sky, so borders never go black
    uint16_t getLight(int x, int y, int z) const;

    // ---- Block text (BlockText above), by local position ----
    static uint32_t textKey(int x, int y, int z) { return (static_cast<uint32_t>(y) << 8) | (static_cast<uint32_t>(z) << 4) | static_cast<uint32_t>(x); }
    static void textPos(uint32_t key, int& x, int& y, int& z) { x = key & 15; z = (key >> 4) & 15; y = static_cast<int>(key >> 8); }
    const BlockText* getText(int x, int y, int z) const {
        if (!inside(x, y, z) || texts.empty()) return nullptr;
        const auto it = texts.find(textKey(x, y, z));
        return it == texts.end() ? nullptr : &it->second;
    }
    void setText(int x, int y, int z, const BlockText& t) { if (inside(x, y, z)) texts[textKey(x, y, z)] = t; }
    void clearText(int x, int y, int z) { if (inside(x, y, z) && !texts.empty()) texts.erase(textKey(x, y, z)); }
    const std::map<uint32_t, BlockText>& allTexts() const { return texts; }

    // Neighbour slots: 0 = +X, 1 = -X, 4 = +Z, 5 = -Z (2/3 unused). Managed by World.
    ChunkData* neighbors[6];
    // This chunk's coordinates (set by World when it's added)
    int chunkX = 0, chunkZ = 0;

private:
    static bool inside(int x, int y, int z) {
        return x >= 0 && x < CHUNK_WIDTH && y >= 0 && y < CHUNK_HEIGHT && z >= 0 && z < CHUNK_WIDTH;
    }
    // Resolves an out-of-range x/z to (chunk, local x, local z). Returns nullptr if not loaded.
    const ChunkData* resolve(int& x, int& z) const;

    uint8_t blocks[CHUNK_WIDTH][CHUNK_HEIGHT][CHUNK_WIDTH];
    uint8_t metadata[CHUNK_WIDTH][CHUNK_HEIGHT][CHUNK_WIDTH];
    uint32_t tint[kTintChannels][CHUNK_WIDTH][CHUNK_WIDTH] = {};
    bool tintSet[kTintChannels] = {};
    std::vector<uint16_t> light;   // empty = no light kept
    std::map<uint32_t, BlockText> texts;   // (by textKey)
};
