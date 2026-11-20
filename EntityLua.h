#pragma once
// EntityLua: the `entities` table mods use (see docs/MODDING.md, "Mobs and other entities").
// The same code runs in the game and on the dedicated server; EntityLuaHost tells it about the
// place it runs in (who the players are, how to hurt one, give one an item...).
//
//   entities.register(name, def)        a kind of mob (hitbox, health, model, on_step, ...)
//   entities.spawn(name, x, y, z [, data]) -> self    (the authority only; nil elsewhere)
//   entities.get(id), entities.get_all([name]), entities.in_radius(x, y, z, r [, name])
//   entities.register_spawn{ name = ..., ... }        natural spawning (the authority's)
//   entities.nearest_player(x, y, z [, max]) -> name, x, y, z, distance
//   entities.get_players() -> { {name=, x=, y=, z=}, ... }
//   entities.damage_player(name, amount [, reason]), entities.drop_item(item, count, x, y, z)
//   entities.get_attack_damage(item, player) -> damage   (replace it to change weapons)
//
// A mob's own functions get `self`: a table that stays the same for its whole life (put your
// own fields on it), with self.id, self.name and the methods listed in kEntityLuaPrelude.

#include <sol/sol.hpp>
#include "Entities.h"
#include "World.h"
#include "LuaSandbox.h"
#include <cmath>
#include <functional>
#include <iostream>
#include <map>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

struct EntityLuaHost {
    EntityManager* manager = nullptr;
    const World* world = nullptr;
    // Singleplayer, hosting, the server: true. Joined to a server: false (only looks are used)
    std::function<bool()> isAuthority = [] { return true; };
    // Every player in the world: name and feet position
    std::function<std::vector<std::pair<std::string, glm::vec3>>()> players;
    std::function<void(const std::string& name, float amount, const std::string& reason)> damagePlayer;
    // Puts items in a player's inventory; false = no room (the item stays on the ground)
    std::function<bool(const std::string& name, const std::string& item, int count)> giveItem;
    std::function<int(int, int, int)> lightLevel;
    // Is the sky overhead at x, y, z? (spawn rules with sky = true). Left empty: the world's
    // sunlight decides - a world without light data (the dedicated server) needs this set.
    std::function<bool(int, int, int)> skyVisible;
    std::function<bool()> isDay;
    std::function<bool()> spawningEnabled = [] { return true; };
    // The game: load the model / animations for a newly registered type (name, its def)
    std::function<void(const std::string&, const sol::table&)> onRegister;
    double timeLimit = 2.0;   // seconds any one callback may run
};

// The `self` methods and the friendly API, on top of the engine's entities._* functions
inline const char* kEntityLuaPrelude = R"LUA(
local E = entities
local M = {}
M.__index = M
E._methods = M
E._active = {}
E._defs = {}

-- Where it is, and moving it
function M:get_pos() return E._pos(self.id) end
function M:set_pos(x, y, z) E._set_pos(self.id, x, y, z) end
function M:get_velocity() return E._vel(self.id) end
function M:set_velocity(x, y, z) E._set_vel(self.id, x, y, z) end
function M:add_velocity(x, y, z) local a, b, c = E._vel(self.id) E._set_vel(self.id, a + x, b + y, c + z) end
function M:get_yaw() return E._yaw(self.id) end
function M:set_yaw(deg) E._set_yaw(self.id, deg) end
-- Walks forward (the way it faces) at `speed` blocks/s; 0 stops. Jumps 1-block steps.
function M:walk(speed) E._walk(self.id, speed or 0) end
function M:stop() E._walk(self.id, 0) end
-- Finds a way there (round trees and walls, up 1-block steps, never through gaps it can't
-- fit) and follows it. Fine to call every tick: it only plans again when the goal moves.
-- Returns false if there's no way at all.
function M:go_to(x, y, z, speed) return E._go_to(self.id, x, y, z, speed or 1) end
-- "following", "arrived", "partial" (got as near as it could), "stuck" or "none"
function M:path_state() return E._path_state(self.id) end
function M:is_moving() local s = E._path_state(self.id) return s == "following" end
-- Walks to a random spot it can reach within `radius` blocks; false if none was found
function M:wander(radius, speed)
    local x, y, z = E._random_target(self.id, radius or 8)
    if not x then return false end
    return E._go_to(self.id, x, y, z, speed or 1)
