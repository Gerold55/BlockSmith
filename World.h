#pragma once
// The world's block data: loading, generating, saving and unloading chunks, water flow
// and block access. No graphics - WorldRenderer draws it on the client, so a server can
// run a World without a window.

#include <glm/glm.hpp>
#include <cstddef>
#include <cstdint>
#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#include <sol/sol.hpp>
#include "ChunkData.h"
#include "Types.h"

struct PairHash {
    size_t operator()(const std::pair<int, int>& p) const noexcept {
        return std::hash<int>()(p.first) ^ (std::hash<int>()(p.second) << 1);
    }
};

struct IVec3Hash {
    size_t operator()(const glm::ivec3& v) const noexcept {
        return static_cast<size_t>(v.x) * 73856093u
             ^ static_cast<size_t>(v.y) * 19349663u
             ^ static_cast<size_t>(v.z) * 83492791u;
    }
};

class ChunkGenerator;
class FluidSimulator;
class LightEngine;

class World {
public:
    using ChunkCoord = std::pair<int, int>;

    World();
    ~World();

    // Per tick: queues and loads chunks around the player, unloads distant ones.
    void update(const glm::vec3& playerPos, sol::state& lua);
    // Fluids (water, lava, modded fluids - see Fluids.h). Call every frame/tick with the
    // time passed; each fluid flows at its own speed.
    // Fluids flowing and blocks falling (register_block's gravity = true: sand, gravel...).
    // Runs where the world is decided (singleplayer, the host, the server); never when remote.
    void updateFluids(float dt);
    FluidSimulator& fluids() { return *m_fluids; }
    // Nothing above this block up to the top of the world (rain and snow reach it)
    bool hasSkyAbove(const glm::ivec3& loc) const {
        for (int y = loc.y + 1; y < CHUNK_HEIGHT; ++y) if (getBlockAt(glm::ivec3(loc.x, y, loc.z)) != 0) return false;
        return true;
    }
    // ---------------- Light (Light.h) ----------------
    // The game turns this on; the dedicated server leaves it off (it never draws anything).
    void setLighting(bool on);
    bool lighting() const { return m_lightingOn; }
    // Every loaded chunk gets redrawn (e.g. after a lighting setting changed)
    void markAllDirty() { for (auto& entry : chunks) m_dirty.insert(entry.first); }
    // Packed light at a block (see light::sky/red/green/blue). Open sky if not known.
    uint16_t getLightAt(const glm::ivec3& loc) const;

    // Is the chunk holding this block loaded?
    bool isLoaded(const glm::ivec3& loc) const { return chunkAt(loc) != nullptr; }
    // Loads the chunk from its save file, or generates it if it has none. Returns false (and
    // adds nothing) if the save file couldn't be checked or read: the chunk may be saved, so it
    // is not replaced with new terrain - ask again later.
    bool loadChunk(int chunkX, int chunkZ, sol::state& lua);

    uint8_t getBlockAt(const glm::ivec3& loc) const;
    void setBlockAt(const glm::ivec3& loc, uint8_t blockId);
    uint8_t getMetadataAt(const glm::ivec3& loc) const;
    void setMetadataAt(const glm::ivec3& loc, uint8_t meta);
    // Block and metadata in one change (one network update, one remesh).
    void setBlockWithMetaAt(const glm::ivec3& loc, uint8_t blockId, uint8_t meta);

    // The chunk at a chunk coordinate, or nullptr if it isn't loaded.
    const ChunkData* chunkData(const ChunkCoord& c) const;

    // Render distance in chunks (radius), clamped to [2, 16].
    void setRenderDistance(int chunks);
    int renderDistance() const { return m_renderDistance; }
    float renderDistanceBlocks() const { return m_renderDistance * 16.0f; }

    // Time spent loading/generating chunks per update, in milliseconds (always at least one).
    void setLoadBudgetMs(float ms) { m_loadBudgetMs = ms; }

    size_t loadedChunkCount() const { return chunks.size(); }
    size_t pendingLoadCount() const { return m_loadQueue.size(); }
    ChunkCoord playerChunk() const { return m_playerChunk; }

    // Fraction (0..1) of chunks within `radius` chunks of pos that are loaded.
    float areaLoadProgress(const glm::vec3& pos, int radius) const;

