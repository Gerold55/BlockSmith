#if defined(_WIN32) && !defined(NOMINMAX)
#define NOMINMAX   // (windows.h's min/max macros would break std::max and glm::max)
#endif
#include "BlockShape.h"
#include "Fluids.h"
#include "Block.h"
#include "Types.h"
#include "World.h"
#include <algorithm>
#include <cmath>
#include <iostream>

// ===========================================================================
// How fluids flow (the same rules as Minecraft's FlowingFluid, for every fluid):
//  - A fluid falls first. Only when it can't go down does it spread sideways, one level
//    thinner per block, so it runs `spread` blocks across flat ground (water 7, lava 3).
//  - Sideways, it heads for the nearest drop within `slopeDistance` blocks (and only that
//    way), so it pours off ledges and into holes instead of spreading in a plain diamond.
//  - Fluid that can fall only spreads sideways too when 3+ sources surround it.
//  - Flowing fluid that loses what feeds it dries up, a level per step.
//  - `infinite` fluids: a gap with 2+ sources beside it (and solid ground or a source below)
//    becomes a source - the classic infinite water pool.
//  - Reactions: a fluid cell touching another fluid it has a reaction with turns into a
//    block (lava + water -> obsidian from a source, cobblestone from flowing lava).
//  - Each fluid steps at its own speed. A step looks at the world as it was when the step
//    started, so the fluid moves one block per step.
// ===========================================================================

namespace {
constexpr int kHorizontal[4][2] = { {1, 0}, {-1, 0}, {0, 1}, {0, -1} };
constexpr int kNoDrop = 1000;
constexpr int kMaxCellsPerStep = 2048;   // per fluid; more waits for the next step
constexpr int kWakeOffsets[7][3] = {
    {0, 0, 0}, {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}
};
}

// ===========================================================================
// FluidRegistry
// ===========================================================================

FluidRegistry& FluidRegistry::get() {
    static FluidRegistry instance;
    return instance;
}

FluidRegistry::FluidRegistry() {
    m_byBlock.fill(-1);
    // Plain water is always there (a game can change it with engine.register_fluid)
    FluidDef water;
    water.name = "water";
    water.sourceBlock = "water";
    water.flowingBlock = "flowing_water";
    m_fluids.push_back(water);
}

void FluidRegistry::registerFluid(FluidDef def) {
    if (def.name.empty()) return;
    if (def.sourceBlock.empty()) def.sourceBlock = def.name;
    def.spread = std::clamp(def.spread, 1, 7);
    def.slopeDistance = std::clamp(def.slopeDistance, 0, 8);
    def.tickSeconds = std::clamp(def.tickSeconds, 0.05f, 30.0f);
    for (auto& f : m_fluids) {
        if (f.name == def.name) { f = def; refresh(); return; }
    }
    m_fluids.push_back(def);
    refresh();
}

void FluidRegistry::registerReaction(FluidReaction r) {
    if (r.fluid.empty() || r.touching.empty()) return;
    for (auto& existing : m_reactions) {
        if (existing.fluid == r.fluid && existing.touching == r.touching) { existing = r; return; }
    }
    m_reactions.push_back(r);
}

void FluidRegistry::refresh() {
    auto& blocks = BlockRegistry::getInstance();
    m_refreshed = true;
    m_byBlock.fill(-1);
    for (size_t i = 0; i < m_fluids.size(); ++i) {
        FluidDef& f = m_fluids[i];
        f.sourceId = blocks.getBlockId(f.sourceBlock);
        f.flowingId = f.flowingBlock.empty() ? 0 : blocks.getBlockId(f.flowingBlock);
        if (f.flowingId == 0) f.flowingId = f.sourceId;   // no flowing block: source block + metadata
        if (f.sourceId != 0) m_byBlock[f.sourceId] = static_cast<int>(i);
        if (f.flowingId != 0) m_byBlock[f.flowingId] = static_cast<int>(i);
    }
}

const FluidDef* FluidRegistry::byName(const std::string& name) const {
    for (const auto& f : m_fluids) if (f.name == name) return &f;
    return nullptr;
}

const FluidReaction* FluidRegistry::reaction(const std::string& fluid, const std::string& touching) const {
    for (const auto& r : m_reactions) if (r.fluid == fluid && r.touching == touching) return &r;
    return nullptr;
}

