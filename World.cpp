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
    std::cout << "[World] Initialized with random seed: " << m_seed << std::endl;
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
void World::markModified(const glm::ivec3& loc) {
    m_modified.insert(ChunkCoord{floorDiv(loc.x, CHUNK_WIDTH), floorDiv(loc.z, CHUNK_WIDTH)});
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
    CaveGenerator::setSeed(m_seed);
    if (m_generator) CaveGenerator::prepare();   // before the threads carve caves
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
                m_generator->request(c.first, c.second, m_resetCount, m_seed);
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
    for (auto& r : m_generator->takeFinished()) {
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
        if (!wantedChunk(*it) && m_generator->cancel(it->first, it->second)) it = m_generating.erase(it);
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
        if (m_generator) m_generator->clearQueue();
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
    const bool fromDisk = disk == DiskLoad::Loaded;

    // 2. Otherwise generate it (Lua mapgen, or the C++ fallback).
    if (!fromDisk) {
        chunk = std::make_shared<ChunkData>();   // fresh, in case a broken file half-filled it
        generateNewChunk(*chunk, chunkX, chunkZ, &lua, m_seed);
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

void World::generateNewChunk(ChunkData& chunk, int cx, int cz, sol::state* lua, uint32_t seed) {
    sol::protected_function genFunc;
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
    // Carve caves into the new terrain (seeded, so the same seed gives the same caves).
    // Saved chunks skip this - their caves are already in the file.
    CaveGenerator::carveChunk(chunk, cx, cz);
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
    if (m_generator) m_generator->clearQueue();
    ++m_resetCount;
    m_remote = false;
    m_recordChanges = false;
    m_changes.clear();
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
    m_fluids->update(dt);
    updateFalling(dt);
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
    recordChange(loc);
}

// ---------------------------------------------------------------------------
// Multiplayer
// ---------------------------------------------------------------------------

void World::recordChange(const glm::ivec3& loc) {
    if (!m_recordChanges && !m_remote) return;
    m_changes.push_back(BlockChange{loc, getBlockAt(loc), getMetadataAt(loc)});
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

void World::setLighting(bool on) {
    if (on == m_lightingOn) return;
    m_lightingOn = on;
    if (!on) { m_light.reset(); return; }
    m_light = std::make_unique<LightEngine>();
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
