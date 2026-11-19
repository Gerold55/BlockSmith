#pragma once
// Block modifiers, as in Luanti (and Minecraft's random ticks): how the world changes by itself.
//
//   engine.register_random_tick{ ... }   MINECRAFT'S RANDOM TICKS: every game tick (20 a second),
//       3 random blocks in each 16x16x16 section near players are picked (the randomTickSpeed
//       game rule); if one is a kind you registered, your action runs. The work is spread
//       evenly over every tick, so grass creeps outwards and crops grow continuously instead
//       of in waves. On average a block is picked every 68 seconds.
//   engine.register_abm{ ... }   ACTIVE block modifier: every `interval` seconds, each block of
//       the given kinds near a player has a 1-in-`chance` chance of running `action` (grass
//       spreading, crops growing, fire, lava cooling...)
//   engine.register_lbm{ ... }   LOADING block modifier: runs on blocks of the given kinds when
//       their chunk is loaded from its save file - once per chunk ever (upgrading old worlds),
//       or every time (catching up on things like timers)
//
// They run only where the world is decided: singleplayer, the game when hosting, the server.
// The scan of the area around players is spread over several ticks (a few milliseconds each),
// so many blocks and many ABMs don't cause a lag spike.

#include <sol/sol.hpp>
#include <glm/glm.hpp>
#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>
#include "World.h"

class BlockModifiers {
public:
    // engine.register_abm / engine.register_lbm go into `engine`
    void bind(sol::state& lua, sol::table engine);

    // Every tick, on the authority. `players`: where they are (feet). `canRun`: false while the
    // world shouldn't change (paused singleplayer).
    void update(float dt, World& world, const std::vector<glm::vec3>& players);

    // A world was opened / closed: which chunks the once-only LBMs already did are kept with it
    // (lbm_done.txt in its folder)
    void loadWorld(const std::string& worldDir);
    void saveWorld(const std::string& worldDir) const;
    void clearWorld();

    // How far from a player blocks are "active" (chunks, each way). Luanti's default is about 3.
    int activeRadius = 3;
    // Random ticks: chunks around each player that get them (Minecraft's simulation distance),
    // and blocks picked per 16x16x16 section per 1/20 s (the randomTickSpeed game rule; 0 = off)
    int randomTickRadius = 6;
    std::function<int()> randomTickSpeed = [] { return 3; };
    double timeLimit = 2.0;          // seconds one action may run (LuaSandbox)
    double budgetMs = 4.0;           // scanning per tick

    size_t abmCount() const { return m_abms.size(); }
    uint64_t randomTicksRun() const { return m_randomTicksRun; }
    size_t lbmCount() const { return m_lbms.size(); }

private:
    struct ABM {
        std::string label;
        std::vector<std::string> nodes, neighbors, without;
        float interval = 10.0f;
        int chance = 50;
        int minY = -1000000, maxY = 1000000;
        sol::protected_function action;
        float timer = 0.0f;
        // resolved block ids (refreshed when blocks are registered later)
        std::array<bool, 256> isNode{}, isNeighbor{}, isWithout{};
        bool anyNeighbor = false, anyWithout = false;
    };
    struct LBM {
        std::string name;
        std::vector<std::string> nodes;
        bool everyLoad = false;
        sol::protected_function action;
        std::array<bool, 256> isNode{};
    };
    struct RandomTick {
        std::string label;
        std::vector<std::string> nodes;
        sol::protected_function action;
    };
    std::vector<RandomTick> m_randomTicks;
    std::array<std::vector<int>, 256> m_tickByBlock;    // block id -> random ticks for it
    bool m_anyTickable = false;
    double m_tickCarry = 0.0;                          // part-ticks owed (the game's tick isn't 1/20 s)
    uint64_t m_randomTicksRun = 0;
    void randomTicks(float dt, World& world, const std::vector<glm::vec3>& players);
    void resolve();
    void runLBMs(World& world, const World::ChunkCoord& c, bool fromDisk);
    void scanChunk(World& world, const World::ChunkCoord& c, const std::vector<int>& due);
    void report(const std::string& where, const std::string& message);

    sol::state* m_lua = nullptr;
    std::vector<ABM> m_abms;
    std::vector<LBM> m_lbms;
    size_t m_resolvedFor = 0;         // registry size the ids were worked out for

    // The scan in progress: these chunks, for these ABMs
    std::vector<World::ChunkCoord> m_scanChunks;
    size_t m_scanNext = 0;
    std::vector<int> m_scanDue;
    std::array<std::vector<int>, 256> m_byBlock;   // block id -> due ABMs wanting it (this scan)

    // Once-only LBMs: the chunks each has done (by name)
    std::map<std::string, std::set<std::pair<int, int>>> m_lbmDone;
    bool m_lbmDirty = false;

    std::map<std::string, std::string> m_lastError;
    uint32_t m_rng = 0x1234567u;
    uint32_t random() { m_rng ^= m_rng << 13; m_rng ^= m_rng >> 17; m_rng ^= m_rng << 5; return m_rng; }
};