FluidCell FluidRegistry::cell(uint8_t blockId, uint8_t meta) const {
    FluidCell c;
    if (blockId == 0) return c;
    c.fluid = forBlock(blockId);
    if (!c.fluid) {
        // A waterlogged slab or stairs holds a water source (BlockShape.h)
        if (shape::isWaterlogged(BlockRegistry::getInstance().getBlock(blockId), meta)) {
            c.fluid = byName("water");
            c.source = c.fluid != nullptr;
            c.held = c.source;
        }
        return c;
    }
    const bool separateFlowing = c.fluid->flowingId != c.fluid->sourceId;
    c.source = blockId == c.fluid->sourceId && (separateFlowing || (meta & 0x0F) == 0);
    if (!c.source) {
        c.falling = (meta & FluidSimulator::kFallingBit) != 0;
        c.level = std::clamp(meta & 0x07, 1, c.fluid->spread);
    }
    return c;
}

float FluidRegistry::surfaceHeight(const FluidCell& c) {
    constexpr float kSourceHeight = 8.0f / 9.0f;
    if (!c.fluid || c.source || c.falling) return kSourceHeight;
    // Water (spread 7): 7/9 .. 1/9.  Lava (spread 3): 6/9, 4/9, 2/9 - as in Minecraft.
    return kSourceHeight * (1.0f - static_cast<float>(c.level) / static_cast<float>(c.fluid->spread + 1));
}

// ===========================================================================
// FluidSimulator
// ===========================================================================

FluidCell FluidSimulator::cellAt(const glm::ivec3& p) const {
    FluidRegistry::get().ensureFresh();
    return FluidRegistry::get().cell(m_world.getBlockAt(p), m_world.getMetadataAt(p));
}

size_t FluidSimulator::activeCount() const {
    size_t n = 0;
    for (const auto& [name, cells] : m_active) n += cells.size();
    return n;
}

void FluidSimulator::clear() {
    m_active.clear();
    m_timers.clear();
}

void FluidSimulator::noteChange(const glm::ivec3& loc) {
    if (m_world.isRemote()) return;   // the server runs fluids
    auto& reg = FluidRegistry::get();
    reg.ensureFresh();
    for (const auto& o : kWakeOffsets) {
        const glm::ivec3 p(loc.x + o[0], loc.y + o[1], loc.z + o[2]);
        const FluidCell c = reg.cell(m_world.getBlockAt(p), m_world.getMetadataAt(p));   // (waterlogged too)
        if (c) m_active[c.fluid->name].insert(p);
    }
}

void FluidSimulator::update(float dt) {
    auto& reg = FluidRegistry::get();
    reg.refresh();
    if (m_world.isRemote()) { m_active.clear(); return; }
    for (const auto& fluid : reg.fluids()) {
        if (fluid.sourceId == 0) continue;            // its blocks aren't registered
        float& timer = m_timers[fluid.name];
        timer += dt;
        if (timer < fluid.tickSeconds) continue;
        timer -= fluid.tickSeconds;
        if (timer > fluid.tickSeconds) timer = 0.0f;   // after a long hitch: don't catch up
        step(fluid);
    }
}

bool FluidSimulator::canReplace(const glm::ivec3& p) const {
    if (p.y < 0 || p.y >= CHUNK_HEIGHT || !m_world.isLoaded(p)) return false;   // unloaded = wall
    const uint8_t id = m_world.getBlockAt(p);
    if (id == 0) return true;
    if (FluidRegistry::get().forBlock(id)) return false;
    const auto& prop = BlockRegistry::getInstance().getBlock(id);
    // Dry plants get washed away; waterlogged ones (metadata bit 0) already hold water
    return prop.pattern == "plantlike" && (m_world.getMetadataAt(p) & 1) == 0;
}

bool FluidSimulator::isHole(const glm::ivec3& p, const FluidDef& fluid) const {
    const glm::ivec3 below(p.x, p.y - 1, p.z);
    if (canReplace(below)) return true;
    const FluidCell c = cellAt(below);
    return c && c.fluid->name == fluid.name;
}

