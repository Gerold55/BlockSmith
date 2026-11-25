#pragma once
// The worlds of a world's dimensions (Dimensions.h), on whoever runs the world: the dedicated
// server, or the game in singleplayer / when hosting. Like Minecraft, a dimension is loaded
// while someone is in it (or a mod asked for it), runs on its own - its own chunks, fluids,
// falling blocks, block modifiers and mobs - and is saved and unloaded a while after it's left
// empty. `primary` is the owner's own world, always there: the overworld on the dedicated
// server; in the game, the dimension its player is in (the others run here, in the background,
// for guests - and the overworld too, while the host is somewhere else).
//
// Each dimension saves into saves/<world>/dimensions/<name>/ (chunks/, lbm_done.txt).

#include "Dimensions.h"
#include "World.h"
#include "StructureAPI.h"
#include "ChunkGenerator.h"
#include <sol/sol.hpp>
#include <filesystem>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <glm/glm.hpp>

class DimensionWorlds {
public:
    // Set by the owner before use
    World* primary = nullptr;
    ChunkGenerator* generator = nullptr;
    std::string worldDir;                 // the world's folder ("" = nothing is saved: a joined game)
    int renderDistance = 8;               // chunks kept loaded around each player in a dimension
    float loadBudgetMs = 4.0f;
    // Called when a dimension's world was just created (register it with the network server,
    // load its block-modifier record...) and just before one is unloaded
    std::function<void(const std::string& dimension, World& world)> onLoaded, onUnloading;

    // The world of a dimension, loading it if it's registered (nullptr: no such dimension)
    World* get(const std::string& nameIn) {
        const std::string name = norm(nameIn);
        if (World* w = find(name)) return w;
        if (!primary || (!name.empty() && !dims::Registry::get().has(name))) return nullptr;
        auto w = std::make_unique<World>();
        w->setDimension(name);
        w->setSeed(primary->seed());
        w->setLighting(primary->lighting());
        w->setRenderDistance(renderDistance);
        w->setLoadBudgetMs(loadBudgetMs);
        if (!worldDir.empty()) {
            const std::string dir = (std::filesystem::path(worldDir) / dims::folderOf(name)).string();
            std::error_code ec;
            std::filesystem::create_directories(std::filesystem::path(dir) / "chunks", ec);
            w->setSaveDirectory(dir);
        }
        if (generator) w->setGenerator(generator);
        World& ref = *w;
        m_worlds[name] = Entry{ std::move(w), 0.0, true };
        std::cout << "[Dimension] Loaded " << (name.empty() ? std::string(dims::kOverworld) : name) << std::endl;
        if (onLoaded) onLoaded(name, ref);
        return &ref;
    }
    // Only if it's loaded already
    World* find(const std::string& nameIn) const {
        const std::string name = norm(nameIn);
        if (primary && primary->dimension() == name) return primary;
        const auto it = m_worlds.find(name);
        return it == m_worlds.end() ? nullptr : it->second.world.get();
    }
    std::vector<std::string> loaded() const {
        std::vector<std::string> out;
        for (const auto& [n, e] : m_worlds) out.push_back(n);
        return out;
    }
    template <typename Fn> void forEach(Fn&& fn) { for (auto& [n, e] : m_worlds) fn(n, *e.world); }

    // Every tick: each loaded dimension loads / generates around its players, flows its fluids,
    // and is unloaded once it's been empty for its unload_after seconds. players: where each
    // dimension's players are (feet).
    void update(sol::state& lua, float dt, double now,
                const std::map<std::string, std::vector<glm::vec3>>& players) {
        std::vector<std::string> idle;
        for (auto& [name, e] : m_worlds) {
            const auto it = players.find(name);
            const bool anyone = it != players.end() && !it->second.empty();
            // (each dimension's own unload_after; < 0: it stays loaded)
            const double after = e.world->dimensionType().unloadAfter;
            if (anyone || e.keep || after < 0.0) e.emptySince = now;
            else if (now - e.emptySince > after) { idle.push_back(name); continue; }
            e.keep = false;
            const glm::vec3 center = anyone ? it->second.front() : e.lastCenter;
            if (anyone) {
                e.lastCenter = center;
                std::vector<World::LoadCenter> extra;
                for (size_t i = 1; i < it->second.size(); ++i) extra.push_back({ it->second[i], renderDistance });
                e.world->setExtraLoadCenters(extra);
            }
            e.world->update(center, lua);
            e.world->updateFluids(dt);
            structure::runMoveCallbacks(lua, *e.world);   // (moves placed: mods' on_done)
        }
        for (const auto& name : idle) unload(name);
    }
    // Keep a dimension loaded this tick even with nobody in it (a mod is building a portal there)
    void touch(const std::string& nameIn) {
        const auto it = m_worlds.find(norm(nameIn));
        if (it != m_worlds.end()) it->second.keep = true;
    }

    int saveAll() {
        int n = 0;
        for (auto& [name, e] : m_worlds) n += e.world->saveAll();
        return n;
    }
    void unload(const std::string& nameIn) {
        const std::string name = norm(nameIn);
        const auto it = m_worlds.find(name);
        if (it == m_worlds.end()) return;
        if (onUnloading) onUnloading(name, *it->second.world);
        it->second.world->finishFalling();   // (nothing left in the air or on its way)
        it->second.world->finishMoves();
        it->second.world->saveAll();
        std::cout << "[Dimension] Unloaded " << (name.empty() ? std::string(dims::kOverworld) : name) << std::endl;
        m_worlds.erase(it);
    }
    void unloadAll() {
        while (!m_worlds.empty()) unload(m_worlds.begin()->first);
    }

    static std::string norm(const std::string& n) { return (n == dims::kOverworld) ? std::string() : n; }

private:
    struct Entry {
        std::unique_ptr<World> world;
        double emptySince = 0.0;
        bool keep = false;
        glm::vec3 lastCenter{0.0f, 80.0f, 0.0f};
    };
    std::map<std::string, Entry> m_worlds;
};
