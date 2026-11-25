#include "World.h"
#include "Light.h"
#include "ChunkData.h"
#include "Block.h"
#include "Types.h"
#include "CaveGenerator.h"
#include "ChunkGenerator.h"
#include "Fluids.h"
#include "SafeReplace.h"
#include <algorithm>
#include <chrono>
#include <climits>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <random>
#include <set>
#include <string>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

constexpr int kMinRenderDistance = 2;
constexpr int kMaxRenderDistance = 16;
constexpr int kUnloadMargin = 2;              // chunks beyond render distance before unloading

// Chunk save file format
constexpr uint32_t kChunkMagic   = 0x32435856;  // "VXC2"
constexpr uint16_t kChunkVersion = 1;
// Optional section after the blocks: biome colours ("TINT", then per channel: u8 channel,
// 256 x RGB). Older readers stop after the blocks, so files stay compatible both ways.
constexpr uint32_t kTintMagic    = 0x544E4954;  // "TINT"
// Terrain info: u16 terrain version, u8 built (see ChunkData). Written before the colours, so
// older builds (which read the colours to the end of the file) simply stop at it.
constexpr uint32_t kTerrainMagic = 0x52524554;  // "TERR"
// Block text (signs...): u16 count, then each: u8 x, u8 y, u8 z, u8 flags (1 waxed), and front
// then back: u8 r g b, u8 glowing, u8 lines, { u16 length, bytes }. Before the colours.
constexpr uint32_t kTextMagic    = 0x54584554;  // "TEXT"
constexpr int      kChunkCells   = CHUNK_WIDTH * CHUNK_HEIGHT * CHUNK_WIDTH;

// Neighbor slots match ChunkData::neighbors:
//   0 = +X, 1 = -X, 4 = +Z, 5 = -Z
struct NeighborDir {
    int dx, dz;
    int slot;      // slot on this chunk that points at the neighbor
    int opposite;  // slot on the neighbor that points back at this chunk
};

constexpr NeighborDir kNeighborDirs[] = {
    { 1,  0, 0, 1},
    {-1,  0, 1, 0},
    { 0,  1, 4, 5},
    { 0, -1, 5, 4},
};

inline int floorDiv(int a, int b) {
    int q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0))) --q;
    return q;
}

inline int positiveMod(int a, int b) {
    int m = a % b;
    return m < 0 ? m + b : m;
}

float elapsedMs(Clock::time_point start) {
    return std::chrono::duration<float, std::milli>(Clock::now() - start).count();
}

int chunkDist2(const World::ChunkCoord& a, const World::ChunkCoord& b) {
    const int dx = a.first - b.first;
    const int dz = a.second - b.second;
    return dx * dx + dz * dz;
}

// Little-endian helpers for the chunk file format
void put16(std::vector<uint8_t>& out, uint16_t v) {
    out.push_back(static_cast<uint8_t>(v & 0xFF));
    out.push_back(static_cast<uint8_t>(v >> 8));
}
void put32(std::vector<uint8_t>& out, uint32_t v) {
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<uint8_t>((v >> (8 * i)) & 0xFF));
}
uint16_t get16(const std::vector<uint8_t>& d, size_t p) {
    return static_cast<uint16_t>(d[p] | (d[p + 1] << 8));
}
uint32_t get32(const std::vector<uint8_t>& d, size_t p) {
    return static_cast<uint32_t>(d[p]) | (static_cast<uint32_t>(d[p + 1]) << 8) |
           (static_cast<uint32_t>(d[p + 2]) << 16) | (static_cast<uint32_t>(d[p + 3]) << 24);
}

// Procedural fallback used when the Lua mapgen is missing.
void generateFallbackTerrain(ChunkData& chunk, int chunkX, int chunkZ, float seedPhase) {
    auto& registry = BlockRegistry::getInstance();
    auto idOr = [&registry](const char* name, uint8_t fallback) {
        uint8_t id = registry.getBlockId(name);
        return id != 0 ? id : fallback;
    };
    const uint8_t stoneId = idOr("stone", 2);
    const uint8_t grassId = idOr("grass", 3);
    const uint8_t dirtId  = idOr("dirt", 1);

    for (int x = 0; x < CHUNK_WIDTH; ++x) {
        for (int z = 0; z < CHUNK_WIDTH; ++z) {
            const int worldX = chunkX * CHUNK_WIDTH + x;
            const int worldZ = chunkZ * CHUNK_WIDTH + z;

            const float h = std::sin(worldX * 0.1f + seedPhase) *
                            std::cos(worldZ * 0.1f + seedPhase) * 5.0f;
            const int surface = 55 + static_cast<int>(h);
            const int top = std::min(surface, CHUNK_HEIGHT - 1);

            // Caves are carved afterwards by CaveGenerator::carveChunk (see loadChunk)
            for (int y = 0; y <= top; ++y) {
                if (y == surface) {
                    chunk.setBlock(x, y, z, grassId);
                } else if (y > surface - 4) {
                    chunk.setBlock(x, y, z, dirtId);
                } else {
                    chunk.setBlock(x, y, z, stoneId);
                }
            }
        }
    }
}

} // namespace

World::World() : m_fluids(std::make_unique<FluidSimulator>(*this)) {
    std::random_device rd;
    m_seed = rd() ^ static_cast<uint32_t>(
                 std::chrono::system_clock::now().time_since_epoch().count());
    // (a placeholder: the world being opened or created sets its real seed, and says it)
}

World::~World() = default;

// ---------------------------------------------------------------------------
// Lookup and bookkeeping
// ---------------------------------------------------------------------------

ChunkData* World::chunkAt(const glm::ivec3& loc) const {
    const auto it = chunks.find(ChunkCoord{floorDiv(loc.x, CHUNK_WIDTH), floorDiv(loc.z, CHUNK_WIDTH)});
    return it != chunks.end() ? it->second.get() : nullptr;
}

// Records that a chunk needs redrawing (picked up by WorldRenderer via takeDirtyChunks).
void World::markDirty(int cx, int cz) {
    const ChunkCoord c{cx, cz};
    if (chunks.count(c)) {
        m_dirty.insert(c);
    }
}

// Marks the chunk containing loc, plus any neighbor whose boundary faces depend on it.
void World::markBlockChanged(const glm::ivec3& loc) {
    const int cx = floorDiv(loc.x, CHUNK_WIDTH);
    const int cz = floorDiv(loc.z, CHUNK_WIDTH);
    const int lx = positiveMod(loc.x, CHUNK_WIDTH);
    const int lz = positiveMod(loc.z, CHUNK_WIDTH);

    markDirty(cx, cz);
    if (lx == 0)               markDirty(cx - 1, cz);
    if (lx == CHUNK_WIDTH - 1) markDirty(cx + 1, cz);
    if (lz == 0)               markDirty(cx, cz - 1);
    if (lz == CHUNK_WIDTH - 1) markDirty(cx, cz + 1);
    // Corner blocks: the diagonal chunk's fluid surface corners average this block too
    if (lx == 0 && lz == 0)                             markDirty(cx - 1, cz - 1);
    if (lx == 0 && lz == CHUNK_WIDTH - 1)               markDirty(cx - 1, cz + 1);
    if (lx == CHUNK_WIDTH - 1 && lz == 0)               markDirty(cx + 1, cz - 1);
    if (lx == CHUNK_WIDTH - 1 && lz == CHUNK_WIDTH - 1) markDirty(cx + 1, cz + 1);
}

// Remembers that this chunk differs from its saved/generated state.
namespace { thread_local int t_naturalDepth = 0; }
World::NaturalChanges::NaturalChanges() { ++t_naturalDepth; }
World::NaturalChanges::~NaturalChanges() { --t_naturalDepth; }
bool World::naturalChange() { return t_naturalDepth > 0; }

// A saved chunk from another terrain version that nobody built in (see ChunkData::built)
bool World::outdatedTerrain(const ChunkData& c) const {
    return m_terrainVersion > 0 && c.terrainKnown && !c.built && c.terrainVersion != m_terrainVersion;
}

// Its file goes (there's nothing in it anyone made); the chunk is generated afresh
void World::forgetOutdatedChunk(const ChunkCoord& c) {
    std::error_code ec;
    std::filesystem::remove(chunkPath(c.first, c.second), ec);
    m_notOnDisk.insert(c);
    static int told = 0;
    if (told++ < 3) {
        std::cout << "[World] Chunk " << c.first << "," << c.second << " was made by an older version of the world "
                  << "generator and nobody built in it: making it again" << (told == 3 ? " (not saying again)" : "") << std::endl;
    }
}

void World::markModified(const glm::ivec3& loc) {
    m_modified.insert(ChunkCoord{floorDiv(loc.x, CHUNK_WIDTH), floorDiv(loc.z, CHUNK_WIDTH)});
    // built in, unless nature did it (see NaturalChanges)
    if (!naturalChange()) {
        if (ChunkData* c = chunkAt(loc)) c->built = true;
    }
}

void World::linkChunk(int cx, int cz) {
    auto selfIt = chunks.find(ChunkCoord{cx, cz});
    if (selfIt == chunks.end()) return;
    ChunkData* self = selfIt->second.get();

    for (const auto& d : kNeighborDirs) {
        auto nIt = chunks.find(ChunkCoord{cx + d.dx, cz + d.dz});
        ChunkData* n = (nIt != chunks.end()) ? nIt->second.get() : nullptr;
        self->neighbors[d.slot] = n;
        if (n) n->neighbors[d.opposite] = self;
    }
}

void World::unlinkChunk(int cx, int cz) {
    for (const auto& d : kNeighborDirs) {
        const int nx = cx + d.dx;
        const int nz = cz + d.dz;
        auto nIt = chunks.find(ChunkCoord{nx, nz});
        if (nIt != chunks.end()) {
            nIt->second->neighbors[d.opposite] = nullptr;
            markDirty(nx, nz);
        }
    }
}