end
-- The nearest dry land it can get to (x, y, z), or nil. (wander already heads there when it's
-- in water.)
function M:find_land(radius) return E._find_land(self.id, radius or 16) end
-- Walked into something it couldn't step or jump over (this tick)
function M:is_blocked() return E._blocked(self.id) end
-- Can it see that point? (a ray from its eyes through the blocks)
function M:can_see(x, y, z)
    local px, py, pz = E._pos(self.id)
    if not px then return false end
    return E._line_of_sight(px, py + E._eye(self.id), pz, x, y, z)
end
-- Can it see that player's head?
function M:can_see_player(name)
    for _, p in ipairs(E._players()) do
        if p.name == name then return self:can_see(p.x, p.y + 1.6, p.z) end
    end
    return false
end
-- Turns to face a point (yaw only)
function M:look_at(x, z)
    local px, _, pz = E._pos(self.id)
    if px then E._set_yaw(self.id, math.deg(math.atan(z - pz, x - px))) end
end
-- Turns towards a point and walks there; true once it's within `near` blocks
function M:walk_to(x, z, speed, near)
    local px, _, pz = E._pos(self.id)
    if not px then return false end
    if (x - px) ^ 2 + (z - pz) ^ 2 <= (near or 1) ^ 2 then E._walk(self.id, 0) return true end
    self:look_at(x, z)
    E._walk(self.id, speed or 1)
    return false
end
function M:distance_to(x, y, z)
    local px, py, pz = E._pos(self.id)
    if not px then return math.huge end
    return math.sqrt((x - px) ^ 2 + (y - py) ^ 2 + (z - pz) ^ 2)
end
function M:is_on_ground() local g = E._flags(self.id) return g end
function M:is_in_water() local _, w = E._flags(self.id) return w end

-- Looks
function M:set_animation(name, speed, loop) E._anim(self.id, name or "", speed or 1, loop ~= false) end
function M:get_animation() return E._get_anim(self.id) end
function M:set_nametag(text) E._nametag(self.id, text or "") end

-- Health
function M:get_health() return E._health(self.id) end
function M:get_max_health() return E._max_health(self.id) end
function M:set_health(h) E._set_health(self.id, h) end
-- Hurts it like a hit (red flash, knockback away from the attacker if given); 0 health = it dies
function M:damage(amount, attacker) E._damage(self.id, amount or 1, attacker) end
function M:remove() E._remove(self.id) end
function M:is_alive() return E._exists(self.id) end

-- Nearby players and entities
function M:nearest_player(max) local x, y, z = E._pos(self.id) if not x then return nil end return E.nearest_player(x, y, z, max) end