    // Y coordinate of the first air block above the highest walkable block in
    // this column. Returns -1 if the column has no loaded ground.
    int groundHeightAt(int x, int z) const;

    // ---------------- Multiplayer ----------------
    struct BlockChange { glm::ivec3 pos; uint8_t id; uint8_t meta; };

    // Remote mode (joined a server): this world never generates, loads, saves or unloads
    // chunks itself - the server sends them. Local edits are kept as predictions and queued
    // in takeBlockChanges() for the client to send. Water doesn't flow (the server does it).
    void setRemote(bool remote) { m_remote = remote; }
    bool isRemote() const { return m_remote; }

    // Adds or replaces a chunk received from the server / removes one it told us to drop.
    void insertChunk(int cx, int cz, std::shared_ptr<ChunkData> chunk);
    void removeChunk(int cx, int cz);
    // Applies a block the server says changed (not queued as a local edit).
    void applyNetworkBlock(const glm::ivec3& loc, uint8_t id, uint8_t meta);

    // Hosting: also keep chunks loaded around these points (other players), each with its
    // own radius in chunks. Cleared by passing an empty list.
    struct LoadCenter { glm::vec3 pos; int radius; };
    void setExtraLoadCenters(const std::vector<LoadCenter>& centers);

    // When on (hosting) or remote, every block/metadata change is queued here.
    void setRecordChanges(bool on) { m_recordChanges = on; if (!on) m_changes.clear(); }
    std::vector<BlockChange> takeBlockChanges();

    // Background world generation (the dedicated server uses this): new chunks are made on
    // the generator's threads instead of inside update(), so ticks never stall. nullptr =
    // generate on the calling thread (the old way). The generator must outlive its use here.
    void setGenerator(ChunkGenerator* generator) { m_generator = generator; }
    size_t generatingCount() const { return m_generating.size(); }

    // Makes a brand-new chunk: the Lua world generator (generate_chunk) if `lua` has one,
    // else the built-in terrain, then caves. Safe to call from any thread with its own lua.
    static void generateNewChunk(ChunkData& chunk, int cx, int cz, sol::state* lua, uint32_t seed);
    // Biome colours for a chunk from Lua's get_grass_color / get_foliage_color (see World.cpp)
    static void computeBiomeTint(ChunkData& chunk, int cx, int cz, sol::state* lua);

    // Chunk <-> bytes, using the save file format (block names, run-length encoded).
    static std::vector<uint8_t> encodeChunk(const ChunkData& chunk);
    static bool decodeChunk(const std::vector<uint8_t>& bytes, ChunkData& chunk);

    // ---------------- Change tracking (for the renderer, later the network) ----------------
    // Chunks whose blocks changed (or whose neighbours loaded/unloaded) since the last call.
    std::vector<ChunkCoord> takeDirtyChunks();
    // Chunks unloaded since the last call.
    std::vector<ChunkCoord> takeUnloadedChunks();
    // Goes up every reset(), so anything caching chunk data knows to throw it all away.
    unsigned resetCount() const { return m_resetCount; }

    // ---------------- Saving / loading ----------------

    // Unloads every chunk and clears all queues (used when switching worlds).
    void reset();

    // Folder for this world's chunk files ("" = saving disabled).
    void setSaveDirectory(const std::string& dir) { m_saveDir = dir; m_notOnDisk.clear(); m_diskRetry.clear(); }
    const std::string& saveDirectory() const { return m_saveDir; }

    // The seed passed to Lua's set_world_seed. Changing it re-seeds Lua on the next update.
    uint32_t seed() const { return m_seed; }
    void setSeed(uint32_t seed) { m_seed = seed; m_luaSeeded = false; }

    // Writes every chunk that changed since it was last saved. Returns chunks written.
    int saveAll();

    // Kept public for compatibility with existing code.
    std::unordered_map<ChunkCoord, std::shared_ptr<ChunkData>, PairHash> chunks;

private:
    ChunkData* chunkAt(const glm::ivec3& loc) const;
    void markDirty(int cx, int cz);
    void markBlockChanged(const glm::ivec3& loc);
    void markModified(const glm::ivec3& loc);
    void linkChunk(int cx, int cz);
    void unlinkChunk(int cx, int cz);