void World::setRenderDistance(int chunks) {
    const int clamped = std::clamp(chunks, kMinRenderDistance, kMaxRenderDistance);
    if (clamped == m_renderDistance) return;
    m_renderDistance = clamped;
    m_queueDirty = true;
}

// ---------------------------------------------------------------------------
// Per-frame update: queue, load, unload, mesh
// ---------------------------------------------------------------------------

void World::ensureLuaSeeded(sol::state& lua) {
    if (m_luaSeeded) return;
    if (!m_dimension.empty()) {
        // Another dimension: its generator gets the seed with each chunk, and its terrain
        // version is the one its dimensions.register gave (the overworld's hooks aren't its own)
        m_terrainVersion = static_cast<uint16_t>(std::clamp(m_dimType.terrainVersion, 0, 65535));
        m_luaSeeded = true;
        return;
    }
    CaveGenerator::setSeed(m_seed);
    if (m_generator) CaveGenerator::prepare();   // before the threads carve caves
    // The world generator's terrain version (its global terrain_version, if it has one)
    {
        sol::object v = lua["terrain_version"];
        m_terrainVersion = (v.get_type() == sol::type::number)
                         ? static_cast<uint16_t>(std::clamp(v.as<double>(), 0.0, 65535.0)) : 0;
    }
    sol::protected_function setSeed = lua["set_world_seed"];
    if (setSeed.valid()) {
        auto result = setSeed(m_seed);
        if (!result.valid()) {
            sol::error err = result;
            std::cerr << "[Mapgen Error]: " << err.what() << std::endl;
        }
    }
    m_luaSeeded = true;
}

// Missing chunks inside the render radius of the player (and of other players when
// hosting), nearest to any of them first.
void World::rebuildLoadQueue() {
    m_loadQueue.clear();

    std::vector<CenterChunk> centers{{m_playerChunk, m_renderDistance}};
    centers.insert(centers.end(), m_extraCenters.begin(), m_extraCenters.end());

    std::unordered_set<ChunkCoord, PairHash> seen;
    for (const auto& center : centers) {
        const int r = center.radius;
        for (int dx = -r; dx <= r; ++dx) {
            for (int dz = -r; dz <= r; ++dz) {
                const ChunkCoord c{center.chunk.first + dx, center.chunk.second + dz};
                if (chunks.find(c) == chunks.end() && seen.insert(c).second) {
                    m_loadQueue.push_back(c);
                }
            }
        }
    }

    for (const auto& m : m_moves) {
        if (m.visualOnly) continue;
        const glm::ivec3 dst = m.from + m.offset;
        const glm::ivec3 end = dst + m.region.size - glm::ivec3(1);
        const int minX = floorDiv(dst.x, CHUNK_WIDTH), maxX = floorDiv(end.x, CHUNK_WIDTH);
        const int minZ = floorDiv(dst.z, CHUNK_WIDTH), maxZ = floorDiv(end.z, CHUNK_WIDTH);
        for (int cx = minX; cx <= maxX; ++cx)
            for (int cz = minZ; cz <= maxZ; ++cz) {
                const ChunkCoord c{ cx, cz };
                if (chunks.find(c) == chunks.end() && seen.insert(c).second)
                    m_loadQueue.push_back(c);
            }
    }

    auto nearest = [&centers](const ChunkCoord& c) {
        int best = INT_MAX;
        for (const auto& center : centers) best = std::min(best, chunkDist2(c, center.chunk));
        return best;
    };
    std::sort(m_loadQueue.begin(), m_loadQueue.end(),
              [&nearest](const ChunkCoord& a, const ChunkCoord& b) { return nearest(a) < nearest(b); });
}

void World::setExtraLoadCenters(const std::vector<LoadCenter>& centers) {
    std::vector<CenterChunk> next;
    next.reserve(centers.size());
    for (const auto& c : centers) {
        next.push_back({ChunkCoord{floorDiv(static_cast<int>(std::floor(c.pos.x)), CHUNK_WIDTH),
                                   floorDiv(static_cast<int>(std::floor(c.pos.z)), CHUNK_WIDTH)},
                        std::clamp(c.radius, kMinRenderDistance, kMaxRenderDistance)});
    }
    const bool same = next.size() == m_extraCenters.size() &&
        std::equal(next.begin(), next.end(), m_extraCenters.begin(),
                   [](const CenterChunk& a, const CenterChunk& b) {
                       return a.chunk == b.chunk && a.radius == b.radius;
                   });
    if (!same) {
        m_extraCenters = std::move(next);
        m_queueDirty = true;
    }
}

void World::loadQueuedChunks(sol::state& lua) {
    const auto start = Clock::now();
    size_t consumed = 0;

    for (auto it = m_diskRetry.begin(); it != m_diskRetry.end();) {   // player went away: forget them
        if (!wantedChunk(it->first)) it = m_diskRetry.erase(it);
        else ++it;
    }

    if (m_generator) {
        // Saved chunks are read here (fast); new ones are handed to the generator's threads,
        // nearest first, a few at a time so the closest are always next in line.
        collectGenerated();
        const size_t maxInFlight = static_cast<size_t>(m_generator->threadCount()) * 3;
        std::vector<ChunkCoord> notYet;   // waiting for a generator slot or a disk retry (kept, in queue order)
        // A chunk the generator must make: ask now if a slot is free, else keep it for later
        // (While set_world_seed is failing - or has failed for good - new terrain waits; the
        //  chunk stays queued, and chunks with a save file still load below.)
        const bool generationHeld = !m_fatalError.empty() || Clock::now() < m_genHoldUntil;
        auto generate = [&](const ChunkCoord& c) {
            if (!generationHeld && m_generating.size() < maxInFlight) {
                m_generator->request(c.first, c.second, m_resetCount, m_seed, m_dimension, m_dimType.caves);
                m_generating.insert(c);
            } else {
                notYet.push_back(c);
            }
        };
        while (consumed < m_loadQueue.size()) {
            // The time budget holds on every path (disk loads, missing files, a busy generator).
            // At least one chunk is handled per update; everything not reached stays queued,
            // untouched - no allocation and no disk access for it.
            if (consumed > 0 && elapsedMs(start) >= m_loadBudgetMs) break;
            const ChunkCoord c = m_loadQueue[consumed++];
            if (chunks.count(c) || m_generating.count(c)) continue;

            // Its file couldn't be checked or read lately: wait for the next try (kept queued)
            if (const auto r = m_diskRetry.find(c); r != m_diskRetry.end() && Clock::now() < r->second.next) {
                notYet.push_back(c);
                continue;
            }

            // Already known to have no file: straight to the generator (no probe, no allocation)
            if (m_notOnDisk.count(c)) { generate(c); continue; }

            // Cheap existence check first; ChunkData is only allocated to read a real file
            std::error_code ec;
            const ChunkFile file = chunkFileState(c, ec);
            if (file == ChunkFile::Missing) {           // confirmed: never saved
                m_diskRetry.erase(c);
                m_notOnDisk.insert(c);
                generate(c);
                continue;
            }
            if (file == ChunkFile::Error) {             // unknown: maybe saved - don't overwrite it
                noteDiskError(c, "couldn't check its save file (" + ec.message() + ")");
                notYet.push_back(c);
                continue;
            }
            auto chunk = std::make_shared<ChunkData>();
            std::string why;
            switch (loadChunkFromDisk(c.first, c.second, *chunk, &why)) {
            case DiskLoad::Loaded:
                if (outdatedTerrain(*chunk)) {          // old terrain nobody built in: made again
                    forgetOutdatedChunk(c);
                    generate(c);
                    break;
                }
                computeBiomeTint(*chunk, c.first, c.second, &lua);   // saved before biome colours
                addLoadedChunk(c.first, c.second, std::move(chunk), true);
                break;
            case DiskLoad::Missing:                     // deleted since the check: as never saved
            case DiskLoad::Damaged:                     // regenerate (it's replaced when saved)
                m_diskRetry.erase(c);
                m_notOnDisk.insert(c);
                generate(c);
                break;
            case DiskLoad::Error:
                noteDiskError(c, why);
                notYet.push_back(c);
                break;
            }
        }
        // Waiting ones go back in front, then everything not reached: no coordinate is lost
        m_loadQueue.erase(m_loadQueue.begin(), m_loadQueue.begin() + static_cast<std::ptrdiff_t>(consumed));
        m_loadQueue.insert(m_loadQueue.begin(), notYet.begin(), notYet.end());
        return;
    }

    std::vector<ChunkCoord> retryLater;   // file couldn't be checked or read: kept queued
    while (consumed < m_loadQueue.size()) {
        const ChunkCoord c = m_loadQueue[consumed++];
        if (chunks.find(c) == chunks.end()) {
            if (const auto r = m_diskRetry.find(c); r != m_diskRetry.end() && Clock::now() < r->second.next) {
                retryLater.push_back(c);
                continue;
            }
            if (!loadChunk(c.first, c.second, lua)) retryLater.push_back(c);
            if (elapsedMs(start) >= m_loadBudgetMs) break;
        }
    }

    m_loadQueue.erase(m_loadQueue.begin(),
                      m_loadQueue.begin() + static_cast<std::ptrdiff_t>(consumed));
    m_loadQueue.insert(m_loadQueue.begin(), retryLater.begin(), retryLater.end());
}

