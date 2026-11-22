// BlockModifiers.cpp: ABMs and LBMs (see BlockModifiers.h)
#include "BlockModifiers.h"
#include "Block.h"
#include "ChunkData.h"
#include "LuaSandbox.h"
#include "Types.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

namespace fs = std::filesystem;

namespace {

std::vector<std::string> nameList(const sol::object& o) {
    std::vector<std::string> out;
    if (o.is<std::string>()) out.push_back(o.as<std::string>());
    else if (o.is<sol::table>()) {
        for (const auto& kv : o.as<sol::table>()) if (kv.second.is<std::string>()) out.push_back(kv.second.as<std::string>());
    }
    return out;
}

void fillIds(const std::vector<std::string>& names, std::array<bool, 256>& ids, bool* any = nullptr) {
    ids.fill(false);
    auto& reg = BlockRegistry::getInstance();
    bool found = false;
    for (const auto& n : names) {
        if (n == "air") { ids[0] = true; found = true; continue; }
        const uint8_t id = reg.getBlockId(n);
        if (id != 0) { ids[id] = true; found = true; }
    }
    if (any) *any = found || !names.empty();
}

int floorDiv16(int v) { return v >= 0 ? v / CHUNK_WIDTH : -((-v + CHUNK_WIDTH - 1) / CHUNK_WIDTH); }

} // namespace

void BlockModifiers::bind(sol::state& lua, sol::table engine) {
    m_lua = &lua;
    BlockModifiers* self = this;
    // engine.register_random_tick{ label, nodenames, action = function(x, y, z, name, meta) end }
    engine["register_random_tick"] = [self](sol::table def) {
        RandomTick t;
        t.label = def.get_or("label", std::string("(no label)"));
        t.nodes = nameList(def["nodenames"]);
        sol::object action = def["action"];
        if (t.nodes.empty() || action.get_type() != sol::type::function) {
            std::cerr << "[Random tick] " << t.label << ": needs nodenames and an action function" << std::endl;
            return;
        }
        t.action = action.as<sol::protected_function>();
        self->m_randomTicks.push_back(std::move(t));
        self->m_resolvedFor = 0;
    };
    // engine.register_abm{ label, nodenames, neighbors, without_neighbors, interval, chance,
    //                      min_y, max_y, action = function(x, y, z, name, meta) end }
    engine["register_abm"] = [self](sol::table def) {
        ABM a;
        a.label = def.get_or("label", std::string("(no label)"));
        a.nodes = nameList(def["nodenames"]);
        a.neighbors = nameList(def["neighbors"]);
        a.without = nameList(def["without_neighbors"]);
        a.interval = std::max(0.1f, def.get_or("interval", 10.0f));
        a.chance = std::max(1, def.get_or("chance", 50));
        a.minY = def.get_or("min_y", a.minY);
        a.maxY = def.get_or("max_y", a.maxY);
        sol::object action = def["action"];
        if (a.nodes.empty() || action.get_type() != sol::type::function) {
            std::cerr << "[ABM] " << a.label << ": needs nodenames and an action function" << std::endl;
            return;
        }
        a.action = action.as<sol::protected_function>();
        a.timer = static_cast<float>(self->random() % 1000) / 1000.0f * a.interval;   // (not all at once)
        self->m_abms.push_back(std::move(a));
        self->m_resolvedFor = 0;
    };
    // engine.register_lbm{ name, nodenames, run_at_every_load, action = function(x, y, z, name, meta) end }
    engine["register_lbm"] = [self](sol::table def) {
        LBM l;
        l.name = def.get_or("name", std::string());
        l.nodes = nameList(def["nodenames"]);
        l.everyLoad = def.get_or("run_at_every_load", false);
        sol::object action = def["action"];
        if (l.name.empty() || l.nodes.empty() || action.get_type() != sol::type::function) {
            std::cerr << "[LBM] needs a name (\"mymod:what_it_does\"), nodenames and an action function" << std::endl;
            return;
        }
        l.action = action.as<sol::protected_function>();
        self->m_lbms.push_back(std::move(l));
        self->m_resolvedFor = 0;
    };
}

