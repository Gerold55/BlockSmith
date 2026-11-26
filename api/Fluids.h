#pragma once
// Fluids: water, lava, molten metal, oil... anything that flows like Minecraft water.
//
// A fluid is two blocks - a source block and a flowing block - plus a few numbers that say
// how it behaves. The engine registers plain water by default; games and mods add their own
// from Lua with engine.register_fluid (see addFluidLuaApi below and docs in Fluids.cpp).
//
// How fluid cells are stored (the same for every fluid):
//   source  = the source block, metadata 0
//   flowing = the flowing block (or the source block, if the fluid has no flowing block),
//             metadata low 3 bits = level 1..spread (1 = next to the source, higher = thinner),
//             metadata bit 0x8   = falling (fluid above it; drawn full height)
//
// FluidSimulator runs the flow for one World; World owns one and forwards block changes to it.

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <glm/glm.hpp>
#include <sol/sol.hpp>

class World;

struct FluidDef {
    std::string name;            // "water", "lava", "molten_iron"...
    std::string sourceBlock;     // block name of a full source (defaults to name)
    std::string flowingBlock;    // block name of flowing cells ("" = source block + metadata)
    int   spread = 7;            // blocks it runs across flat ground from a source (1..7)
    float tickSeconds = 0.25f;   // time between flow steps (water 0.25 s, Minecraft lava 1.5 s)
    int   slopeDistance = 4;     // how far it looks for a drop to flow towards (lava: 2)
    bool  infinite = true;       // a gap between 2 sources becomes a source (water yes, lava no)
    bool  swimmable = true;      // players swim in it instead of walking
    float swimSpeed = 2.2f;      // movement speed while in it (blocks/s)

    // Filled by FluidRegistry::refresh() from the block names (0 = block doesn't exist)
    uint8_t sourceId = 0;
    uint8_t flowingId = 0;
};

// What happens when one fluid touches another, e.g. lava next to water.
struct FluidReaction {
    std::string fluid;           // the fluid that changes ("lava")
    std::string touching;        // when it touches this fluid ("water")
    std::string sourceBecomes;   // a source cell turns into this block ("obsidian")
    std::string flowingBecomes;  // a flowing cell turns into this block ("cobblestone")
};

// One cell as a fluid sees it.
struct FluidCell {
    const FluidDef* fluid = nullptr;   // nullptr = not a fluid
    bool source = false;
    bool falling = false;
    int level = 0;                     // 0 for sources, 1..spread for flowing
    bool held = false;                 // a source held in a waterlogged slab/stairs (BlockShape.h)
    explicit operator bool() const { return fluid != nullptr; }
};

class FluidRegistry {
public:
    static FluidRegistry& get();

    // Adds a fluid, or replaces the one with the same name.
    void registerFluid(FluidDef def);
    void registerReaction(FluidReaction reaction);

    // Re-reads the block ids from the block names. Cheap (a few lookups); the simulator and
    // the mesher call it before use, so ids never go stale when blocks are registered later.
    void refresh();
    // refresh() if it has never run (so lookups work before the first update)
    void ensureFresh() { if (!m_refreshed) refresh(); }
    // Back to plain water only: the mods are being loaded again
    void reset();

    // The fluid a block belongs to (source or flowing block), or nullptr. Valid after refresh().
    const FluidDef* forBlock(uint8_t blockId) const {
        const int i = m_byBlock[blockId];
        return i < 0 ? nullptr : &m_fluids[static_cast<size_t>(i)];
    }
    const FluidDef* byName(const std::string& name) const;
    const std::vector<FluidDef>& fluids() const { return m_fluids; }
    const FluidReaction* reaction(const std::string& fluid, const std::string& touching) const;

    // Reads a cell's fluid state from its block id and metadata.
    FluidCell cell(uint8_t blockId, uint8_t meta) const;

    // Surface height (0..1) of a fluid cell, Minecraft's heights: sources 8/9 of a block,
    // each level lower by an even share, falling fluid as high as a source.
    static float surfaceHeight(const FluidCell& c);

private:
    FluidRegistry();
    std::vector<FluidDef> m_fluids;
    std::vector<FluidReaction> m_reactions;
    std::array<int, 256> m_byBlock{};
    bool m_refreshed = false;
};

class FluidSimulator {
public:
    explicit FluidSimulator(World& world) : m_world(world) {}

    // A block changed at loc: fluids in and around it may need to move.
    void noteChange(const glm::ivec3& loc);
    // Call every frame / tick: each fluid takes its own steps at its own speed.
    void update(float dt);
    void clear();

    FluidCell cellAt(const glm::ivec3& p) const;
    size_t activeCount() const;

    static constexpr uint8_t kFallingBit = 0x08;

private:
    void step(const FluidDef& fluid);
    bool canReplace(const glm::ivec3& p) const;                       // air or a dry plant
    bool isHole(const glm::ivec3& p, const FluidDef& fluid) const;    // could drop down from p
    int  slopeDistance(const glm::ivec3& start, const FluidDef& fluid) const;

    World& m_world;
    struct IVec3Hash {
        size_t operator()(const glm::ivec3& v) const noexcept {
            return static_cast<size_t>(v.x) * 73856093u ^ static_cast<size_t>(v.y) * 19349663u
                 ^ static_cast<size_t>(v.z) * 83492791u;
        }
    };
    std::unordered_map<std::string, std::unordered_set<glm::ivec3, IVec3Hash>> m_active;  // per fluid
    std::unordered_map<std::string, float> m_timers;
};

// engine.register_fluid / register_fluid_reaction / get_fluid / list_fluids / is_fluid_source.
// `getWorld` returns the world the get/is functions read (may return nullptr).
void addFluidLuaApi(sol::state& lua, sol::table engine, std::function<World*()> getWorld);