// Finished chunks from the generator's threads: keep the ones still needed.
void World::collectGenerated() {
    for (auto& r : m_generator->takeFinished(m_dimension)) {
        const ChunkCoord c{r.cx, r.cz};
        if (r.epoch != m_resetCount) continue;          // from before a reset (another world)
        m_generating.erase(c);
        if (!r.chunk) {                                 // the job failed: ask for it again later
            m_queueDirty = true;                        // (the rebuilt queue includes it)
            if (!r.seedError.empty()) noteSeedFailure(r.seedError);
            continue;
        }
        m_seedFailures = 0;                             // the generator works (again)
        if (chunks.count(c) || !wantedChunk(c)) continue;   // already there / player went away
        addLoadedChunk(r.cx, r.cz, std::move(r.chunk));
    }
    // Requests for places nobody is near any more: drop them if they haven't started
    for (auto it = m_generating.begin(); it != m_generating.end();) {
        if (!wantedChunk(*it) && m_generator->cancel(it->first, it->second, m_dimension)) it = m_generating.erase(it);
        else ++it;
    }
}

// set_world_seed failed in a generator thread. Every job in flight fails the same way at about
// the same time, so only the first failure after a hold counts as an attempt.
void World::noteSeedFailure(const std::string& error) {
    if (!m_fatalError.empty()) return;
    const auto now = Clock::now();
    if (m_seedFailures > 0 && now < m_genHoldUntil) return;   // part of the attempt already counted
    ++m_seedFailures;
    if (m_seedFailures >= kMaxSeedAttempts) {
        m_fatalError = "The world generator failed (set_world_seed): " + error;
        std::cerr << "[World] " << m_fatalError << " - giving up after " << m_seedFailures << " attempts" << std::endl;
        if (m_generator) m_generator->clearQueue(m_dimension);
        return;
    }
    const int shift = std::min(m_seedFailures - 1, 4);          // 0.5 s, 1 s, 2 s, 4 s, 8 s
    m_genHoldUntil = now + std::chrono::milliseconds(500 << shift);
    std::cerr << "[World] set_world_seed failed (attempt " << m_seedFailures << " of " << kMaxSeedAttempts
              << "), trying again in " << (500 << shift) / 1000.0 << " s: " << error << std::endl;
}

void World::addLoadedChunk(int cx, int cz, std::shared_ptr<ChunkData> chunk, bool fromDisk) {
    if (!m_remote) m_justLoaded.push_back({ChunkCoord{cx, cz}, fromDisk});
    m_notOnDisk.erase(ChunkCoord{cx, cz});   // loaded now: it may be saved, so probe again next time
    m_diskRetry.erase(ChunkCoord{cx, cz});
    chunk->chunkX = cx;
    chunk->chunkZ = cz;
    if (!fromDisk) {   // just made: by this terrain version, and nobody has built in it yet
        chunk->terrainVersion = m_terrainVersion;
        chunk->terrainKnown = true;
        chunk->built = false;
    }
    ChunkData& added = *chunk;
    chunks[ChunkCoord{cx, cz}] = std::move(chunk);
    linkChunk(cx, cz);
    lightChunkAdded(added);
    // The new chunk and its neighbors need (re)meshing so border faces cull correctly.
    markDirty(cx, cz);
    markDirty(cx - 1, cz);
    markDirty(cx + 1, cz);
    markDirty(cx, cz - 1);
    markDirty(cx, cz + 1);
    // Diagonal chunks too: fluid surface corners at chunk corners look into this chunk
    markDirty(cx - 1, cz - 1);
    markDirty(cx + 1, cz - 1);
    markDirty(cx - 1, cz + 1);
    markDirty(cx + 1, cz + 1);
}

void World::update(const glm::vec3& playerPos, sol::state& lua) {
    if (m_remote) return;   // the server decides which chunks we have
    ensureLuaSeeded(lua);

    const int pcx = floorDiv(static_cast<int>(std::floor(playerPos.x)), CHUNK_WIDTH);
    const int pcz = floorDiv(static_cast<int>(std::floor(playerPos.z)), CHUNK_WIDTH);
    const ChunkCoord playerChunk{pcx, pcz};

    if (m_queueDirty || playerChunk != m_playerChunk) {
        m_playerChunk = playerChunk;
        m_queueDirty = false;
        rebuildLoadQueue();
    }

    loadQueuedChunks(lua);
    unloadDistantChunks();
}

// Unloads chunks that are too far from the player and from every other load center.
bool World::wantedChunk(const ChunkCoord& c) const {
    auto isNear = [&c](const ChunkCoord& center, int radius) {
        const int dx = c.first - center.first;
        const int dz = c.second - center.second;
        return std::max(std::abs(dx), std::abs(dz)) <= radius + kUnloadMargin;
    };
    if (isNear(m_playerChunk, m_renderDistance)) return true;
    for (const auto& center : m_extraCenters) {
        if (isNear(center.chunk, center.radius)) return true;
    }
    for (const auto& m : m_moves) {
        if (m.visualOnly) continue;
        const glm::ivec3 dst = m.from + m.offset;
        const glm::ivec3 end = dst + m.region.size - glm::ivec3(1);
        if (c.first >= floorDiv(dst.x, CHUNK_WIDTH) && c.first <= floorDiv(end.x, CHUNK_WIDTH) &&
            c.second >= floorDiv(dst.z, CHUNK_WIDTH) && c.second <= floorDiv(end.z, CHUNK_WIDTH)) return true;
    }
    return false;
}

void World::unloadDistantChunks() {
    auto wanted = [this](const ChunkCoord& c) { return wantedChunk(c); };

    for (auto it = chunks.begin(); it != chunks.end();) {
        const int cx = it->first.first;
        const int cz = it->first.second;

        if (!wanted(it->first)) {
            // Keep player changes: write the chunk out before throwing it away
            if (m_modified.count(it->first)) {
                if (!saveChunkToDisk(it->first, *it->second)) {
                    // Save failed: retain the chunk in memory and keep m_modified intact for retry
                    ++it;
                    continue;
                }
                m_modified.erase(it->first);
            }

            unlinkChunk(cx, cz);                 // clear neighbor pointers before destroying
            if (m_light) m_light->forget(it->second.get());   // the light engine holds chunk pointers
            m_dirty.erase(it->first);
            m_unloaded.push_back(it->first);
            it = chunks.erase(it);
        } else {
            ++it;
        }
    }
}

bool World::loadChunk(int chunkX, int chunkZ, sol::state& lua) {
    auto chunk = std::make_shared<ChunkData>();
    const ChunkCoord c{chunkX, chunkZ};

    // 1. Saved chunk on disk? Use it instead of generating.
    std::string why;
    const DiskLoad disk = loadChunkFromDisk(chunkX, chunkZ, *chunk, &why);
    if (disk == DiskLoad::Error) {                 // maybe saved: never replace it with new terrain
        noteDiskError(c, why);
        return false;
    }
    m_diskRetry.erase(c);
    bool fromDisk = disk == DiskLoad::Loaded;
    if (fromDisk && outdatedTerrain(*chunk)) {     // old terrain nobody built in: made again
        forgetOutdatedChunk(c);
        fromDisk = false;
    }

    // 2. Otherwise generate it (Lua mapgen, or the C++ fallback).
    if (!fromDisk) {
        chunk = std::make_shared<ChunkData>();   // fresh, in case a broken file half-filled it
        generateNewChunk(*chunk, chunkX, chunkZ, &lua, m_seed, m_dimension, m_dimType.caves);
    } else {
        computeBiomeTint(*chunk, chunkX, chunkZ, &lua);   // saved before biome colours
    }
    addLoadedChunk(chunkX, chunkZ, std::move(chunk), fromDisk);
    return true;
}

// Biome colours from the game's Lua: get_grass_color(x, z) and get_foliage_color(x, z) return
// r, g, b (0..255, or 0..1). Asked on a 5 x 5 grid (every 4 blocks; the last row is the next
// chunk's first, so colours match across chunk edges) and blended in between: 25 calls a
// chunk instead of 256, and smooth changes between biomes.
void World::computeBiomeTint(ChunkData& chunk, int cx, int cz, sol::state* lua) {
    if (!lua) return;
    const char* hooks[ChunkData::kTintChannels] = { "get_grass_color", "get_foliage_color" };
    for (int c = 0; c < ChunkData::kTintChannels; ++c) {
        if (chunk.hasTint(c)) continue;                 // the mapgen set them itself
        sol::protected_function fn = (*lua)[hooks[c]];
        if (!fn.valid()) continue;
        float grid[5][5][3];
        bool ok = true;
        for (int gx = 0; gx < 5 && ok; ++gx) {
            for (int gz = 0; gz < 5 && ok; ++gz) {
                auto r = fn(cx * CHUNK_WIDTH + gx * 4, cz * CHUNK_WIDTH + gz * 4);
                if (!r.valid()) {
                    sol::error err = r;
                    std::cerr << "[Mapgen Error] " << hooks[c] << ": " << err.what() << std::endl;
                    ok = false;
                    break;
                }
                float rgb[3] = { 1.0f, 1.0f, 1.0f };
                if (r.return_count() >= 3) {
                    for (int k = 0; k < 3; ++k) rgb[k] = r.get<float>(k);
                } else if (r.return_count() == 1 && r.get_type() == sol::type::table) {
                    sol::table t = r.get<sol::table>();
                    for (int k = 0; k < 3; ++k) rgb[k] = t.get_or(k + 1, 1.0f);
                } else {
                    ok = false;
                    break;
                }
                const bool bytes = rgb[0] > 1.0f || rgb[1] > 1.0f || rgb[2] > 1.0f;
                for (int k = 0; k < 3; ++k) grid[gx][gz][k] = std::clamp(bytes ? rgb[k] / 255.0f : rgb[k], 0.0f, 1.0f);
            }
        }
        if (!ok) continue;
        for (int x = 0; x < CHUNK_WIDTH; ++x) {
            for (int z = 0; z < CHUNK_WIDTH; ++z) {
                const int gx = x / 4, gz = z / 4;
                const float fx = (x % 4) / 4.0f, fz = (z % 4) / 4.0f;
                uint32_t packed = 0;
                for (int k = 0; k < 3; ++k) {
                    const float v = (grid[gx][gz][k] * (1 - fx) + grid[gx + 1][gz][k] * fx) * (1 - fz)
                                  + (grid[gx][gz + 1][k] * (1 - fx) + grid[gx + 1][gz + 1][k] * fx) * fz;
                    packed = (packed << 8) | static_cast<uint32_t>(std::lround(v * 255.0f));
                }
                chunk.setColumnTint(x, z, packed, c);
            }
        }
    }
}