void BlockModifiers::resolve() {
    const size_t n = BlockRegistry::getInstance().getAllBlocks().size();
    if (n == m_resolvedFor) return;
    m_resolvedFor = n;
    for (auto& a : m_abms) {
        fillIds(a.nodes, a.isNode);
        fillIds(a.neighbors, a.isNeighbor, &a.anyNeighbor);
        fillIds(a.without, a.isWithout, &a.anyWithout);
        a.anyNeighbor = !a.neighbors.empty();
        a.anyWithout = !a.without.empty();
    }
    for (auto& l : m_lbms) fillIds(l.nodes, l.isNode);
    for (auto& v : m_tickByBlock) v.clear();
    m_anyTickable = false;
    for (size_t i = 0; i < m_randomTicks.size(); ++i) {
        std::array<bool, 256> ids{};
        fillIds(m_randomTicks[i].nodes, ids);
        for (int id = 0; id < 256; ++id) {
            if (ids[id]) { m_tickByBlock[id].push_back(static_cast<int>(i)); m_anyTickable = true; }
        }
    }
}

// Minecraft's random ticks: per 1/20 s, `speed` random blocks in every 16x16x16 section of the
// chunks near players. Most picks are air or stone (nothing to do: one array lookup); the few
// that land on grass, crops, saplings... run their action.
void BlockModifiers::randomTicks(float dt, World& world, const std::vector<glm::vec3>& players) {
    if (!m_anyTickable || players.empty()) return;
    const int speed = std::clamp(randomTickSpeed ? randomTickSpeed() : 3, 0, 4096);
    if (speed <= 0) { m_tickCarry = 0.0; return; }
    m_tickCarry += dt * 20.0;                           // game ticks this frame (Minecraft: 20 a second)
    int ticks = static_cast<int>(m_tickCarry);
    if (ticks <= 0) return;
    m_tickCarry -= ticks;
    ticks = std::min(ticks, 10);                        // (after a long pause: don't run minutes at once)
    std::set<std::pair<int, int>> chunks;
    for (const auto& p : players) {
        const int pcx = floorDiv16(static_cast<int>(std::floor(p.x)));
        const int pcz = floorDiv16(static_cast<int>(std::floor(p.z)));
        for (int dx = -randomTickRadius; dx <= randomTickRadius; ++dx)
            for (int dz = -randomTickRadius; dz <= randomTickRadius; ++dz) chunks.insert({pcx + dx, pcz + dz});
    }
    auto& reg = BlockRegistry::getInstance();
    struct Hit { int tick; int x, y, z; };
    std::vector<Hit> hits;
    for (const auto& c : chunks) {
        const ChunkData* chunk = world.chunkData(World::ChunkCoord{c.first, c.second});
        if (!chunk) continue;
        for (int section = 0; section < CHUNK_HEIGHT / 16; ++section) {
            for (int n = 0; n < ticks * speed; ++n) {
                const uint32_t r = random();
                const int x = r & 15, z = (r >> 4) & 15, y = section * 16 + ((r >> 8) & 15);
                const uint8_t id = chunk->getLocalBlock(x, y, z);
                for (int t : m_tickByBlock[id]) hits.push_back({t, c.first * CHUNK_WIDTH + x, y, c.second * CHUNK_WIDTH + z});
            }
        }
    }
    for (const Hit& h : hits) {
        const glm::ivec3 pos(h.x, h.y, h.z);
        const uint8_t id = world.getBlockAt(pos);
        RandomTick& t = m_randomTicks[h.tick];
        bool still = false;
        for (int i : m_tickByBlock[id]) if (i == h.tick) still = true;
        if (!still) continue;                            // changed by an earlier one this tick
        const std::string& name = id == 0 ? std::string("air") : reg.getBlock(id).name;
        luasandbox::TimeLimit limit(timeLimit, "random tick");
        auto r = t.action(h.x, h.y, h.z, name, static_cast<int>(world.getMetadataAt(pos)));
        ++m_randomTicksRun;
        if (!r.valid()) { sol::error err = r; report("random tick \"" + t.label + "\"", err.what()); }
    }
}

void BlockModifiers::report(const std::string& where, const std::string& message) {
    std::string& last = m_lastError[where];
    if (last == message) return;
    last = message;
    std::cerr << "[Lua] " << where << ": " << message << std::endl;
}