// Minecraft's "slope finding": from a cell the fluid could spread into, how many more steps
// across passable ground until a place where it can fall? Breadth-first, limited distance.
int FluidSimulator::slopeDistance(const glm::ivec3& start, const FluidDef& fluid) const {
    if (isHole(start, fluid)) return 0;
    std::vector<glm::ivec3> frontier{start};
    std::unordered_set<glm::ivec3, IVec3Hash> seen{start};
    for (int depth = 1; depth <= fluid.slopeDistance; ++depth) {
        std::vector<glm::ivec3> next;
        for (const auto& c : frontier) {
            for (const auto& d : kHorizontal) {
                const glm::ivec3 n(c.x + d[0], c.y, c.z + d[1]);
                if (!seen.insert(n).second) continue;
                const FluidCell f = cellAt(n);
                const bool sameFlowing = f && f.fluid->name == fluid.name && !f.source;
                if (!canReplace(n) && !sameFlowing) continue;   // wall, source, other fluid
                if (isHole(n, fluid)) return depth;
                next.push_back(n);
            }
        }
        frontier.swap(next);
        if (frontier.empty()) break;
    }
    return kNoDrop;
}

void FluidSimulator::step(const FluidDef& fluid) {
    auto activeIt = m_active.find(fluid.name);
    if (activeIt == m_active.end() || activeIt->second.empty()) return;
    auto& active = activeIt->second;
    const auto& reg = FluidRegistry::get();
    auto& blocks = BlockRegistry::getInstance();

    std::vector<glm::ivec3> batch;
    batch.reserve(std::min<size_t>(active.size(), kMaxCellsPerStep));
    for (auto it = active.begin(); it != active.end() && batch.size() < static_cast<size_t>(kMaxCellsPerStep);) {
        batch.push_back(*it);
        it = active.erase(it);
    }

    // Changes are collected, then applied together. Two cells pouring into the same spot:
    // the stronger wins (a reaction's block > source > falling > lower level > drying up).
    struct Change { uint8_t id; uint8_t meta; int strength; };
    std::unordered_map<glm::ivec3, Change, IVec3Hash> changes;
    auto propose = [&](const glm::ivec3& p, uint8_t id, uint8_t meta, int strength) {
        auto [it, inserted] = changes.emplace(p, Change{id, meta, strength});
        if (!inserted && strength > it->second.strength) it->second = Change{id, meta, strength};
    };
    const uint8_t flowingId = fluid.flowingId;
    auto proposeFlowing = [&](const glm::ivec3& p, uint8_t meta) {
        const int strength = (meta & kFallingBit) ? 50 : 10 - (meta & 0x07);
        propose(p, flowingId, meta, strength);
    };
    auto isSame = [&fluid](const FluidCell& c) { return c && c.fluid->name == fluid.name; };

    for (const glm::ivec3& p : batch) {
        const FluidCell self = cellAt(p);
        if (!isSame(self) || p.y <= 0) continue;
        // Water held in a slab or stairs: flows out like a source, but the block stays (no
        // reacting with lava into stone), and never down through a solid bottom
        const bool held = self.held;

        // 0. Touching another fluid it reacts with? Then it turns into a block.
        bool reacted = false;
        const int touch[5][3] = { {1, 0, 0}, {-1, 0, 0}, {0, 0, 1}, {0, 0, -1}, {0, 1, 0} };
        for (const auto& o : touch) {
            const FluidCell other = cellAt({p.x + o[0], p.y + o[1], p.z + o[2]});
            if (!other || other.fluid->name == fluid.name) continue;
            const FluidReaction* r = reg.reaction(fluid.name, other.fluid->name);
            if (!r || held) continue;
            uint8_t id = blocks.getBlockId(self.source ? r->sourceBecomes : r->flowingBecomes);
            if (id == 0) id = blocks.getBlockId("stone");      // the game has no such block
            if (id == 0) continue;
            propose(p, id, 0, 200);
            reacted = true;
            break;
        }
        if (reacted) continue;

        // 1. Flowing: what should this cell be, given its neighbours?
        if (!self.source) {
            uint8_t wantId = flowingId, wantMeta = 0;
            if (isSame(cellAt({p.x, p.y + 1, p.z}))) {
                wantMeta = kFallingBit;                          // a fall keeps falling
            } else {
                int best = 99, sources = 0;
                for (const auto& d : kHorizontal) {
                    const FluidCell n = cellAt({p.x + d[0], p.y, p.z + d[1]});
                    if (!isSame(n)) continue;
                    if (n.source) ++sources;
                    best = std::min(best, (n.source || n.falling) ? 0 : n.level);
                }
                const glm::ivec3 below(p.x, p.y - 1, p.z);
                const FluidCell under = cellAt(below);
                const bool solidBelow = !under && !canReplace(below) && m_world.isLoaded(below);
                if (fluid.infinite && sources >= 2 && (solidBelow || (isSame(under) && under.source))) {
                    wantId = fluid.sourceId;                     // infinite source
                } else if (best + 1 > fluid.spread) {
                    wantId = 0;                                  // nothing feeds it: dry up
                } else {
                    wantMeta = static_cast<uint8_t>(best + 1);
                }
            }
            const uint8_t curId = m_world.getBlockAt(p), curMeta = m_world.getMetadataAt(p);
            if (wantId != curId || (wantId != 0 && wantMeta != (curMeta & 0x0F))) {
                const int strength = wantId == 0 ? 0 : wantId == fluid.sourceId && wantMeta == 0 ? 100
                                   : (wantMeta & kFallingBit) ? 50 : 10 - (wantMeta & 0x07);
                propose(p, wantId, wantMeta, strength);
                continue;                                        // spreads once it has settled
            }
        }

        // 2. Down first: into air/plants, or turning thinner flowing fluid below into a fall.
        //    Fluid that can fall only spreads sideways too when 3+ sources surround it.
        const glm::ivec3 below(p.x, p.y - 1, p.z);
        const FluidCell under = cellAt(below);
        bool canFall = canReplace(below) || (isSame(under) && !under.source && !under.falling);
        if (held && shape::fullBottom(blocks.getBlock(m_world.getBlockAt(p)), m_world.getMetadataAt(p))) canFall = false;
        if (canFall) {
            proposeFlowing(below, kFallingBit);
            int sources = 0;
            for (const auto& d : kHorizontal) {
                const FluidCell n = cellAt({p.x + d[0], p.y, p.z + d[1]});
                if (isSame(n) && n.source) ++sources;
            }
            if (sources < 3) continue;
        } else if (!self.source && isHole(p, fluid)) {
            continue;                                            // resting on the same fluid
        }

        // 3. Sideways, one level thinner, towards the nearest drop. The way to a drop is
        //    measured through fluid already flowing there, so it doesn't turn and spread
        //    everywhere once it's on its way.
        const int sideLevel = (self.source || self.falling) ? 1 : self.level + 1;
        if (sideLevel > fluid.spread) continue;
        int dist[4];
        bool canFill[4];
        int nearest = kNoDrop;
        for (int i = 0; i < 4; ++i) {
            dist[i] = kNoDrop + 1;
            canFill[i] = false;
            const glm::ivec3 n(p.x + kHorizontal[i][0], p.y, p.z + kHorizontal[i][1]);
            const FluidCell f = cellAt(n);
            const bool replace = canReplace(n);
            if (!replace && !(isSame(f) && !f.source)) continue;
            canFill[i] = replace || (!f.falling && f.level > sideLevel);
            dist[i] = slopeDistance(n, fluid);
            nearest = std::min(nearest, dist[i]);
        }
        for (int i = 0; i < 4; ++i) {
            if (!canFill[i] || dist[i] > kNoDrop || dist[i] != nearest) continue;
            proposeFlowing({p.x + kHorizontal[i][0], p.y, p.z + kHorizontal[i][1]}, static_cast<uint8_t>(sideLevel));
        }
    }

    for (const auto& [pos, c] : changes) {
        if (m_world.getBlockAt(pos) == c.id && (c.id == 0 || (m_world.getMetadataAt(pos) & 0x0F) == c.meta)) continue;
        m_world.setBlockWithMetaAt(pos, c.id, c.meta);   // wakes the neighbours (noteChange)
    }
}