void World::generateNewChunk(ChunkData& chunk, int cx, int cz, sol::state* lua, uint32_t seed,
                             const std::string& dimension, bool caves) {
    sol::protected_function genFunc;
    if (dimension.empty() || dimension == "overworld") {
        if (lua) genFunc = (*lua)["generate_chunk"];
        if (lua && genFunc.valid()) {
            auto result = genFunc(&chunk, cx, cz);
            if (!result.valid()) {
                sol::error err = result;
                std::cerr << "[Mapgen Error]: " << err.what() << std::endl;
            }
        } else {
            generateFallbackTerrain(chunk, cx, cz, static_cast<float>(seed % 100000) * 0.001f);
        }
    } else if (lua) {
        // Another dimension: the generator its mod gave dimensions.register (kept by name in
        // _dimension_generators in every Lua state). None: the chunk stays empty (a void).
        sol::object gens = (*lua)["_dimension_generators"];
        if (gens.get_type() == sol::type::table) {
            sol::protected_function fn = gens.as<sol::table>()[dimension];
            if (fn.valid()) {
                auto result = fn(&chunk, cx, cz, seed);
                if (!result.valid()) {
                    sol::error err = result;
                    std::cerr << "[Mapgen Error] (" << dimension << "): " << err.what() << std::endl;
                }
            }
        }
    }
    // Carve caves into the new terrain (seeded, so the same seed gives the same caves).
    // Saved chunks skip this - their caves are already in the file.
    if (caves) CaveGenerator::carveChunk(chunk, cx, cz);
    computeBiomeTint(chunk, cx, cz, lua);
}

// ---------------------------------------------------------------------------
// Saving / loading
// ---------------------------------------------------------------------------

void World::reset() {
    if (m_light) m_light->takeTouched();   // forget chunk pointers before the chunks go
    chunks.clear();
    m_loadQueue.clear();
    m_dirty.clear();
    m_unloaded.clear();
    m_justLoaded.clear();
    m_generating.clear();   // results still on their way carry the old reset count: ignored
    if (m_generator) m_generator->clearQueue(m_dimension);
    ++m_resetCount;
    m_remote = false;
    m_recordChanges = false;
    m_changes.clear();
    m_textChanges.clear();
    m_extraCenters.clear();
    m_fluids->clear();
    m_modified.clear();
    m_notOnDisk.clear();
    m_diskRetry.clear();
    m_seedFailures = 0;
    m_genHoldUntil = {};
    m_fatalError.clear();
    m_fallChecks.clear();   // blocks falling in the old world
    m_falling.clear();
    m_fallingStarts.clear();
    m_queueDirty = true;
    m_luaSeeded = false;
    m_playerChunk = ChunkCoord{0, 0};
}

int World::saveAll() {
    if (m_saveDir.empty()) return 0;

    int written = 0;
    for (auto it = m_modified.begin(); it != m_modified.end();) {
        auto cIt = chunks.find(*it);
        if (cIt != chunks.end() && saveChunkToDisk(*it, *cIt->second)) {
            ++written;
            it = m_modified.erase(it);
        } else if (cIt == chunks.end()) {
            it = m_modified.erase(it);   // already unloaded (and saved then)
        } else {
            ++it;                        // write failed - keep it for the next try
        }
    }
    return written;
}

World::ChunkFile World::chunkFileState(const ChunkCoord& c, std::error_code& ec) const {
    ec.clear();
    if (m_saveDir.empty()) return ChunkFile::Missing;          // not saving: nothing is ever on disk
    const std::filesystem::file_status st = std::filesystem::status(chunkPath(c.first, c.second), ec);
    // A path that doesn't exist (or whose folder doesn't: "chunks/" not made yet, or a file in
    // its place) is a confirmed absence, not an error.
    if (st.type() == std::filesystem::file_type::not_found ||
        ec == std::errc::no_such_file_or_directory || ec == std::errc::not_a_directory) {
        ec.clear();
        return ChunkFile::Missing;
    }
    if (ec) return ChunkFile::Error;                           // couldn't look: unknown
    if (std::filesystem::is_regular_file(st)) return ChunkFile::Present;
    return ChunkFile::Missing;   // something that isn't a file (a folder...): not a saved chunk, as before
}

void World::noteDiskError(const ChunkCoord& c, const std::string& what) {
    DiskRetry& r = m_diskRetry[c];
    ++r.failures;
    // 0.25 s, 0.5 s, 1 s, 2 s, then every 4 s while it lasts
    const int shift = std::min(r.failures - 1, 4);
    r.next = Clock::now() + std::chrono::milliseconds(250 << shift);
    if (r.failures == 1 || r.failures % 15 == 0) {             // not every attempt
        std::cerr << "[Load] Chunk " << c.first << "," << c.second << ": " << what
                  << " - not generating it (it may be saved); trying again"
                  << (r.failures > 1 ? " (" + std::to_string(r.failures) + " attempts so far)" : std::string())
                  << std::endl;
    }
}

std::string World::chunkPath(int cx, int cz) const {
    const std::string name = "c." + std::to_string(cx) + "." + std::to_string(cz) + ".bin";
    return (std::filesystem::path(m_saveDir) / "chunks" / name).string();
}

// File layout (little-endian):
//   u32 magic, u16 version, u16 width, u16 height
//   u16 paletteCount, then per entry: u8 id, u8 nameLength, name bytes
//   runs until the chunk is full: u16 count, u8 blockId, u8 metadata
// Cells are stored y-major, then z, then x.
// The palette stores block NAMES, so saves still load correctly if mods
// are added/removed and block ids shift.
std::vector<uint8_t> World::encodeChunk(const ChunkData& chunk) {
    auto& registry = BlockRegistry::getInstance();

    // Run-length encode (id, meta) pairs
    std::vector<uint8_t> runs;
    runs.reserve(4096);
    bool used[256] = {};

    uint16_t runLen = 0;
    uint8_t runId = 0, runMeta = 0;
    auto flush = [&]() {
        if (runLen == 0) return;
        put16(runs, runLen);
        runs.push_back(runId);
        runs.push_back(runMeta);
    };

    for (int y = 0; y < CHUNK_HEIGHT; ++y) {
        for (int z = 0; z < CHUNK_WIDTH; ++z) {
            for (int x = 0; x < CHUNK_WIDTH; ++x) {
                const uint8_t id = chunk.getLocalBlock(x, y, z);
                const uint8_t meta = chunk.getLocalMetadata(x, y, z);
                used[id] = true;
                if (runLen > 0 && id == runId && meta == runMeta && runLen < 0xFFFF) {
                    ++runLen;
                } else {
                    flush();
                    runId = id;
                    runMeta = meta;
                    runLen = 1;
                }
            }
        }
    }
    flush();

    // Header + palette
    std::vector<uint8_t> out;
    out.reserve(runs.size() + 512);
    put32(out, kChunkMagic);
    put16(out, kChunkVersion);
    put16(out, static_cast<uint16_t>(CHUNK_WIDTH));
    put16(out, static_cast<uint16_t>(CHUNK_HEIGHT));

    std::vector<uint8_t> paletteIds;
    for (int id = 1; id < 256; ++id) if (used[id]) paletteIds.push_back(static_cast<uint8_t>(id));
    put16(out, static_cast<uint16_t>(paletteIds.size()));
    for (uint8_t id : paletteIds) {
        std::string name = registry.getBlock(id).name;
        if (name.size() > 255) name.resize(255);
        out.push_back(id);
        out.push_back(static_cast<uint8_t>(name.size()));
        out.insert(out.end(), name.begin(), name.end());
    }
    out.insert(out.end(), runs.begin(), runs.end());

    // Terrain info (which world generator version made it, and whether anyone built in it)
    if (chunk.terrainKnown) {
        put32(out, kTerrainMagic);
        put16(out, chunk.terrainVersion);
        out.push_back(chunk.built ? 1 : 0);
    }

    // Block text (signs)
    if (!chunk.allTexts().empty()) {
        put32(out, kTextMagic);
        put16(out, static_cast<uint16_t>(std::min<size_t>(chunk.allTexts().size(), 0xFFFF)));
        size_t n = 0;
        for (const auto& [key, t] : chunk.allTexts()) {
            if (++n > 0xFFFF) break;
            int x, y, z;
            ChunkData::textPos(key, x, y, z);
            out.push_back(static_cast<uint8_t>(x)); out.push_back(static_cast<uint8_t>(y)); out.push_back(static_cast<uint8_t>(z));
            out.push_back(t.waxed ? 1 : 0);
            for (const BlockTextSide* side : { &t.front, &t.back }) {
                out.push_back(static_cast<uint8_t>(side->color >> 16));
                out.push_back(static_cast<uint8_t>(side->color >> 8));
                out.push_back(static_cast<uint8_t>(side->color));
                out.push_back(side->glowing ? 1 : 0);
                const size_t lines = std::min(side->lines.size(), BlockText::kMaxLines);
                out.push_back(static_cast<uint8_t>(lines));
                for (size_t i = 0; i < lines; ++i) {
                    const std::string& l = side->lines[i];
                    const size_t len = std::min(l.size(), BlockText::kMaxLineBytes);
                    put16(out, static_cast<uint16_t>(len));
                    out.insert(out.end(), l.begin(), l.begin() + static_cast<std::ptrdiff_t>(len));
                }
            }
        }
    }

    // Biome colours (if this chunk has them)
    bool anyTint = false;
    for (int c = 0; c < ChunkData::kTintChannels; ++c) anyTint = anyTint || chunk.hasTint(c);
    if (anyTint) {
        put32(out, kTintMagic);
        for (int c = 0; c < ChunkData::kTintChannels; ++c) {
            if (!chunk.hasTint(c)) continue;
            out.push_back(static_cast<uint8_t>(c));
            for (int z = 0; z < CHUNK_WIDTH; ++z) {
                for (int x = 0; x < CHUNK_WIDTH; ++x) {
                    const uint32_t rgb = chunk.getColumnTint(x, z, c);
                    out.push_back(static_cast<uint8_t>(rgb >> 16));
                    out.push_back(static_cast<uint8_t>(rgb >> 8));
                    out.push_back(static_cast<uint8_t>(rgb));
                }
            }
        }
    }
    return out;
}