-- The friendly API
function E.register(name, def) E._register(name, def) E._defs[name] = def end
function E.get(id) return E._active[id] end
function E.get_all(name)
    local out = {}
    for id, self in pairs(E._active) do if not name or self.name == name then out[#out + 1] = self end end
    return out
end
function E.in_radius(x, y, z, r, name)
    local out = {}
    for _, id in ipairs(E._in_radius(x, y, z, r, name or "")) do
        local self = E._active[id]
        if self then out[#out + 1] = self end
    end
    return out
end
function E.spawn(name, x, y, z, data)
    local id = E._spawn(name, x, y, z, data)
    return id and E._active[id] or nil
end
function E.nearest_player(x, y, z, max)
    local best, bd
    for _, p in ipairs(E._players()) do
        local d = math.sqrt((p.x - x) ^ 2 + (p.y - y) ^ 2 + (p.z - z) ^ 2)
        if (not max or d <= max) and (not bd or d < bd) then best, bd = p, d end
    end
    if best then return best.name, best.x, best.y, best.z, bd end
    return nil
end
function E.get_players() return E._players() end
function E.line_of_sight(x1, y1, z1, x2, y2, z2) return E._line_of_sight(x1, y1, z1, x2, y2, z2) end
-- A walkable route for a kind of entity: { {x=,y=,z=}, ... } and whether it reaches the end
function E.find_path(name, x1, y1, z1, x2, y2, z2) return E._find_path(name, x1, y1, z1, x2, y2, z2) end
function E.damage_player(name, amount, reason) E._damage_player(name, amount or 1, reason or "") end
function E.drop_item(item, count, x, y, z) return E._drop_item(item, count or 1, x, y, z) end

-- Damage a hit does, by what's in hand (swords hit hardest, axes next). Mods may replace it.
local tiers = { wood = 0, wooden = 0, gold = 0, golden = 0, stone = 1, iron = 2, diamond = 3, netherite = 4 }
function E.get_attack_damage(item, player)
    item = item or ""
    local tier = 0
    for k, v in pairs(tiers) do if item:find(k, 1, true) then tier = math.max(tier, v) end end
    if item:find("sword", 1, true) then return 4 + tier end
    if item:find("axe", 1, true) and not item:find("pickaxe", 1, true) then return 3 + tier end
    if item:find("pickaxe", 1, true) then return 2 + tier end
    if item:find("shovel", 1, true) then return 1.5 + tier end
    return 1
end
)LUA";

class EntityLua {
public:
    void bind(sol::state& lua, EntityLuaHost host) {
        m_lua = &lua;
        m_host = std::move(host);
        sol::table E = lua["entities"].get_or_create<sol::table>();
        EntityManager* mgr = m_host.manager;
        EntityLua* self = this;

        E["_register"] = [self, mgr](const std::string& name, sol::table def) {
            if (name.empty() || name.rfind("__", 0) == 0) {
                std::cerr << "[Entities] register: '" << name << "' isn't a name you can use" << std::endl;
                return;
            }
            EntityType t;
            t.name = name;
            if (sol::optional<sol::table> hb = def["hitbox"]) {
                t.width = hb->get_or(1, t.width);
                t.height = hb->get_or(2, t.height);
            }
            t.width = std::clamp(t.width, 0.1f, 8.0f);
            t.height = std::clamp(t.height, 0.1f, 16.0f);
            t.maxHealth = std::max(1.0f, def.get_or("health", t.maxHealth));
            t.stepHeight = std::clamp(def.get_or("step_height", t.stepHeight), 0.0f, 2.0f);
            t.gravity = def.get_or("gravity", t.gravity);
            t.collides = def.get_or("collides", t.collides);
            t.autoJump = def.get_or("auto_jump", t.autoJump);
            t.pushable = def.get_or("knockback", t.pushable);
            t.solid = def.get_or("solid", t.solid);
            t.despawn = def.get_or("despawn", t.despawn);
            t.model = def.get_or("model", std::string());
            t.texture = def.get_or("texture", std::string());
            t.animations = def.get_or("animations", std::string());
            t.scale = def.get_or("scale", 1.0f);
            t.eyeHeight = def.get_or("eye_height", -1.0f);
            mgr->registerType(t);
            if (self->m_host.onRegister) self->m_host.onRegister(name, def);
        };

        // ---- the authority's functions (joined to a server they do nothing / return nil) ----
        E["_spawn"] = [self, mgr](const std::string& name, float x, float y, float z, sol::object data) -> sol::object {
            sol::state_view L(*self->m_lua);
            if (!self->authority() || !mgr->type(name)) {
                if (self->authority()) std::cerr << "[Entities] spawn: no entity called '" << name << "' (entities.register it first)" << std::endl;
                return sol::make_object(L, sol::lua_nil);
            }
            const uint32_t id = mgr->spawn(name, glm::vec3(x, y, z));
            if (!id) return sol::make_object(L, sol::lua_nil);
            self->activate(id, data);
            if (!mgr->get(id)) return sol::make_object(L, sol::lua_nil);   // on_spawn removed it
            return sol::make_object(L, id);
        };
        E["_remove"] = [mgr](uint32_t id) { mgr->remove(id); };
        E["_exists"] = [mgr](uint32_t id) { return mgr->get(id) != nullptr; };
        E["_pos"] = [self, mgr](uint32_t id) { return self->vec(mgr->get(id), 0); };
        E["_vel"] = [self, mgr](uint32_t id) { return self->vec(mgr->get(id), 1); };
        E["_set_pos"] = [mgr](uint32_t id, float x, float y, float z) {
            if (Entity* e = mgr->get(id); e && std::isfinite(x) && std::isfinite(y) && std::isfinite(z)) e->pos = {x, y, z};
        };
        E["_set_vel"] = [mgr](uint32_t id, float x, float y, float z) {
            if (Entity* e = mgr->get(id); e && std::isfinite(x) && std::isfinite(y) && std::isfinite(z)) {
                e->vel = glm::clamp(glm::vec3(x, y, z), glm::vec3(-60.0f), glm::vec3(60.0f));
                e->walking = false;
                mgr->stopPath(*e);
            }
        };
        E["_yaw"] = [mgr](uint32_t id) { const Entity* e = mgr->get(id); return e ? e->yaw : 0.0f; };
        E["_set_yaw"] = [mgr](uint32_t id, float deg) {
            if (Entity* e = mgr->get(id); e && std::isfinite(deg)) e->yaw = std::fmod(std::fmod(deg, 360.0f) + 360.0f, 360.0f);
        };
        E["_walk"] = [mgr](uint32_t id, float speed) {
            Entity* e = mgr->get(id);
            if (!e || !std::isfinite(speed)) return;
            speed = std::clamp(speed, -30.0f, 30.0f);
            mgr->stopPath(*e);                       // walking by hand: no path any more
            e->walking = speed != 0.0f;
            const float r = glm::radians(e->yaw);
            e->walk = glm::vec2(std::cos(r), std::sin(r)) * speed;
            if (!e->walking) e->walk = glm::vec2(0.0f);
        };
        E["_go_to"] = [self, mgr](uint32_t id, float x, float y, float z, float speed) {
            Entity* e = mgr->get(id);
            if (!e || !self->m_host.world || !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) return false;
            return mgr->goTo(*e, *self->m_host.world, glm::vec3(x, y, z), std::clamp(std::isfinite(speed) ? speed : 1.0f, 0.1f, 30.0f));
        };
        E["_path_state"] = [mgr](uint32_t id) { const Entity* e = mgr->get(id); return e ? e->pathState : std::string("none"); };
        E["_blocked"] = [mgr](uint32_t id) { const Entity* e = mgr->get(id); return e && e->blocked; };
        E["_eye"] = [mgr](uint32_t id) {
            const Entity* e = mgr->get(id);
            const EntityType* t = e ? mgr->type(e->type) : nullptr;
            if (!t) return 1.0f;
            return t->eyeHeight >= 0.0f ? t->eyeHeight : t->height * 0.85f;
        };
        E["_random_target"] = [self, mgr](uint32_t id, float radius) -> std::tuple<sol::object, sol::object, sol::object> {
            sol::state_view L(*self->m_lua);
            const Entity* e = mgr->get(id);
            glm::vec3 out;
            if (!e || !self->m_host.world || !mgr->randomDestination(*self->m_host.world, *e, std::clamp(radius, 2.0f, 32.0f), out)) {
                return { sol::make_object(L, sol::lua_nil), sol::make_object(L, sol::lua_nil), sol::make_object(L, sol::lua_nil) };
            }
            return { sol::make_object(L, out.x), sol::make_object(L, out.y), sol::make_object(L, out.z) };
        };
        E["_find_land"] = [self, mgr](uint32_t id, float radius) -> std::tuple<sol::object, sol::object, sol::object> {
            sol::state_view L(*self->m_lua);
            const Entity* e = mgr->get(id);
            glm::vec3 out;
            if (!e || !self->m_host.world || !mgr->findLand(*self->m_host.world, *e, std::clamp(radius, 2.0f, 32.0f), out)) {
                return { sol::make_object(L, sol::lua_nil), sol::make_object(L, sol::lua_nil), sol::make_object(L, sol::lua_nil) };
            }
            return { sol::make_object(L, out.x), sol::make_object(L, out.y), sol::make_object(L, out.z) };
        };
        E["_line_of_sight"] = [self](float x1, float y1, float z1, float x2, float y2, float z2) {
            if (!self->m_host.world) return true;
            return EntityManager::lineOfSight(*self->m_host.world, glm::vec3(x1, y1, z1), glm::vec3(x2, y2, z2));
        };
        E["_find_path"] = [self, mgr](const std::string& name, float x1, float y1, float z1, float x2, float y2, float z2) {
            sol::state_view L(*self->m_lua);
            sol::table out = L.create_table();
            const EntityType* t = mgr->type(name);
            if (!t || !self->m_host.world) return std::make_tuple(out, false);
            bool reached = false;
            int i = 1;
            for (const auto& p : mgr->findPath(*self->m_host.world, *t, glm::vec3(x1, y1, z1), glm::vec3(x2, y2, z2), &reached)) {
                out[i++] = L.create_table_with("x", p.x, "y", p.y, "z", p.z);
            }
            return std::make_tuple(out, reached);
        };
        E["_flags"] = [mgr](uint32_t id) {
            const Entity* e = mgr->get(id);
            return std::make_tuple(e && e->onGround, e && e->inWater);
        };
        E["_anim"] = [mgr](uint32_t id, const std::string& name, float speed, bool loop) {
            Entity* e = mgr->get(id);
            if (!e) return;
            if (e->anim == name && e->animSpeed == speed && e->animLoop == loop) return;
            e->anim = name; e->animSpeed = std::isfinite(speed) ? speed : 1.0f; e->animLoop = loop;
            e->touch();
        };
        E["_get_anim"] = [mgr](uint32_t id) { const Entity* e = mgr->get(id); return e ? e->anim : std::string(); };
        E["_nametag"] = [mgr](uint32_t id, const std::string& text) {
            Entity* e = mgr->get(id);
            if (!e || e->nametag == text) return;
            e->nametag = text.substr(0, 64);
            e->touch();
        };
        E["_health"] = [mgr](uint32_t id) { const Entity* e = mgr->get(id); return e ? e->health : 0.0f; };
        E["_max_health"] = [mgr](uint32_t id) {
            const Entity* e = mgr->get(id);
            const EntityType* t = e ? mgr->type(e->type) : nullptr;
            return t ? t->maxHealth : 0.0f;
        };
        E["_set_health"] = [self, mgr](uint32_t id, float h) {
            Entity* e = mgr->get(id);
            if (!e || !std::isfinite(h)) return;
            e->health = h;
            e->touch();
            if (h <= 0.0f) self->kill(id, "");
        };
        E["_damage"] = [self, mgr](uint32_t id, float amount, sol::optional<std::string> attacker) {
            Entity* e = mgr->get(id);
            if (!e || !std::isfinite(amount)) return;
            glm::vec3 from;
            const bool hasFrom = attacker && self->playerPos(*attacker, from);
            if (mgr->damage(*e, amount, hasFrom ? &from : nullptr)) self->kill(id, attacker.value_or(""));
        };
        E["_in_radius"] = [mgr](float x, float y, float z, float r, const std::string& name) {
            std::vector<uint32_t> out;
            const glm::vec3 c(x, y, z);
            for (const auto& [id, e] : mgr->all()) {
                if (e.removed || (!name.empty() && e.type != name)) continue;
                if (glm::distance(e.pos, c) <= r) out.push_back(id);
            }
            return sol::as_table(out);
        };
        E["_players"] = [self]() {
            sol::state_view L(*self->m_lua);
            sol::table out = L.create_table();
            int i = 1;
            if (self->m_host.players) {
                for (const auto& [name, p] : self->m_host.players()) {
                    out[i++] = L.create_table_with("name", name, "x", p.x, "y", p.y, "z", p.z);
                }
            }
            return out;
        };
        E["_damage_player"] = [self](const std::string& name, float amount, const std::string& reason) {
            if (self->authority() && self->m_host.damagePlayer && std::isfinite(amount) && amount > 0.0f) {
                self->m_host.damagePlayer(name, amount, reason);
            }
        };
        E["_drop_item"] = [self, mgr](const std::string& item, int count, float x, float y, float z) -> uint32_t {
            if (!self->authority()) return 0;
            return self->drop(item, count, glm::vec3(x, y, z));
        };

        // entities.register_spawn{ name, on = {blocks}, min_light, max_light, min_y, max_y,
        //   chance, group = n or {min, max}, max_nearby, when = "day"|"night"|"any", sky = true }
        E["register_spawn"] = [mgr](sol::table def) {
            SpawnRule r;
            r.type = def.get_or("name", std::string());
            if (r.type.empty()) { std::cerr << "[Entities] register_spawn: needs name = \"mymod:mob\"" << std::endl; return; }
            if (sol::optional<sol::table> on = def["on"]) {
                for (const auto& kv : *on) if (kv.second.is<std::string>()) r.onBlocks.push_back(kv.second.as<std::string>());
            }
            r.minLight = std::clamp(def.get_or("min_light", r.minLight), 0, 15);
            r.maxLight = std::clamp(def.get_or("max_light", r.maxLight), 0, 15);
            r.minY = def.get_or("min_y", r.minY);
            r.maxY = def.get_or("max_y", r.maxY);
            r.chance = std::clamp(def.get_or("chance", r.chance), 0.0f, 1.0f);
            sol::object group = def["group"];
            if (group.is<int>()) r.groupMin = r.groupMax = group.as<int>();
            else if (group.is<sol::table>()) {
                r.groupMin = group.as<sol::table>().get_or(1, 1);
                r.groupMax = group.as<sol::table>().get_or(2, r.groupMin);
            }
            r.groupMin = std::clamp(r.groupMin, 1, 16);
            r.groupMax = std::clamp(r.groupMax, r.groupMin, 16);
            r.maxNearby = std::clamp(def.get_or("max_nearby", r.maxNearby), 1, 100);
            r.when = def.get_or("when", r.when);
            r.needsSky = def.get_or("sky", r.needsSky);
            mgr->addSpawnRule(r);
        };

        sol::protected_function_result r = lua.safe_script(kEntityLuaPrelude, sol::script_pass_on_error, "=entities");
        if (!r.valid()) {
            sol::error err = r;
            std::cerr << "[Entities] " << err.what() << std::endl;
        }
    }

    bool authority() const { return m_host.isAuthority && m_host.isAuthority(); }
    // The world they live in (the game's world can change; set it before step)
    void setWorld(const World* world) { m_host.world = world; }

    // ---- The authority, every tick ----
    void step(float dt) {
        if (!m_lua || !m_host.manager || !m_host.world || !authority()) return;
        EntityManager& mgr = *m_host.manager;
        std::vector<std::pair<std::string, glm::vec3>> players;
        if (m_host.players) players = m_host.players();
        std::vector<glm::vec3> positions;
        for (const auto& p : players) positions.push_back(p.second);

        mgr.step(dt, *m_host.world, positions);
        forgetRemoved();

        // Each mob's own on_step (not the ones frozen far away)
        std::vector<uint32_t> ids;
        ids.reserve(mgr.all().size());
        for (const auto& [id, e] : mgr.all()) {
            if (e.removed) continue;
            float nearest = 1e9f;
            for (const auto& p : positions) nearest = std::min(nearest, glm::distance(p, e.pos));
            if (nearest <= EntityManager::kActiveRange && m_host.world->isLoaded(glm::ivec3(glm::floor(e.pos)))) ids.push_back(id);
        }
        for (uint32_t id : ids) {
            if (!mgr.get(id)) continue;
            callDef(id, "on_step", dt);
        }

        // Dropped items: picked up by a player standing on them
        for (const auto& [itemId, who] : mgr.itemsInReach(positions)) {
            Entity* e = mgr.get(itemId);
            if (!e || who >= players.size()) continue;
            if (!m_host.giveItem || m_host.giveItem(players[who].first, e->item, e->itemCount)) mgr.remove(itemId);
            else e->pickupDelay = 1.0f;              // no room: try again in a moment
        }

        // Natural spawning
        const bool day = m_host.isDay ? m_host.isDay() : true;
        auto light = m_host.lightLevel ? m_host.lightLevel : std::function<int(int, int, int)>([](int, int, int) { return 15; });
        for (uint32_t id : mgr.trySpawning(dt, *m_host.world, positions, light, day,
                                           m_host.spawningEnabled ? m_host.spawningEnabled() : true,
                                           m_host.skyVisible)) {
            activate(id, sol::lua_nil);
        }
        forgetRemoved();
    }

    // A player hit it (left click). `from` = where they are (for knockback)
    void punch(uint32_t id, const std::string& player, const glm::vec3& from, const std::string& item) {
        if (!authority()) return;
        EntityManager& mgr = *m_host.manager;
        Entity* e = mgr.get(id);
        if (!e) return;
        if (e->type == EntityManager::kItemType) return;
        if (e->hurtTimer > 0.0f) return;                 // just hit: half a second before the next
        float damage = 1.0f;
        {
            sol::protected_function f = (*m_lua)["entities"]["get_attack_damage"];
            if (f.valid()) {
                luasandbox::TimeLimit limit(m_host.timeLimit, "entities.get_attack_damage");
                auto r = f(item, player);
                if (r.valid() && r.get_type() == sol::type::number) damage = r.get<float>();
                else if (!r.valid()) {
                    sol::error err = r;      // (not sol::error(r): MSVC finds that ambiguous)
                    report("get_attack_damage", err.what());
                }
            }
        }
        bool handled = false;
        callDef(id, "on_punch", [&](sol::protected_function_result& r) {
            handled = r.get_type() == sol::type::boolean && r.get<bool>();
        }, player, item, damage);
        e = mgr.get(id);
        if (handled || !e) return;
        if (mgr.damage(*e, damage, &from)) kill(id, player);
    }

    // A player right-clicked it
    void rightClick(uint32_t id, const std::string& player, const std::string& item) {
        if (!authority() || !m_host.manager->get(id)) return;
        callDef(id, "on_right_click", [](sol::protected_function_result&) {}, player, item);
    }

    // Dies: on_death(self, killer) (return true to keep it alive after all), its drops fall
    void kill(uint32_t id, const std::string& killer) {
        EntityManager& mgr = *m_host.manager;
        Entity* e = mgr.get(id);
        if (!e) return;
        bool keep = false;
        callDef(id, "on_death", [&](sol::protected_function_result& r) {
            keep = r.get_type() == sol::type::boolean && r.get<bool>();
        }, killer);
        e = mgr.get(id);
        if (!e) return;
        if (keep) { if (e->health <= 0.0f) e->health = 1.0f; return; }
        // drops = { {"beef", 1, 3}, {"leather", 0, 2}, ... }  (item, fewest, most)
        sol::object defObj = (*m_lua)["entities"]["_defs"][e->type];
        const glm::vec3 at = e->pos + glm::vec3(0.0f, 0.3f, 0.0f);
        if (defObj.is<sol::table>()) {
            sol::object dropsObj = defObj.as<sol::table>()["drops"];
            if (dropsObj.is<sol::table>()) {
                for (const auto& kv : dropsObj.as<sol::table>()) {
                    if (!kv.second.is<sol::table>()) continue;
                    sol::table d = kv.second.as<sol::table>();
                    const std::string item = d.get_or(1, std::string());
                    const int lo = d.get_or(2, 1), hi = std::max(lo, d.get_or(3, lo));
                    const int n = lo + (hi > lo ? static_cast<int>(rand01() * (hi - lo + 1)) % (hi - lo + 1) : 0);
                    if (!item.empty() && n > 0) drop(item, n, at);
                }
            }
        }
        mgr.remove(id);
    }

    // A dropped item, popping out in a random direction
    uint32_t drop(const std::string& item, int count, const glm::vec3& at) {
        if (item.empty() || count <= 0) return 0;
        const float a = rand01() * 6.2831853f;
        return m_host.manager->dropItem(item, std::min(count, 64 * 64), at,
                                        glm::vec3(std::cos(a) * 1.5f, 4.0f, std::sin(a) * 1.5f));
    }

    // After loading a world: every saved mob gets its self table and on_spawn(self, saved)
    void activateAll() {
        if (!m_lua) return;
        std::vector<uint32_t> ids;
        for (const auto& [id, e] : m_host.manager->all()) ids.push_back(id);
        for (uint32_t id : ids) {
            const Entity* e = m_host.manager->get(id);
            if (!e) continue;
            sol::state_view L(*m_lua);
            activate(id, e->saved.empty() ? sol::object(sol::lua_nil) : sol::make_object(L, e->saved));
        }
    }

    // Saving: on_save(self) -> text kept with the world
    void saveData(Entity& e) {
        callDef(e.id, "on_save", [&](sol::protected_function_result& r) {
            if (r.get_type() == sol::type::string) e.saved = r.get<std::string>();
            else if (r.get_type() == sol::type::lua_nil || r.get_type() == sol::type::none) e.saved.clear();
        });
    }

    // A world closed: forget every self table
    void reset() {
        if (!m_lua) return;
        sol::object active = (*m_lua)["entities"]["_active"];
        if (active.is<sol::table>()) (*m_lua)["entities"]["_active"] = m_lua->create_table();
    }

private:
    std::tuple<sol::object, sol::object, sol::object> vec(const Entity* e, int which) {
        sol::state_view L(*m_lua);
        if (!e) return { sol::make_object(L, sol::lua_nil), sol::make_object(L, sol::lua_nil), sol::make_object(L, sol::lua_nil) };
        const glm::vec3 v = which == 0 ? e->pos : e->vel;
        return { sol::make_object(L, v.x), sol::make_object(L, v.y), sol::make_object(L, v.z) };
    }

    bool playerPos(const std::string& name, glm::vec3& out) const {
        if (!m_host.players) return false;
        for (const auto& [n, p] : m_host.players()) if (n == name) { out = p; return true; }
        return false;
    }

    // Makes its self table and runs on_spawn(self, data)
    void activate(uint32_t id, sol::object data) {
        const Entity* e = m_host.manager->get(id);
        if (!e) return;
        sol::state_view L(*m_lua);
        sol::table E = L["entities"];
        sol::table selfT = L.create_table();
        selfT["id"] = id;
        selfT["name"] = e->type;
        selfT[sol::metatable_key] = E["_methods"];
        E["_active"][id] = selfT;
        callDef(id, "on_spawn", [](sol::protected_function_result&) {}, data);
    }

    void forgetRemoved() {
        sol::table active = (*m_lua)["entities"]["_active"];
        for (uint32_t id : m_host.manager->takeRemoved()) active[id] = sol::lua_nil;
    }

    template <typename... Args>
    void callDef(uint32_t id, const char* fn, Args&&... args) {
        callDef(id, fn, [](sol::protected_function_result&) {}, std::forward<Args>(args)...);
    }
    template <typename Use, typename... Args>
    auto callDef(uint32_t id, const char* fn, Use&& use, Args&&... args)
        -> decltype(use(std::declval<sol::protected_function_result&>()), void()) {
        const Entity* e = m_host.manager->get(id);
        if (!e) return;
        sol::state_view L(*m_lua);
        sol::object defObj = L["entities"]["_defs"][e->type];
        if (!defObj.is<sol::table>()) return;
        sol::object fnObj = defObj.as<sol::table>()[fn];
        if (fnObj.get_type() != sol::type::function) return;
        sol::object selfObj = L["entities"]["_active"][id];
        if (!selfObj.is<sol::table>()) return;
        const std::string type = e->type;
        sol::protected_function f = fnObj.as<sol::protected_function>();
        luasandbox::TimeLimit limit(m_host.timeLimit, fn);
        sol::protected_function_result r = f(selfObj, std::forward<Args>(args)...);
        if (!r.valid()) {
            sol::error err = r;
            report(type + "." + fn, err.what());
            return;
        }
        use(r);
    }

    void report(const std::string& where, const std::string& message) {
        std::string& last = m_lastError[where];
        if (last == message) return;
        last = message;
        std::cerr << "[Lua] " << where << ": " << message << std::endl;
    }

    float rand01() {
        m_rng ^= m_rng << 13; m_rng ^= m_rng >> 17; m_rng ^= m_rng << 5;
        return static_cast<float>(m_rng & 0xFFFFFF) / static_cast<float>(0x1000000);
    }

    sol::state* m_lua = nullptr;
    EntityLuaHost m_host;
    std::map<std::string, std::string> m_lastError;
    uint32_t m_rng = 0x2545F491u;
};