// ===========================================================================
// Lua API
// ===========================================================================
//
// engine.register_fluid{
//     name = "lava",                 -- required
//     source = "lava",               -- source block (default: name)
//     flowing = "flowing_lava",      -- flowing block (default: none -> source block + metadata)
//     spread = 3,                    -- blocks across flat ground, 1..7 (default 7)
//     tick = 1.5,                    -- seconds per flow step (default 0.25)
//     slope_distance = 2,            -- how far it looks for a drop (default 4)
//     infinite = false,              -- 2 sources + gap = new source (default true)
//     swimmable = true,              -- players swim in it (default true)
//     swim_speed = 1.0,              -- blocks/s while in it (default 2.2)
//     ...any other string fields (e.g. bucket = "lava_bucket") are kept and returned by
//     engine.list_fluids(), for other mods to use, plus mod_path (the registering mod's folder).
// }
// engine.register_fluid_reaction{ fluid = "lava", touching = "water",
//                                 source_becomes = "obsidian", flowing_becomes = "cobblestone" }
// engine.get_fluid(x, y, z)  -> nil, or { name, block, level, source, falling }
// engine.is_fluid_source(x, y, z) -> true for a full source of any fluid
// engine.list_fluids()       -> { { name, source, flowing, spread, tick, ... }, ... }