bool World::saveChunkToDisk(const ChunkCoord& c, const ChunkData& chunk) const {
    if (m_saveDir.empty()) return false;
    const std::vector<uint8_t> out = encodeChunk(chunk);

    // Write to a temp file, then swap it in, so a crash mid-write can't corrupt the save
    namespace fs = std::filesystem;
    const std::string finalPath = chunkPath(c.first, c.second);
    const std::string tmpPath = finalPath + ".tmp";
    std::error_code ec;
    fs::create_directories(fs::path(finalPath).parent_path(), ec);

    {
        std::ofstream f(tmpPath, std::ios::binary | std::ios::trunc);
        if (!f) {
            std::cerr << "[Save] Could not write " << tmpPath << std::endl;
            return false;
        }
        f.write(reinterpret_cast<const char*>(out.data()), static_cast<std::streamsize>(out.size()));
        if (!f) {
            std::cerr << "[Save] Write failed for " << tmpPath << std::endl;
            return false;
        }
    }

    // One-step replace; on failure the existing save is kept as it was (SafeReplace.h) and the
    // chunk stays marked as changed, so it's saved again later
    if (!replaceFile(tmpPath, finalPath, ec)) {
        std::cerr << "[Save] Could not replace " << finalPath << " (the previous save is kept): "
                  << ec.message() << std::endl;
        return false;
    }
    return true;
}

World::DiskLoad World::loadChunkFromDisk(int cx, int cz, ChunkData& chunk, std::string* why) const {
    if (m_saveDir.empty()) return DiskLoad::Missing;
    auto fail = [&](const std::string& msg) { if (why) *why = msg; return DiskLoad::Error; };

    const std::string path = chunkPath(cx, cz);
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        // Never saved - or there, but couldn't be opened. Only a confirmed absence means "generate".
        std::error_code ec;
        const ChunkFile state = chunkFileState(ChunkCoord{cx, cz}, ec);
        if (state == ChunkFile::Missing) return DiskLoad::Missing;
        return fail(state == ChunkFile::Error ? "couldn't check its save file (" + ec.message() + ")"
                                              : "its save file exists but couldn't be opened");
    }

    // Read exactly as many bytes as the file has: a read that stops early is a disk error (try
    // again), not a damaged file (which would be regenerated and then overwritten when saved).
    std::error_code ec;
    const std::uintmax_t size = std::filesystem::file_size(path, ec);
    if (ec) {
        if (ec == std::errc::no_such_file_or_directory) return DiskLoad::Missing;   // deleted just now
        return fail("couldn't get its save file's size (" + ec.message() + ")");
    }
    constexpr std::uintmax_t kMaxChunkFile = 64u * 1024u * 1024u;   // far more than any chunk needs
    if (size > kMaxChunkFile) {
        std::cerr << "[Load] Chunk " << cx << "," << cz << " has an impossibly large file - regenerating it" << std::endl;
        return DiskLoad::Damaged;
    }
    std::vector<uint8_t> d(static_cast<size_t>(size));
    if (size > 0) f.read(reinterpret_cast<char*>(d.data()), static_cast<std::streamsize>(size));
    if (static_cast<std::uintmax_t>(f.gcount()) != size && size > 0) {
        return fail("read only " + std::to_string(f.gcount()) + " of its " + std::to_string(size) + " bytes");
    }
    if (!decodeChunk(d, chunk)) {
        std::cerr << "[Load] Chunk " << cx << "," << cz << " is damaged - regenerating it" << std::endl;
        return DiskLoad::Damaged;
    }
    return DiskLoad::Loaded;
}

bool World::decodeChunk(const std::vector<uint8_t>& d, ChunkData& chunk) {
    if (d.size() < 12) return false;
    if (get32(d, 0) != kChunkMagic || get16(d, 4) != kChunkVersion) return false;
    if (get16(d, 6) != CHUNK_WIDTH || get16(d, 8) != CHUNK_HEIGHT) return false;

    // Palette: saved id -> current id (looked up by name)
    auto& registry = BlockRegistry::getInstance();
    uint8_t remap[256] = {};   // unknown blocks become air
    size_t p = 10;
    const uint16_t paletteCount = get16(d, p);
    p += 2;
    for (uint16_t i = 0; i < paletteCount; ++i) {
        if (p + 2 > d.size()) return false;
        const uint8_t savedId = d[p];
        const uint8_t len = d[p + 1];
        p += 2;
        if (p + len > d.size()) return false;
        const std::string name(d.begin() + p, d.begin() + p + len);
        p += len;
        remap[savedId] = registry.getBlockId(name);
        if (remap[savedId] == 0 && name != "air") {
            // Once per name, not once per chunk (a missing mod would otherwise flood the log)
            static std::set<std::string> warned;
            if (warned.insert(name).second) {
                std::cerr << "[Load] Unknown block '" << name << "' (is a mod missing?) - replaced with air" << std::endl;
            }
        }
    }

    // Runs
    int cell = 0;
    while (cell < kChunkCells && p + 4 <= d.size()) {
        const uint16_t count = get16(d, p);
        const uint8_t id = remap[d[p + 2]];
        const uint8_t meta = d[p + 3];
        p += 4;

        for (uint16_t i = 0; i < count && cell < kChunkCells; ++i, ++cell) {
            if (id == 0 && meta == 0) continue;   // new chunks start as air
            const int x = cell % CHUNK_WIDTH;
            const int z = (cell / CHUNK_WIDTH) % CHUNK_WIDTH;
            const int y = cell / (CHUNK_WIDTH * CHUNK_WIDTH);
            chunk.setBlockWithMeta(x, y, z, id, meta);
        }
    }
    // Terrain info, if saved
    if (cell == kChunkCells && p + 7 <= d.size() && get32(d, p) == kTerrainMagic) {
        chunk.terrainVersion = get16(d, p + 4);
        chunk.built = d[p + 6] != 0;
        chunk.terrainKnown = true;
        p += 7;
    }
    // Block text (signs), if saved - read carefully: a damaged section just ends it
    if (cell == kChunkCells && p + 6 <= d.size() && get32(d, p) == kTextMagic) {
        p += 4;
        const uint16_t count = get16(d, p);
        p += 2;
        bool ok = true;
        for (uint16_t i = 0; i < count && ok; ++i) {
            if (p + 4 > d.size()) { ok = false; break; }
            const int x = d[p], y = d[p + 1], z = d[p + 2];
            BlockText t;
            t.waxed = (d[p + 3] & 1) != 0;
            p += 4;
            for (BlockTextSide* side : { &t.front, &t.back }) {
                if (p + 5 > d.size()) { ok = false; break; }
                side->color = (static_cast<uint32_t>(d[p]) << 16) | (static_cast<uint32_t>(d[p + 1]) << 8) | d[p + 2];
                side->glowing = d[p + 3] != 0;
                const int lines = d[p + 4];
                p += 5;
                for (int l = 0; l < lines; ++l) {
                    if (p + 2 > d.size()) { ok = false; break; }
                    const uint16_t len = get16(d, p);
                    p += 2;
                    if (p + len > d.size()) { ok = false; break; }
                    if (static_cast<size_t>(l) < BlockText::kMaxLines)
                        side->lines.emplace_back(reinterpret_cast<const char*>(d.data() + p), std::min<size_t>(len, BlockText::kMaxLineBytes));
                    p += len;
                }
            }
            if (ok && x < CHUNK_WIDTH && z < CHUNK_WIDTH && y < CHUNK_HEIGHT) chunk.setText(x, y, z, t);
        }
        if (!ok) p = d.size();
    }
    // Biome colours, if saved
    if (cell == kChunkCells && p + 4 <= d.size() && get32(d, p) == kTintMagic) {
        p += 4;
        constexpr size_t kChannelBytes = 1 + CHUNK_WIDTH * CHUNK_WIDTH * 3;
        while (p + kChannelBytes <= d.size()) {
            const int c = d[p++];
            for (int z = 0; z < CHUNK_WIDTH; ++z) {
                for (int x = 0; x < CHUNK_WIDTH; ++x) {
                    chunk.setColumnTint(x, z, (uint32_t(d[p]) << 16) | (uint32_t(d[p + 1]) << 8) | d[p + 2], c);
                    p += 3;
                }
            }
        }
    }
    return cell == kChunkCells;
}

const ChunkData* World::chunkData(const ChunkCoord& c) const {
    const auto it = chunks.find(c);
    return it != chunks.end() ? it->second.get() : nullptr;
}

std::vector<World::ChunkCoord> World::takeDirtyChunks() {
    std::vector<ChunkCoord> out(m_dirty.begin(), m_dirty.end());
    m_dirty.clear();
    return out;
}

std::vector<World::ChunkCoord> World::takeUnloadedChunks() {
    std::vector<ChunkCoord> out;
    out.swap(m_unloaded);
    return out;
}

// ---------------------------------------------------------------------------
// Fluids: the simulation is in Fluids.cpp
// ---------------------------------------------------------------------------

void World::updateFluids(float dt) {
    NaturalChanges nature;   // flowing and falling: not building
    m_fluids->update(dt);
    updateFalling(dt);
    updateMoves();
}