void BlockModifiers::update(float dt, World& world, const std::vector<glm::vec3>& players) {
    if (!m_lua || world.isRemote()) return;
    World::NaturalChanges nature;   // ABMs, LBMs and random ticks: nature, not building
    resolve();

    // ---- LBMs: chunks that just arrived ----
    for (const auto& [c, fromDisk] : world.takeLoadedChunks()) runLBMs(world, c, fromDisk);

    // ---- Random ticks (Minecraft's): a little every tick ----
    randomTicks(dt, world, players);

    if (m_abms.empty()) return;
    // ---- ABMs: whose interval is up ----
    for (auto& a : m_abms) a.timer += dt;
    if (m_scanNext >= m_scanChunks.size()) {
        // start a new scan with every ABM that's due
        std::vector<int> due;
        for (size_t i = 0; i < m_abms.size(); ++i) {
            if (m_abms[i].timer >= m_abms[i].interval) {
                m_abms[i].timer = std::fmod(m_abms[i].timer, m_abms[i].interval);
                due.push_back(static_cast<int>(i));
            }
        }
        if (due.empty() || players.empty()) return;
        // the chunks near players (each once), nearest first
        std::set<std::pair<int, int>> set;
        for (const auto& p : players) {
            const int pcx = floorDiv16(static_cast<int>(std::floor(p.x)));
            const int pcz = floorDiv16(static_cast<int>(std::floor(p.z)));
            for (int dx = -activeRadius; dx <= activeRadius; ++dx)
                for (int dz = -activeRadius; dz <= activeRadius; ++dz) set.insert({pcx + dx, pcz + dz});
        }
        m_scanChunks.assign(set.begin(), set.end());
        m_scanNext = 0;
        m_scanDue = due;
        for (auto& v : m_byBlock) v.clear();
        for (int i : due) {
            for (int id = 0; id < 256; ++id) if (m_abms[i].isNode[id]) m_byBlock[id].push_back(i);
        }
    }
    // ---- carry on scanning, a few milliseconds per tick ----
    const auto start = std::chrono::steady_clock::now();
    while (m_scanNext < m_scanChunks.size()) {
        scanChunk(world, m_scanChunks[m_scanNext++], m_scanDue);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        if (ms >= budgetMs) break;
    }
}

void BlockModifiers::scanChunk(World& world, const World::ChunkCoord& c, const std::vector<int>& due) {
    const ChunkData* chunk = world.chunkData(c);
    if (!chunk) return;
    auto& reg = BlockRegistry::getInstance();
    const int bx = c.first * CHUNK_WIDTH, bz = c.second * CHUNK_WIDTH;
    int lo = CHUNK_HEIGHT, hi = -1;
    for (int i : due) { lo = std::min(lo, std::max(0, m_abms[i].minY)); hi = std::max(hi, std::min(CHUNK_HEIGHT - 1, m_abms[i].maxY)); }
    if (hi < lo) return;

    struct Hit { int abm; int x, y, z; };
    std::vector<Hit> hits;
    for (int x = 0; x < CHUNK_WIDTH; ++x) {
        for (int z = 0; z < CHUNK_WIDTH; ++z) {
            for (int y = lo; y <= hi; ++y) {
                const uint8_t id = chunk->getLocalBlock(x, y, z);
                const auto& wanting = m_byBlock[id];
                if (wanting.empty()) continue;
                for (int i : wanting) {
                    const ABM& a = m_abms[i];
                    if (y < a.minY || y > a.maxY) continue;
                    if (random() % static_cast<uint32_t>(a.chance) != 0) continue;   // 1 in `chance`
                    // neighbours: the 26 blocks around it (getBlock looks into the next chunks)
                    if (a.anyNeighbor || a.anyWithout) {
                        bool has = false, hasWithout = false;
                        for (int dx = -1; dx <= 1; ++dx) for (int dy = -1; dy <= 1; ++dy) for (int dz = -1; dz <= 1; ++dz) {
                            if (!dx && !dy && !dz) continue;
                            const int ny = y + dy;
                            if (ny < 0 || ny >= CHUNK_HEIGHT) continue;
                            const uint8_t n = chunk->getBlock(x + dx, ny, z + dz);
                            if (a.isNeighbor[n]) has = true;
                            if (a.isWithout[n]) hasWithout = true;
                        }
                        if (a.anyNeighbor && !has) continue;
                        if (a.anyWithout && hasWithout) continue;
                    }
                    hits.push_back({i, bx + x, y, bz + z});
                }
            }
        }
    }
    // The actions run after the scan (they may change this chunk)
    for (const Hit& h : hits) {
        const glm::ivec3 pos(h.x, h.y, h.z);
        const uint8_t id = world.getBlockAt(pos);
        ABM& a = m_abms[h.abm];
        if (!a.isNode[id]) continue;                    // changed by an earlier action
        const std::string& name = id == 0 ? std::string("air") : reg.getBlock(id).name;
        luasandbox::TimeLimit limit(timeLimit, "ABM");
        auto r = a.action(h.x, h.y, h.z, name, static_cast<int>(world.getMetadataAt(pos)));
        if (!r.valid()) { sol::error err = r; report("ABM \"" + a.label + "\"", err.what()); }
    }
}