    void rebuildLoadQueue();
    void loadQueuedChunks(sol::state& lua);
    void unloadDistantChunks();
    bool wantedChunk(const ChunkCoord& c) const;   // inside some player's area (+ margin)
    void collectGenerated();
    void addLoadedChunk(int cx, int cz, std::shared_ptr<ChunkData> chunk);
    void recordChange(const glm::ivec3& loc);
    void ensureLuaSeeded(sol::state& lua);

    void noteFluidChange(const glm::ivec3& loc);

    // Chunk files: <saveDir>/chunks/c.<x>.<z>.bin
    std::string chunkPath(int cx, int cz) const;
    // What is on disk for a chunk. Only Missing means "never saved": Error means the file system
    // couldn't tell (permissions, a locked or unplugged drive, too many open files...) - the
    // chunk may well be saved, so it must not be replaced with new terrain.
    enum class ChunkFile { Present, Missing, Error };
    // Is there a saved file for this chunk? (no allocation, no reading) `ec` says why on Error.
    ChunkFile chunkFileState(const ChunkCoord& c, std::error_code& ec) const;
    // Chunks CONFIRMED to have no file (or only a damaged one): only the generator can make them,
    // so they aren't probed again while waiting for a generator slot. Forgotten when the chunk
    // loads (it may be saved later), on reset and when the save folder changes.
    std::unordered_set<ChunkCoord, PairHash> m_notOnDisk;
    // Chunks whose file couldn't be checked or read: tried again later (sooner at first, then
    // up to every few seconds) - never generated in the meantime.
    struct DiskRetry {
        int failures = 0;
        std::chrono::steady_clock::time_point next;
    };
    std::unordered_map<ChunkCoord, DiskRetry, PairHash> m_diskRetry;
    // A disk problem for chunk c: logged (not every attempt) and its next try scheduled
    void noteDiskError(const ChunkCoord& c, const std::string& what);
    bool saveChunkToDisk(const ChunkCoord& c, const ChunkData& chunk) const;
    enum class DiskLoad { Loaded, Missing, Damaged, Error };
    // Missing: no file. Damaged: a file that isn't a valid chunk (regenerated, and replaced when
    // saved). Error: the file couldn't be opened or read - try again later. `why` is set on Error.
    DiskLoad loadChunkFromDisk(int cx, int cz, ChunkData& chunk, std::string* why = nullptr) const;

    uint32_t m_seed = 0;
    bool m_luaSeeded = false;

    int m_renderDistance = 10;
    bool m_queueDirty = true;
    ChunkCoord m_playerChunk{0, 0};

    float m_loadBudgetMs = 4.0f;

    std::vector<ChunkCoord> m_loadQueue;

    // Background generation
    ChunkGenerator* m_generator = nullptr;
    std::unordered_set<ChunkCoord, PairHash> m_generating;   // requested, not back yet

    // Multiplayer
    bool m_remote = false;
    bool m_recordChanges = false;
    std::vector<BlockChange> m_changes;
    struct CenterChunk { ChunkCoord chunk; int radius; };
    std::vector<CenterChunk> m_extraCenters;

    // Change tracking
    std::unordered_set<ChunkCoord, PairHash> m_dirty;
    std::vector<ChunkCoord> m_unloaded;
    unsigned m_resetCount = 0;

    std::unique_ptr<FluidSimulator> m_fluids;

    // Light
    bool m_lightingOn = false;
    std::unique_ptr<LightEngine> m_light;
    void lightChunkAdded(ChunkData& chunk);
    void lightBlockChanged(const glm::ivec3& loc, uint8_t oldId);

    // Falling blocks: cells to check after a change, and blocks on their way down
    struct FallingBlock { glm::ivec3 pos; float speed; float progress; };
    std::vector<glm::ivec3> m_fallChecks;
    std::vector<FallingBlock> m_falling;
    void updateFalling(float dt);
    bool canFallInto(const glm::ivec3& p) const;
    void markLightTouched();

    // Saving
    std::string m_saveDir;
    std::unordered_set<ChunkCoord, PairHash> m_modified;   // changed since last save
};