void World::noteFluidChange(const glm::ivec3& loc) {
    m_fluids->noteChange(loc);
    // This cell, and the one above (it may have lost what held it up), may need to fall
    if (!m_remote && m_fallChecks.size() < 100000) {
        m_fallChecks.push_back(loc);
        m_fallChecks.push_back(loc + glm::ivec3(0, 1, 0));
    }
}

// ---------------------------------------------------------------------------
// Falling blocks (register_block's gravity = true), like Minecraft's sand and gravel: with
// nothing solid underneath, the block leaves the world and drops smoothly, speeding up, then
// is placed on the first solid block. Only two block changes (where it was, where it lands),
// so chunks aren't rebuilt and players aren't sent a change for every block it passes; a stack
// falls one block after another.
// ---------------------------------------------------------------------------

// Air, a fluid, or something you walk through (grass, flowers, torches - they're crushed)
bool World::canFallInto(const glm::ivec3& p) const {
    if (p.y < 0 || p.y >= CHUNK_HEIGHT || !isLoaded(p)) return false;   // unloaded: wait
    const uint8_t id = getBlockAt(p);
    if (id == 0) return true;
    if (FluidRegistry::get().forBlock(id)) return true;
    return !BlockRegistry::getInstance().getBlock(id).isWalkable;
}

std::vector<World::FallingInfo> World::fallingBlocks() const {
    std::vector<FallingInfo> out;
    out.reserve(m_falling.size());
    for (const auto& f : m_falling) {
        out.push_back(FallingInfo{ f.id, f.meta, glm::vec3(static_cast<float>(f.x), f.y, static_cast<float>(f.z)) });
    }
    return out;
}

void World::addFallingVisual(const glm::ivec3& cell, uint8_t id, uint8_t meta) {
    if (m_falling.size() >= 4096 || id == 0) return;
    m_falling.push_back(FallingBlock{ cell.x, cell.z, static_cast<float>(cell.y), 0.0f, id, meta, true, 0.0f });
}

// Minecraft's 0.04 blocks/tick^2 = 16 blocks/s^2, up to about 40 blocks/s
bool World::stepFalling(FallingBlock& f, float dt) {
    f.speed = std::min(f.speed + 16.0f * dt, 40.0f);
    float newY = f.y - f.speed * dt;
    int cell = static_cast<int>(std::floor(f.y + 1e-4f));   // the cell its bottom is in
    while (newY < static_cast<float>(cell)) {
        const glm::ivec3 below(f.x, cell - 1, f.z);
        if (below.y >= 0 && below.y < CHUNK_HEIGHT && !isLoaded(below)) {
            f.y = static_cast<float>(cell);                     // not loaded yet: wait above it
            f.speed = 0.0f;
            return false;
        }
        if (!canFallInto(below)) {                              // landed on it
            f.y = static_cast<float>(cell);
            return true;
        }
        --cell;
    }
    f.y = newY;
    return false;
}

void World::landFalling(const FallingBlock& f, int cellY) {
    // Normally the cell it fell into; if someone built there meanwhile, the first free one above
    glm::ivec3 at(f.x, cellY, f.z);
    while (at.y < CHUNK_HEIGHT && !canFallInto(at)) ++at.y;
    if (at.y >= CHUNK_HEIGHT) return;
    setBlockWithMetaAt(at, f.id, f.meta);
}

void World::updateFalling(float dt) {
    auto& reg = BlockRegistry::getInstance();
    dt = std::min(dt, 0.25f);   // (a long frame shouldn't send blocks through the floor in one step)

    // A remote world (joined to a server): the server decides; we only animate what it said
    // started falling. Its landing arrives as an ordinary block change.
    if (m_remote) {
        m_fallChecks.clear();
        for (size_t i = 0; i < m_falling.size();) {
            FallingBlock& f = m_falling[i];
            // Landed here first: it rests in place until the server's "it landed" arrives
            // (applyNetworkBlock removes it then) - at most 1.5 s, in case that never comes
            if (f.landedFor > 0.0f || stepFalling(f, dt)) f.landedFor += dt;
            if (f.landedFor > 1.5f) { m_falling[i] = m_falling.back(); m_falling.pop_back(); }
            else ++i;
        }
        return;
    }

    // Blocks that should start falling: they leave the world, and fall as FallingBlocks
    if (!m_fallChecks.empty()) {
        std::vector<glm::ivec3> checks;
        checks.swap(m_fallChecks);
        for (const auto& p : checks) {
            if (p.y <= 0 || p.y >= CHUNK_HEIGHT || !isLoaded(p)) continue;
            const uint8_t id = getBlockAt(p);
            if (id == 0 || !reg.getBlock(id).hasGravity) continue;
            if (!canFallInto(p + glm::ivec3(0, -1, 0))) continue;
            if (m_falling.size() >= 4096) break;
            const uint8_t meta = getMetadataAt(p);
            setBlockAt(p, 0);              // (queues the block above: a stack follows)
            m_falling.push_back(FallingBlock{ p.x, p.z, static_cast<float>(p.y), 0.0f, id, meta, false, 0.0f });
            m_fallingStarts.push_back(FallingStart{ p, id, meta });
        }
    }

    for (size_t i = 0; i < m_falling.size();) {
        FallingBlock& f = m_falling[i];
        if (stepFalling(f, dt)) {
            if (!f.visualOnly) landFalling(f, static_cast<int>(std::lround(f.y)));
            m_falling[i] = m_falling.back();
            m_falling.pop_back();
        } else {
            ++i;
        }
    }
}

void World::finishFalling() {
    for (auto& f : m_falling) {
        if (f.visualOnly) continue;
        // straight down to where it would land (or as far as the loaded world goes)
        int cell = static_cast<int>(std::floor(f.y + 1e-4f));
        while (cell > 0) {
            const glm::ivec3 below(f.x, cell - 1, f.z);
            if (!isLoaded(below) || !canFallInto(below)) break;
            --cell;
        }
        landFalling(f, cell);
    }
    m_falling.clear();
}

// ---------------------------------------------------------------------------
// Block and metadata access
// ---------------------------------------------------------------------------

uint8_t World::getBlockAt(const glm::ivec3& loc) const {
    if (loc.y < 0 || loc.y >= CHUNK_HEIGHT) return 0;
    const ChunkData* chunk = chunkAt(loc);
    if (!chunk) return 0;
    return chunk->getBlock(positiveMod(loc.x, CHUNK_WIDTH), loc.y,
                           positiveMod(loc.z, CHUNK_WIDTH));
}

void World::setBlockAt(const glm::ivec3& loc, uint8_t blockId) {
    if (loc.y < 0 || loc.y >= CHUNK_HEIGHT) return;
    ChunkData* chunk = chunkAt(loc);
    if (!chunk) return;

    const uint8_t oldId = chunk->getLocalBlock(positiveMod(loc.x, CHUNK_WIDTH), loc.y, positiveMod(loc.z, CHUNK_WIDTH));
    chunk->setBlock(positiveMod(loc.x, CHUNK_WIDTH), loc.y,
                    positiveMod(loc.z, CHUNK_WIDTH), blockId);
    lightBlockChanged(loc, oldId);
    // A placed fluid source is a fresh source (old metadata would make it look like flowing)
    const FluidDef* placedFluid = FluidRegistry::get().forBlock(blockId);
    if (placedFluid && blockId == placedFluid->sourceId) {
        chunk->setMetadata(positiveMod(loc.x, CHUNK_WIDTH), loc.y, positiveMod(loc.z, CHUNK_WIDTH), 0);
    }
    markBlockChanged(loc);
    markModified(loc);
    noteFluidChange(loc);
    recordChange(loc);
}

uint8_t World::getMetadataAt(const glm::ivec3& loc) const {
    if (loc.y < 0 || loc.y >= CHUNK_HEIGHT) return 0;
    const ChunkData* chunk = chunkAt(loc);
    if (!chunk) return 0;
    return chunk->getMetadata(positiveMod(loc.x, CHUNK_WIDTH), loc.y,
                              positiveMod(loc.z, CHUNK_WIDTH));
}

void World::setBlockWithMetaAt(const glm::ivec3& loc, uint8_t blockId, uint8_t meta) {
    if (loc.y < 0 || loc.y >= CHUNK_HEIGHT) return;
    ChunkData* chunk = chunkAt(loc);
    if (!chunk) return;
    const uint8_t oldId = chunk->getLocalBlock(positiveMod(loc.x, CHUNK_WIDTH), loc.y, positiveMod(loc.z, CHUNK_WIDTH));
    chunk->setBlockWithMeta(positiveMod(loc.x, CHUNK_WIDTH), loc.y, positiveMod(loc.z, CHUNK_WIDTH), blockId, meta);
    lightBlockChanged(loc, oldId);
    markBlockChanged(loc);
    markModified(loc);
    noteFluidChange(loc);
    recordChange(loc);
}

void World::setMetadataAt(const glm::ivec3& loc, uint8_t meta) {
    if (loc.y < 0 || loc.y >= CHUNK_HEIGHT) return;
    ChunkData* chunk = chunkAt(loc);
    if (!chunk) return;

    chunk->setMetadata(positiveMod(loc.x, CHUNK_WIDTH), loc.y,
                       positiveMod(loc.z, CHUNK_WIDTH), meta);
    markBlockChanged(loc);
    markModified(loc);
    noteFluidChange(loc);    // (a slab or stairs may have been waterlogged or drained)
    recordChange(loc);
}

// ---------------------------------------------------------------------------
// Multiplayer
// ---------------------------------------------------------------------------

void World::recordChange(const glm::ivec3& loc) {
    if (!m_recordChanges && !m_remote) return;
    m_changes.push_back(BlockChange{loc, getBlockAt(loc), getMetadataAt(loc)});
}

