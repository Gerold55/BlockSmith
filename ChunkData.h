#pragma once
// A chunk's blocks and metadata - no graphics. Used by the world, saving, world generation
// and (later) the server and networking. The client builds a ChunkMesh from it to draw it.

#include <cstdint>
#include "Types.h"

class ChunkData {
public:
    ChunkData();

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

    // Neighbour slots: 0 = +X, 1 = -X, 4 = +Z, 5 = -Z (2/3 unused). Managed by World.
    ChunkData* neighbors[6];

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
};