void BlockModifiers::runLBMs(World& world, const World::ChunkCoord& c, bool fromDisk) {
    if (m_lbms.empty()) return;
    const ChunkData* chunk = world.chunkData(c);
    if (!chunk) return;
    auto& reg = BlockRegistry::getInstance();
    const std::pair<int, int> key{c.first, c.second};
    std::vector<LBM*> run;
    for (auto& l : m_lbms) {
        if (l.everyLoad) {
            if (fromDisk) run.push_back(&l);            // (a brand-new chunk has nothing to catch up on)
            continue;
        }
        auto& done = m_lbmDone[l.name];
        if (done.count(key)) continue;
        done.insert(key);
        m_lbmDirty = true;
        if (fromDisk) run.push_back(&l);               // new chunks never need upgrading: just noted
    }
    if (run.empty()) return;
    struct Hit { LBM* lbm; int x, y, z; };
    std::vector<Hit> hits;
    const int bx = c.first * CHUNK_WIDTH, bz = c.second * CHUNK_WIDTH;
    for (int x = 0; x < CHUNK_WIDTH; ++x)
        for (int z = 0; z < CHUNK_WIDTH; ++z)
            for (int y = 0; y < CHUNK_HEIGHT; ++y) {
                const uint8_t id = chunk->getLocalBlock(x, y, z);
                for (LBM* l : run) if (l->isNode[id]) hits.push_back({l, bx + x, y, bz + z});
            }
    for (const Hit& h : hits) {
        const glm::ivec3 pos(h.x, h.y, h.z);
        const uint8_t id = world.getBlockAt(pos);
        if (!h.lbm->isNode[id]) continue;
        const std::string& name = id == 0 ? std::string("air") : reg.getBlock(id).name;
        luasandbox::TimeLimit limit(timeLimit, "LBM");
        auto r = h.lbm->action(h.x, h.y, h.z, name, static_cast<int>(world.getMetadataAt(pos)));
        if (!r.valid()) { sol::error err = r; report("LBM \"" + h.lbm->name + "\"", err.what()); }
    }
}

// lbm_done.txt: "name<TAB>cx,cz;cx,cz;..." one line per once-only LBM
void BlockModifiers::loadWorld(const std::string& worldDir) {
    clearWorld();
    std::ifstream f(fs::path(worldDir) / "lbm_done.txt");
    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const size_t tab = line.find('\t');
        if (tab == std::string::npos) continue;
        auto& done = m_lbmDone[line.substr(0, tab)];
        std::stringstream ss(line.substr(tab + 1));
        std::string item;
        while (std::getline(ss, item, ';')) {
            int cx = 0, cz = 0;
            if (std::sscanf(item.c_str(), "%d,%d", &cx, &cz) == 2) done.insert({cx, cz});
        }
    }
}

void BlockModifiers::saveWorld(const std::string& worldDir) const {
    if (worldDir.empty() || m_lbmDone.empty()) return;
    const fs::path tmp = fs::path(worldDir) / "lbm_done.txt.tmp";
    {
        std::ofstream f(tmp, std::ios::trunc);
        if (!f) return;
        for (const auto& [name, done] : m_lbmDone) {
            f << name << '\t';
            bool first = true;
            for (const auto& [cx, cz] : done) { f << (first ? "" : ";") << cx << ',' << cz; first = false; }
            f << '\n';
        }
    }
    std::error_code ec;
    fs::rename(tmp, fs::path(worldDir) / "lbm_done.txt", ec);
    if (ec) { fs::remove(fs::path(worldDir) / "lbm_done.txt", ec); fs::rename(tmp, fs::path(worldDir) / "lbm_done.txt", ec); }
}

void BlockModifiers::clearWorld() {
    m_lbmDone.clear();
    m_lbmDirty = false;
    m_scanChunks.clear();
    m_scanNext = 0;
    for (auto& a : m_abms) a.timer = static_cast<float>(random() % 1000) / 1000.0f * a.interval;
}