const BlockText* World::getBlockTextAt(const glm::ivec3& loc) const {
    const ChunkData* c = chunkAt(loc);
    if (!c) return nullptr;
    return c->getText(positiveMod(loc.x, CHUNK_WIDTH), loc.y, positiveMod(loc.z, CHUNK_WIDTH));
}

bool World::setBlockTextAt(const glm::ivec3& loc, const BlockText& text) {
    ChunkData* c = chunkAt(loc);
    if (!c || loc.y < 0 || loc.y >= CHUNK_HEIGHT) return false;
    BlockText t = text;   // (kept within the limits)
    for (BlockTextSide* side : { &t.front, &t.back }) {
        if (side->lines.size() > BlockText::kMaxLines) side->lines.resize(BlockText::kMaxLines);
        for (auto& l : side->lines) if (l.size() > BlockText::kMaxLineBytes) l.resize(BlockText::kMaxLineBytes);
        side->color &= 0xFFFFFF;
    }
    c->setText(positiveMod(loc.x, CHUNK_WIDTH), loc.y, positiveMod(loc.z, CHUNK_WIDTH), t);
    if (!m_remote) markModified(loc);
    if (m_recordChanges) m_textChanges.push_back(loc);
    return true;
}

bool World::clearBlockTextAt(const glm::ivec3& loc) {
    ChunkData* c = chunkAt(loc);
    if (!c || !getBlockTextAt(loc)) return false;
    c->clearText(positiveMod(loc.x, CHUNK_WIDTH), loc.y, positiveMod(loc.z, CHUNK_WIDTH));
    if (!m_remote) markModified(loc);
    if (m_recordChanges) m_textChanges.push_back(loc);
    return true;
}

void World::applyNetworkText(const glm::ivec3& loc, const BlockText* text) {
    ChunkData* c = chunkAt(loc);
    if (!c) return;
    if (text) c->setText(positiveMod(loc.x, CHUNK_WIDTH), loc.y, positiveMod(loc.z, CHUNK_WIDTH), *text);
    else c->clearText(positiveMod(loc.x, CHUNK_WIDTH), loc.y, positiveMod(loc.z, CHUNK_WIDTH));
}

std::vector<World::BlockChange> World::takeBlockChanges() {
    std::vector<BlockChange> out;
    out.swap(m_changes);
    return out;
}

void World::insertChunk(int cx, int cz, std::shared_ptr<ChunkData> chunk) {
    if (!chunk) return;
    const ChunkCoord c{cx, cz};
    if (chunks.count(c)) unlinkChunk(cx, cz);
    chunk->neighbors[0] = chunk->neighbors[1] = chunk->neighbors[4] = chunk->neighbors[5] = nullptr;
    chunk->chunkX = cx;
    chunk->chunkZ = cz;
    if (m_light) { auto old = chunks.find(c); if (old != chunks.end()) m_light->forget(old->second.get()); }
    ChunkData& added = *chunk;
    chunks[c] = std::move(chunk);
    linkChunk(cx, cz);
    lightChunkAdded(added);
    markDirty(cx, cz);
    markDirty(cx - 1, cz);
    markDirty(cx + 1, cz);
    markDirty(cx, cz - 1);
    markDirty(cx, cz + 1);
    // Diagonal chunks too: fluid surface corners at chunk corners look into this chunk
    markDirty(cx - 1, cz - 1);
    markDirty(cx + 1, cz - 1);
    markDirty(cx - 1, cz + 1);
    markDirty(cx + 1, cz + 1);
}

void World::removeChunk(int cx, int cz) {
    const ChunkCoord c{cx, cz};
    auto it = chunks.find(c);
    if (it == chunks.end()) return;
    unlinkChunk(cx, cz);
    if (m_light) m_light->forget(it->second.get());
    m_dirty.erase(c);
    m_unloaded.push_back(c);
    chunks.erase(it);
}

void World::applyNetworkBlock(const glm::ivec3& loc, uint8_t id, uint8_t meta) {
    if (loc.y < 0 || loc.y >= CHUNK_HEIGHT) return;
    // A falling block of ours (shown since the server said it started falling) has landed here:
    // the real block takes over from the moving copy
    if (id != 0) {
        for (size_t i = 0; i < m_falling.size(); ++i) {
            const FallingBlock& f = m_falling[i];
            if (f.visualOnly && f.id == id && f.x == loc.x && f.z == loc.z && f.y >= static_cast<float>(loc.y) - 0.5f) {
                m_falling[i] = m_falling.back();
                m_falling.pop_back();
                break;
            }
        }
    }
    ChunkData* chunk = chunkAt(loc);
    if (!chunk) return;
    const int lx = positiveMod(loc.x, CHUNK_WIDTH);
    const int lz = positiveMod(loc.z, CHUNK_WIDTH);
    if (chunk->getLocalBlock(lx, loc.y, lz) == id && chunk->getLocalMetadata(lx, loc.y, lz) == meta) return;
    const uint8_t oldId = chunk->getLocalBlock(lx, loc.y, lz);
    chunk->setBlockWithMeta(lx, loc.y, lz, id, meta);
    lightBlockChanged(loc, oldId);
    markBlockChanged(loc);
}

// ---------------------------------------------------------------------------
// Loading readiness
// ---------------------------------------------------------------------------

// Fraction of chunks within `radius` of pos that are loaded (WorldRenderer checks meshes).
float World::areaLoadProgress(const glm::vec3& pos, int radius) const {
    const int pcx = floorDiv(static_cast<int>(std::floor(pos.x)), CHUNK_WIDTH);
    const int pcz = floorDiv(static_cast<int>(std::floor(pos.z)), CHUNK_WIDTH);

    int total = 0;
    int done = 0;
    for (int dx = -radius; dx <= radius; ++dx) {
        for (int dz = -radius; dz <= radius; ++dz) {
            ++total;
            const ChunkCoord c{pcx + dx, pcz + dz};
            if (chunks.count(c)) ++done;
        }
    }
    return total > 0 ? static_cast<float>(done) / static_cast<float>(total) : 1.0f;
}

// First air block above the highest walkable block in this column, or -1 if none is loaded.
int World::groundHeightAt(int x, int z) const {
    for (int y = CHUNK_HEIGHT - 1; y >= 0; --y) {
        const uint8_t id = getBlockAt(glm::ivec3(x, y, z));
        if (id == 0) continue;
        if (BlockRegistry::getInstance().getBlock(id).isWalkable) {
            return y + 1;
        }
    }
    return -1;
}

// ---------------------------------------------------------------------------
// Light (Light.h)
// ---------------------------------------------------------------------------

void World::setDimension(const std::string& nameIn) {
    const std::string name = nameIn == dims::kOverworld ? std::string() : nameIn;
    m_dimension = name;
    m_dimType = dims::Registry::get().type(name.empty() ? std::string(dims::kOverworld) : name);
    m_luaSeeded = false;   // (its own terrain version, worked out again before chunks load)
    if (name.empty()) { m_dimType = DimensionType(); m_dimType.name = dims::kOverworld; }
    if (m_light) {
        m_light->setSkylight(m_dimType.hasSkylight);
        for (auto& entry : chunks) lightChunkAdded(*entry.second);   // (normally none: set before loading)
    }
}

void World::setLighting(bool on) {
    if (on == m_lightingOn) return;
    m_lightingOn = on;
    if (!on) { m_light.reset(); return; }
    m_light = std::make_unique<LightEngine>();
    m_light->setSkylight(m_dimType.hasSkylight);
    for (auto& entry : chunks) lightChunkAdded(*entry.second);   // chunks already here
    for (auto& entry : chunks) markDirty(entry.first.first, entry.first.second);
}

uint16_t World::getLightAt(const glm::ivec3& loc) const {
    if (loc.y >= CHUNK_HEIGHT) return ChunkData::kFullSky;
    if (loc.y < 0) return 0;
    const ChunkData* chunk = chunkAt(loc);
    if (!chunk || !chunk->hasLight()) return ChunkData::kFullSky;
    return chunk->getLocalLight(positiveMod(loc.x, CHUNK_WIDTH), loc.y, positiveMod(loc.z, CHUNK_WIDTH));
}

void World::lightChunkAdded(ChunkData& chunk) {
    if (!m_light) return;
    m_light->chunkAdded(chunk);
    markLightTouched();
}

void World::lightBlockChanged(const glm::ivec3& loc, uint8_t oldId) {
    if (!m_light) return;
    ChunkData* chunk = chunkAt(loc);
    if (!chunk || !chunk->hasLight()) return;
    const int lx = positiveMod(loc.x, CHUNK_WIDTH), lz = positiveMod(loc.z, CHUNK_WIDTH);
    const light::BlockLight& before = light::blockLight(oldId);
    const light::BlockLight& after = light::blockLight(chunk->getLocalBlock(lx, loc.y, lz));
    // Water flowing into water, a block swapped for one that treats light the same: nothing to do
    if (&before == &after) return;
    if (before.opaque == after.opaque && before.absorb == after.absorb && !before.emits() && !after.emits()
        && before.filter[0] == after.filter[0] && before.filter[1] == after.filter[1] && before.filter[2] == after.filter[2]) {
        return;
    }
    m_light->blockChanged(*chunk, lx, loc.y, lz);
    markLightTouched();
}

void World::markLightTouched() {
    for (ChunkData* c : m_light->takeTouched()) markDirty(c->chunkX, c->chunkZ);
}

// ---------------------------------------------------------------------------
// Regions (Region.h): reading and writing boxes of blocks, and moving them (seen moving)
// ---------------------------------------------------------------------------

namespace {
double steadySeconds() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}
void sortCorners(const glm::ivec3& a, const glm::ivec3& b, glm::ivec3& mn, glm::ivec3& mx) {
    mn = glm::min(a, b);
    mx = glm::max(a, b);
}
}

