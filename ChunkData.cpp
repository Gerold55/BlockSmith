#include "ChunkData.h"
#include <algorithm>
#include <cstring>

ChunkData::ChunkData() {
    std::memset(blocks, 0, sizeof(blocks));
    std::memset(metadata, 0, sizeof(metadata));
    for (int i = 0; i < 6; ++i) neighbors[i] = nullptr;
}

void ChunkData::setBlock(int x, int y, int z, uint8_t blockId) {
    if (inside(x, y, z)) blocks[x][y][z] = blockId;
}

void ChunkData::setBlockWithMeta(int x, int y, int z, uint8_t blockId, uint8_t meta) {
    if (inside(x, y, z)) {
        blocks[x][y][z] = blockId;
        metadata[x][y][z] = meta;
    }
}

void ChunkData::setMetadata(int x, int y, int z, uint8_t meta) {
    if (inside(x, y, z)) metadata[x][y][z] = meta;
}

uint8_t ChunkData::getLocalBlock(int x, int y, int z) const {
    return inside(x, y, z) ? blocks[x][y][z] : 0;
}

uint8_t ChunkData::getLocalMetadata(int x, int y, int z) const {
    return inside(x, y, z) ? metadata[x][y][z] : 0;
}

// Steps through neighbours until (x, z) is inside a chunk, so diagonal cells
// (e.g. x = -1 and z = -1) work too.
const ChunkData* ChunkData::resolve(int& x, int& z) const {
    const ChunkData* c = this;
    while (c) {
        if (x < 0)                 { x += CHUNK_WIDTH; c = c->neighbors[1]; }
        else if (x >= CHUNK_WIDTH) { x -= CHUNK_WIDTH; c = c->neighbors[0]; }
        else if (z < 0)            { z += CHUNK_WIDTH; c = c->neighbors[5]; }
        else if (z >= CHUNK_WIDTH) { z -= CHUNK_WIDTH; c = c->neighbors[4]; }
        else return c;
    }
    return nullptr;
}

uint8_t ChunkData::getBlock(int x, int y, int z) const {
    if (y < 0 || y >= CHUNK_HEIGHT) return 0;
    const ChunkData* c = resolve(x, z);
    return c ? c->getLocalBlock(x, y, z) : 0;
}

uint8_t ChunkData::getMetadata(int x, int y, int z) const {
    if (y < 0 || y >= CHUNK_HEIGHT) return 0;
    const ChunkData* c = resolve(x, z);
    return c ? c->getLocalMetadata(x, y, z) : 0;
}

void ChunkData::setColumnTint(int x, int z, uint32_t rgb, int channel) {
    if (channel < 0 || channel >= kTintChannels || x < 0 || x >= CHUNK_WIDTH || z < 0 || z >= CHUNK_WIDTH) return;
    tint[channel][x][z] = rgb & 0xFFFFFFu;
    tintSet[channel] = true;
}

uint32_t ChunkData::getColumnTint(int x, int z, int channel) const {
    if (channel < 0 || channel >= kTintChannels) return kNoTint;
    const ChunkData* c = resolve(x, z);
    if (!c || !c->tintSet[channel]) return kNoTint;
    return c->tint[channel][x][z];
}

uint16_t ChunkData::getLight(int x, int y, int z) const {
    if (y >= CHUNK_HEIGHT) return kFullSky;
    if (y < 0) return 0;
    const ChunkData* c = resolve(x, z);
    if (!c || !c->hasLight()) return kFullSky;
    return c->getLocalLight(x, y, z);
}