namespace {
std::unordered_map<std::string, std::unordered_map<std::string, std::string>> g_fluidExtras;
}

void addFluidLuaApi(sol::state& /*lua*/, sol::table engine, std::function<World*()> getWorld) {
    engine["register_fluid"] = [](sol::table t, sol::this_state s) {
        FluidDef def;
        def.name = t.get_or("name", std::string());
        if (def.name.empty()) {
            std::cerr << "[Fluids] register_fluid needs a name" << std::endl;
            return;
        }
        def.sourceBlock = t.get_or("source", def.name);
        def.flowingBlock = t.get_or("flowing", std::string());
        def.spread = t.get_or("spread", 7);
        def.tickSeconds = t.get_or("tick", 0.25f);
        def.slopeDistance = t.get_or("slope_distance", 4);
        def.infinite = t.get_or("infinite", true);
        def.swimmable = t.get_or("swimmable", true);
        def.swimSpeed = t.get_or("swim_speed", 2.2f);
        auto& extras = g_fluidExtras[def.name];
        extras.clear();
        t.for_each([&extras](const sol::object& k, const sol::object& v) {
            if (k.is<std::string>() && v.is<std::string>()) extras[k.as<std::string>()] = v.as<std::string>();
        });
        // The folder of the mod that registered it (its pictures, e.g. a bucket, are there)
        sol::state_view lua(s);
        sol::optional<std::string> modPath = lua["g_CurrentModPath"];
        if (modPath && !modPath->empty()) extras["mod_path"] = *modPath;
        std::cout << "[Fluids] Registered " << def.name << " (spreads " << def.spread << ", step "
                  << def.tickSeconds << " s)" << std::endl;
        FluidRegistry::get().registerFluid(def);
    };
    engine["register_fluid_reaction"] = [](sol::table t) {
        FluidReaction r;
        r.fluid = t.get_or("fluid", std::string());
        r.touching = t.get_or("touching", std::string());
        r.sourceBecomes = t.get_or("source_becomes", std::string("stone"));
        r.flowingBecomes = t.get_or("flowing_becomes", r.sourceBecomes);
        FluidRegistry::get().registerReaction(r);
    };
    engine["get_fluid"] = [getWorld](int x, int y, int z, sol::this_state s) -> sol::object {
        World* w = getWorld ? getWorld() : nullptr;
        if (!w) return sol::lua_nil;
        const glm::ivec3 p(x, y, z);
        const uint8_t id = w->getBlockAt(p);
        const FluidCell c = FluidRegistry::get().cell(id, w->getMetadataAt(p));
        if (!c) return sol::lua_nil;
        sol::state_view lua(s);
        sol::table t = lua.create_table();
        t["name"] = c.fluid->name;
        t["block"] = BlockRegistry::getInstance().getBlock(id).name;
        t["level"] = c.level;
        t["source"] = c.source;
        t["falling"] = c.falling;
        return sol::make_object(lua, t);
    };
    engine["is_fluid_source"] = [getWorld](int x, int y, int z) {
        World* w = getWorld ? getWorld() : nullptr;
        if (!w) return false;
        const glm::ivec3 p(x, y, z);
        return FluidRegistry::get().cell(w->getBlockAt(p), w->getMetadataAt(p)).source;
    };
    engine["list_fluids"] = [](sol::this_state s) {
        sol::state_view lua(s);
        sol::table list = lua.create_table();
        int i = 1;
        for (const auto& f : FluidRegistry::get().fluids()) {
            sol::table t = lua.create_table();
            for (const auto& [k, v] : g_fluidExtras[f.name]) t[k] = v;
            t["name"] = f.name;
            t["source"] = f.sourceBlock;
            t["flowing"] = f.flowingBlock;
            t["spread"] = f.spread;
            t["tick"] = f.tickSeconds;
            t["slope_distance"] = f.slopeDistance;
            t["infinite"] = f.infinite;
            t["swimmable"] = f.swimmable;
            list[i++] = t;
        }
        return list;
    };
}

void FluidRegistry::reset() {
    *this = FluidRegistry();
    g_fluidExtras.clear();
}