bool World::regionLoaded(const glm::ivec3& a, const glm::ivec3& b) const {
    glm::ivec3 mn, mx;
    sortCorners(a, b, mn, mx);
    for (int cx = floorDiv(mn.x, CHUNK_WIDTH); cx <= floorDiv(mx.x, CHUNK_WIDTH); ++cx)
        for (int cz = floorDiv(mn.z, CHUNK_WIDTH); cz <= floorDiv(mx.z, CHUNK_WIDTH); ++cz)
            if (!chunkData(ChunkCoord{ cx, cz })) return false;
    return true;
}

bool World::readRegion(const glm::ivec3& a, const glm::ivec3& b, Region& out, std::string& why) const {
    glm::ivec3 mn, mx;
    sortCorners(a, b, mn, mx);
    mn.y = std::max(mn.y, 0);
    mx.y = std::min(mx.y, CHUNK_HEIGHT - 1);
    if (mx.y < mn.y) { why = "the box is above or below the world"; return false; }
    const glm::ivec3 size = mx - mn + glm::ivec3(1);
    if (!Region::validSize(size)) {
        why = "invalid or too big (x/z up to 4096, y up to 256, " +
              std::to_string(Region::kMaxCells) + " blocks at most)";
        return false;
    }
    if (!regionLoaded(mn, mx)) { why = "part of the box isn't loaded"; return false; }
    out.resize(size);
    for (int x = 0; x < size.x; ++x)
        for (int z = 0; z < size.z; ++z) {
            const glm::ivec3 col(mn.x + x, 0, mn.z + z);
            const ChunkData* c = chunkAt(col);
            const int lx = positiveMod(col.x, CHUNK_WIDTH), lz = positiveMod(col.z, CHUNK_WIDTH);
            for (int y = 0; y < size.y; ++y) {
                const size_t i = out.index(x, y, z);
                out.ids[i] = c->getLocalBlock(lx, mn.y + y, lz);
                out.metas[i] = c->getLocalMetadata(lx, mn.y + y, lz);
                if (const BlockText* t = c->getText(lx, mn.y + y, lz)) out.texts[static_cast<uint32_t>(i)] = *t;
            }
        }
    return true;
}

size_t World::writeRegion(const Region& r, const glm::ivec3& origin, bool withAir, bool* complete) {
    if (complete) {
        *complete = false;
        if (!r.valid()) return 0;
        const glm::ivec3 end = origin + r.size - glm::ivec3(1);
        if (!regionLoaded(origin, end)) return 0;
    }
    size_t changed = 0;
    for (int x = 0; x < r.size.x; ++x)
        for (int z = 0; z < r.size.z; ++z)
            for (int y = 0; y < r.size.y; ++y) {
                const glm::ivec3 p = origin + glm::ivec3(x, y, z);
                if (p.y < 0 || p.y >= CHUNK_HEIGHT) continue;
                const size_t i = r.index(x, y, z);
                const uint8_t id = r.ids[i], meta = r.metas[i];
                if (id == 0 && !withAir) continue;
                if (!chunkAt(p)) {
                    if (complete) return changed;
                    continue;
                }
                const auto t = r.texts.find(static_cast<uint32_t>(i));
                if (getBlockAt(p) != id || getMetadataAt(p) != meta) {
                    setBlockWithMetaAt(p, id, meta);
                    ++changed;
                }
                if (t != r.texts.end()) setBlockTextAt(p, t->second);
                else if (getBlockTextAt(p)) clearBlockTextAt(p);
            }
    if (complete) *complete = true;
    return changed;
}

std::vector<uint16_t> World::sampleLight(const glm::ivec3& mn, const glm::ivec3& size) const {
    // a box one block bigger on every side, so the faces at its edges get the light in front of them
    const glm::ivec3 s = size + glm::ivec3(2);
    std::vector<uint16_t> out(static_cast<size_t>(s.x) * s.y * s.z, ChunkData::kFullSky);
    if (!m_lightingOn) return out;
    for (int x = 0; x < s.x; ++x)
        for (int y = 0; y < s.y; ++y)
            for (int z = 0; z < s.z; ++z)
                out[(static_cast<size_t>(x) * s.y + y) * s.z + z] = getLightAt(mn - glm::ivec3(1) + glm::ivec3(x, y, z));
    return out;
}

uint32_t World::startMove(const glm::ivec3& a, const glm::ivec3& b, const glm::ivec3& offset, float seconds, std::string& why) {
    if (m_remote) { why = "only where the world runs"; return 0; }
    if (m_moves.size() >= kMaxMoves) { why = "too many moving at once"; return 0; }
    glm::ivec3 mn, mx;
    sortCorners(a, b, mn, mx);
    const glm::ivec3 size = mx - mn + glm::ivec3(1);
    if (static_cast<size_t>(size.x) * size.y * size.z > kMaxMoveCells) {
        why = "too big to move (" + std::to_string(kMaxMoveCells) + " blocks at most)";
        return 0;
    }
    if (mn.y + offset.y < 0 || mx.y + offset.y >= CHUNK_HEIGHT) { why = "it would leave the world"; return 0; }
    if (!regionLoaded(mn + offset, mx + offset)) { why = "where it's going isn't loaded"; return 0; }
    // (no overlapping moves: one would take the other's blocks halfway)
    for (const auto& m : m_moves) {
        const glm::ivec3 omn = glm::min(m.from, m.from + m.offset), omx = glm::max(m.from, m.from + m.offset) + m.region.size - glm::ivec3(1);
        const glm::ivec3 nmn = glm::min(mn, mn + offset), nmx = glm::max(mx, mx + offset);
        if (glm::all(glm::lessThanEqual(omn, nmx)) && glm::all(glm::lessThanEqual(nmn, omx))) { why = "something is already moving there"; return 0; }
    }
    MovingRegion m;
    if (!readRegion(mn, mx, m.region, why)) return 0;
    m.id = m_nextMoveId++;
    m.from = mn;
    m.offset = offset;
    m.seconds = std::clamp(seconds, 0.0f, 60.0f);
    m.startedAt = steadySeconds();
    m.light = sampleLight(mn, m.region.size);
    m.visualOnly = false;
    if (m_recordChanges) m_moveStarts.push_back(MoveStart{ m.id, m.from, m.offset, m.seconds, m.region });   // (a server sends these)
    // it leaves the world now (the players were told first: they see it lift out, not a hole)
    Region air;
    air.resize(m.region.size);
    writeRegion(air, mn, true);
    if (m.seconds <= 0.0f) {
        if (!placeMove(m)) m_moves.push_back(std::move(m));
        return m.id;
    }
    m_moves.push_back(std::move(m));
    return m_moves.back().id;
}

bool World::placeMove(const MovingRegion& m) {
    bool complete = false;
    writeRegion(m.region, m.from + m.offset, true, &complete);
    if (!complete) {
        m_queueDirty = true;
        return false;
    }
    if (m_recordChanges) m_moveEnds.push_back(m.id);
    m_movesDone.push_back(m.id);
    return true;
}

void World::updateMoves() {
    if (m_moves.empty()) return;
    const double now = steadySeconds();
    for (size_t i = 0; i < m_moves.size();) {
        MovingRegion& m = m_moves[i];
        const double t = now - m.startedAt;
        if (m.visualOnly) {
            // (the server's "it's placed" never came: gone after a while)
            if (t > m.seconds + 5.0) { m_moves.erase(m_moves.begin() + i); continue; }
            ++i;
            continue;
        }
        if (t >= m.seconds) {
            if (placeMove(m)) m_moves.erase(m_moves.begin() + i);
            else ++i;
            continue;
        }
        ++i;
    }
}

void World::finishMoves() {
    for (size_t i = 0; i < m_moves.size();) {
        if (m_moves[i].visualOnly || placeMove(m_moves[i])) m_moves.erase(m_moves.begin() + i);
        else ++i;
    }
}

void World::addMoveVisual(uint32_t id, const glm::ivec3& from, const glm::ivec3& offset, float seconds, Region region) {
    if (!Region::validSize(region.size) || region.cells() > kMaxMoveCells) return;
    for (const auto& m : m_moves) if (m.id == id) return;
    updateMoves();     // (drops copies whose end never came)
    while (m_moves.size() >= kMaxMoves) {
        const auto oldestVisual = std::find_if(m_moves.begin(), m_moves.end(),
                                               [](const MovingRegion& m) { return m.visualOnly; });
        if (oldestVisual == m_moves.end()) return;
        m_moves.erase(oldestVisual);
    }
    MovingRegion m;
    m.id = id;
    m.from = from;
    m.offset = offset;
    m.seconds = std::clamp(seconds, 0.0f, 60.0f);
    m.startedAt = steadySeconds();
    m.light = sampleLight(from, region.size);     // (the blocks are still here: the server's "air" comes next)
    m.region = std::move(region);
    m.visualOnly = true;
    m_moves.push_back(std::move(m));
}

void World::endMoveVisual(uint32_t id) {
    for (size_t i = 0; i < m_moves.size(); ++i)
        if (m_moves[i].id == id && m_moves[i].visualOnly) { m_moves.erase(m_moves.begin() + i); return; }
}

std::vector<World::MoveView> World::movingRegions() const {
    std::vector<MoveView> out;
    if (m_moves.empty()) return out;
    const double now = steadySeconds();
    for (const auto& m : m_moves) {
        if (m.visualOnly && now - m.startedAt > m.seconds + 5.0) continue;   // (never ended: stop drawing it)
        float f = m.seconds > 0.0f ? static_cast<float>((now - m.startedAt) / m.seconds) : 1.0f;
        f = std::clamp(f, 0.0f, 1.0f);
        f = f * f * (3.0f - 2.0f * f);          // eases in and out
        out.push_back(MoveView{ m.id, glm::vec3(m.from) + glm::vec3(m.offset) * f, m.from, &m.region, &m.light });
    }
    return out;
}
