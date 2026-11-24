#pragma once
// Server-side mod events and functions, the same on the dedicated server (ServerMain.cpp) and
// in the game (main.cpp: singleplayer and hosting a LAN game).
//
// Events (globals a mod may define; chain them like any hook):
//   on_player_join(name, info)          info = { operator, mode, new_account }
//   on_player_leave(name, x, y, z)
//   on_player_chat(name, text) -> false to drop the line, or a string to replace it
//   can_edit_block(name, x, y, z, old_block, new_block, old_meta, new_meta) -> false[, reason] to
//       refuse (asked when the block or its metadata changes)
//   on_block_broken(x, y, z, block, meta, player) / on_block_placed(x, y, z, block, player)
//   on_player_harvest(name, item, x, y, z)   a joined player used `item` (the scythe) on that
//       block: the server works out what it does, from its own world (engine.player_set_block)
//   on_server_start() / on_server_stop()
//   on_mod_message(name, channel, data)   a player's game sent this (engine.send_to_server):
//       text on a named channel, checked for size and flooding. Data only - never run it.
// Each call is time-limited (luasandbox::TimeLimit) and errors are printed once until they change.
//
// Functions (engine.*): get_online_players, get_player_info, message_player, kick_player,
// teleport_player, set_player_mode, get_player_mode, is_player_operator, get_server_info,
// player_set_block (a change made for a joined player, checked like their own edits),
// send_to_player / send_to_all (mod messages to players' games: on_server_message there).
// Kicks from mods wait until flush() (after the network update), so a mod can never remove a
// player while the server is still handling that player's packet.

#include "LuaSandbox.h"
#include "Net.h"
#include "Dimensions.h"
#include "Block.h"
#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <cstdio>
#include <functional>
#include <iostream>
#include <map>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

class ServerLuaApi {
public:
    struct PlayerInfo {
        std::string name;
        std::string dimension;        // "" = the overworld
        glm::vec3 pos{0.0f};
        std::string mode;
        bool op = false;
    };

    sol::state* lua = nullptr;
    GameServer* server = nullptr;
    double timeLimit = 5.0;   // seconds per event call (<= 0: none)
    bool dedicated = false;

    // Who is an operator (dedicated: ops.txt). Required.
    std::function<bool(const std::string&)> isOperator;
    // The local player (singleplayer / LAN host); not set on the dedicated server
    std::function<bool(PlayerInfo&)> localPlayer;
    std::function<void(const std::string&, const glm::vec3&)> localMessage;
    std::function<void(const glm::vec3&)> localTeleport;
    // Dimensions: send a player to a dimension (the owner moves the local player, or asks the
    // server); make sure a dimension's world is loaded (dimensions.run_in)
    std::function<bool(const std::string& name, const std::string& dimension, const glm::vec3& pos)> changeDimension;
    std::function<bool(const std::string& dimension)> loadDimension;
    // Loads (or generates) the chunks within `radius` blocks of x, z in a dimension, now
    std::function<bool(const std::string& dimension, int x, int z, int radius)> loadArea;
    // A mod message for the local player's own game (its on_server_message). Returns whether
    // it was accepted (false: its queue is full, and the message is dropped).
    std::function<bool(const std::string& channel, const std::string& data)> localModMessage;
    // Dedicated server: remember a mode set by a mod (gamemodes.txt)
    std::function<void(const std::string&, const std::string&)> rememberMode;
    // engine.get_server_info() fields
    std::function<void(sol::table&)> fillServerInfo;

    // ---- Events ----
    void playerJoined(const std::string& name, bool newAccount = false) {
        call("on_player_join", [](sol::protected_function_result&) {}, name, infoTable(name, newAccount));
    }
    void playerLeft(const std::string& name, const glm::vec3& pos) {
        call("on_player_leave", [](sol::protected_function_result&) {}, name, pos.x, pos.y, pos.z);
    }
    // false = drop the line; text may be replaced
    bool chat(const std::string& name, std::string& text) {
        bool keep = true;
        call("on_player_chat", [&](sol::protected_function_result& r) {
            if (r.return_count() < 1) return;
            const sol::object v = r.get<sol::object>(0);
            if (v.get_type() == sol::type::boolean && !v.as<bool>()) keep = false;
            else if (v.get_type() == sol::type::string) text = v.as<std::string>();
        }, name, text);
        return keep;
    }
    // "" = allowed; a reason (or GameServer::kSilentRefusal) = refused
    std::string canEdit(const std::string& name, const glm::ivec3& pos, uint8_t oldId, uint8_t oldMeta,
                        uint8_t newId, uint8_t newMeta) {
        std::string refusal;
        auto& reg = BlockRegistry::getInstance();
        call("can_edit_block", [&](sol::protected_function_result& r) {
            if (r.return_count() < 1) return;
            const sol::object v = r.get<sol::object>(0);
            if (v.get_type() != sol::type::boolean || v.as<bool>()) return;   // nil / true: allowed
            refusal = GameServer::kSilentRefusal;
            if (r.return_count() >= 2 && r.get<sol::object>(1).get_type() == sol::type::string) {
                refusal = r.get<std::string>(1);
                if (refusal.empty()) refusal = GameServer::kSilentRefusal;
            }
        }, name, pos.x, pos.y, pos.z, blockName(reg, oldId), blockName(reg, newId),
           static_cast<int>(oldMeta), static_cast<int>(newMeta));
        return refusal;
    }
    // A change was made by `name` ("" = not a player): on_block_broken / on_block_placed
    void edited(const std::string& name, const glm::ivec3& pos, uint8_t oldId, uint8_t newId, uint8_t meta = 0) {
        auto& reg = BlockRegistry::getInstance();
        sol::object who = name.empty() ? sol::make_object(*lua, sol::lua_nil) : sol::make_object(*lua, name);
        if (newId == 0) call("on_block_broken", [](sol::protected_function_result&) {}, pos.x, pos.y, pos.z,
                             blockName(reg, oldId), static_cast<int>(meta), who);
        else call("on_block_placed", [](sol::protected_function_result&) {}, pos.x, pos.y, pos.z,
                  blockName(reg, newId), who);
    }
    // A joined player used `item` on the block at pos (C_HARVEST): on_player_harvest
    void harvest(const std::string& name, const std::string& item, const glm::ivec3& pos) {
        call("on_player_harvest", [](sol::protected_function_result&) {}, name, item, pos.x, pos.y, pos.z);
    }
    // A player wrote on a block (a sign): can_edit_text(name, x, y, z, side, lines) -> false[,
    // reason] refuses, a table of lines replaces theirs, nothing allows it
    bool canEditText(const std::string& name, const glm::ivec3& pos, int side, std::vector<std::string>& lines,
                     std::string& refusal) {
        bool ok = true;
        sol::table t = lua->create_table();
        for (size_t i = 0; i < lines.size(); ++i) t[i + 1] = lines[i];
        call("can_edit_text", [&](sol::protected_function_result& r) {
            if (r.return_count() < 1) return;
            const sol::object v = r.get<sol::object>(0);
            if (v.get_type() == sol::type::boolean && !v.as<bool>()) {
                ok = false;
                if (r.return_count() >= 2 && r.get<sol::object>(1).get_type() == sol::type::string) refusal = r.get<std::string>(1);
            } else if (v.get_type() == sol::type::table) {
                sol::table nt = v.as<sol::table>();
                lines.clear();
                for (size_t i = 1; i <= BlockText::kMaxLines && i <= nt.size(); ++i) {
                    sol::object l = nt[i];
                    lines.push_back(l.get_type() == sol::type::string ? l.as<std::string>().substr(0, BlockText::kMaxLineBytes) : std::string());
                }
            }
        }, name, pos.x, pos.y, pos.z, std::string(side == 1 ? "back" : "front"), t);
        return ok;
    }
    // A player is now in another dimension: on_player_change_dimension(name, from, to)
    void dimensionChanged(const std::string& name, const std::string& from, const std::string& to) {
        dims::InDimension here(to);
        call("on_player_change_dimension", [](sol::protected_function_result&) {}, name,
             from.empty() ? std::string(dims::kOverworld) : from, to.empty() ? std::string(dims::kOverworld) : to);
    }
    // A player's game sent its server's mods a message (engine.send_to_server)
    void modMessage(const std::string& name, const std::string& channel, const std::string& data) {
        call("on_mod_message", [](sol::protected_function_result&) {}, name, channel, data);
    }
    void serverStarted() { call("on_server_start", [](sol::protected_function_result&) {}); }
    void serverStopping() { call("on_server_stop", [](sol::protected_function_result&) {}); }

    // Kicks asked for by mods: done here, outside the network code
    void flush() {
        if (!server) { m_pendingKicks.clear(); return; }
        auto kicks = std::move(m_pendingKicks);
        m_pendingKicks.clear();
        for (const auto& [name, reason] : kicks) server->kickPlayer(name, reason);
    }

    // Runs a global Lua function with the time limit; `use` sees its results. False if it
    // isn't defined or failed (the error is printed once until it changes).
    template <typename Use, typename... Args>
    bool call(const char* hook, Use&& use, Args&&... args) {
        if (!lua) return false;
        sol::protected_function fn = (*lua)[hook];
        if (!fn.valid()) return false;
        luasandbox::TimeLimit limit(timeLimit, hook);
        sol::protected_function_result r = fn(std::forward<Args>(args)...);
        if (!r.valid()) {
            sol::error err = r;
            report(hook, err.what());
            return false;
        }
        use(r);
        return true;
    }
    // A Lua error, printed once until the message for that hook changes
    void report(const std::string& hook, const std::string& message) {
        std::string& last = m_lastError[hook];
        if (last == message) return;
        last = message;
        std::cerr << "[Lua] " << hook << ": " << message << std::endl;
    }

    // ---- engine.* functions ----
    void addFunctions(sol::table engine) {
        engine["get_online_players"] = [this](sol::this_state ts) {
            sol::state_view lv(ts);
            sol::table out = lv.create_table();
            int i = 1;
            for (const auto& p : players()) out[i++] = toTable(lv, p);
            return out;
        };
        engine["get_player_info"] = [this](const std::string& name, sol::this_state ts) -> sol::object {
            sol::state_view lv(ts);
            PlayerInfo p;
            if (!findPlayer(name, p)) return sol::make_object(lv, sol::lua_nil);
            return sol::make_object(lv, toTable(lv, p));
        };
        engine["message_player"] = [this](const std::string& name, const std::string& text, sol::optional<float> r,
                                          sol::optional<float> g, sol::optional<float> b) {
            const glm::vec3 color = colorOf(r, g, b);
            PlayerInfo local;
            if (localPlayer && localPlayer(local) && local.name == name) {
                if (localMessage) localMessage(text, color);
                return true;
            }
            return server && server->running() && server->messagePlayer(name, text, color);
        };
        // engine.send_to_player(name, channel, data) -> true if they're online: a message for that
        // player's game (its on_server_message(channel, data)). data is text; numbers are turned
        // into text. channel: letters, digits and _ : . - , your mod's name first ("hud").
        engine["send_to_player"] = [this](const std::string& name, const std::string& channel, sol::object data) {
            std::string text;
            if (!modText(data, text) || !net::validModChannel(channel) || text.size() > net::kMaxModMessage) return false;
            PlayerInfo local;
            if (localPlayer && localPlayer(local) && local.name == name) {
                return localModMessage && localModMessage(channel, text);
            }
            return server && server->running() && server->sendModMessage(name, channel, text);
        };
        // engine.send_to_all(channel, data) -> how many players got it (everyone online, the host too)
        engine["send_to_all"] = [this](const std::string& channel, sol::object data) {
            std::string text;
            if (!modText(data, text) || !net::validModChannel(channel) || text.size() > net::kMaxModMessage) return 0;
            int n = 0;
            PlayerInfo local;
            if (localPlayer && localPlayer(local) && localModMessage && localModMessage(channel, text)) ++n;
            if (server && server->running()) n += server->sendModMessageToAll(channel, text);
            return n;
        };
        // engine.get_player_dimension(name) -> "overworld" | "mod:name", or nil if they aren't online
        engine["get_player_dimension"] = [this](const std::string& name, sol::this_state ts) -> sol::object {
            sol::state_view lv(ts);
            PlayerInfo p;
            if (!findPlayer(name, p)) return sol::make_object(lv, sol::lua_nil);
            return sol::make_object(lv, p.dimension.empty() ? std::string(dims::kOverworld) : p.dimension);
        };
        // engine.set_player_dimension(name, dimension [, x, y, z]) -> true: they're now in that
        // dimension at x, y, z (its world is loaded first; no position: its `spawn`).
        // on_player_change_dimension runs.
        engine["set_player_dimension"] = [this](const std::string& name, const std::string& dimension,
                                                sol::optional<float> x, sol::optional<float> y, sol::optional<float> z) {
            if (!dims::Registry::get().has(dimension) || !changeDimension) return false;
            glm::vec3 at = dims::Registry::get().type(dimension).spawn;
            if (x && y && z) at = glm::vec3(*x, *y, *z);
            if (!std::isfinite(at.x) || !std::isfinite(at.y) || !std::isfinite(at.z)) return false;
            return changeDimension(name, dimension, at);
        };
        {
            sol::table dimensions = (*lua)["dimensions"].get_or_create<sol::table>();
            // dimensions.current() -> the dimension this code runs in ("overworld" or "mod:name"):
            // where the player, block or entity that started it is
            dimensions["current"] = []() {
                return dims::current().empty() ? std::string(dims::kOverworld) : dims::current();
            };
            // dimensions.load_area(dimension, x, z [, radius]) -> true: the chunks within radius
            // blocks (default 16, at most 64) of x, z are there now - loaded from disk or generated
            // - so blocks there can be read and changed straight away (building a portal there)
            dimensions["load_area"] = [this](const std::string& dimension, float x, float z, sol::optional<int> radius) {
                if (!dims::Registry::get().has(dimension) || !loadArea) return false;
                if (!std::isfinite(x) || !std::isfinite(z)) return false;
                return loadArea(dimension, static_cast<int>(std::floor(x)), static_cast<int>(std::floor(z)),
                                std::clamp(radius.value_or(16), 0, 64));
            };
            // dimensions.run_in(dimension, fn, ...) -> fn's first result: runs fn with world
            // functions (engine.set_block, get_block...) acting in that dimension (loaded first)
            dimensions["run_in"] = [this](const std::string& dimension, sol::protected_function fn,
                                          sol::variadic_args args, sol::this_state ts) -> sol::object {
                sol::state_view lv(ts);
                if (!dims::Registry::get().has(dimension)) throw std::runtime_error("dimensions.run_in: no dimension '" + dimension + "'");
                if (loadDimension && !loadDimension(dimension)) return sol::make_object(lv, sol::lua_nil);
                dims::InDimension here(dimension);
                auto r = fn(args);
                if (!r.valid()) { sol::error err = r; throw std::runtime_error(err.what()); }
                if (r.return_count() < 1) return sol::make_object(lv, sol::lua_nil);
                return r.get<sol::object>(0);
            };
        }
        engine["kick_player"] = [this](const std::string& name, sol::optional<std::string> reason) {
            if (!server || !server->running()) return false;
            const auto names = server->playerNames();
            if (std::find(names.begin(), names.end(), name) == names.end()) return false;
            m_pendingKicks.emplace_back(name, reason.value_or("Kicked by the server"));
            return true;
        };
        engine["teleport_player"] = [this](const std::string& name, float x, float y, float z) {
            const glm::vec3 to(x, y, z);
            PlayerInfo local;
            if (localPlayer && localPlayer(local) && local.name == name) {
                if (localTeleport) localTeleport(to);
                return localTeleport != nullptr;
            }
            return server && server->running() && server->teleportPlayer(name, to);
        };
        engine["set_player_mode"] = [this](const std::string& name, const std::string& modeIn) {
            const std::string mode = modeIn == "creative" ? "creative" : modeIn == "survival" ? "survival" : "";
            if (mode.empty() || !server || !server->running()) return false;
            const bool online = server->setPlayerMode(name, mode);
            if (online && rememberMode) rememberMode(name, mode);
            return online;
        };
        engine["get_player_mode"] = [this](const std::string& name, sol::this_state ts) -> sol::object {
            sol::state_view lv(ts);
            PlayerInfo p;
            if (!findPlayer(name, p) || p.mode.empty()) return sol::make_object(lv, sol::lua_nil);
            return sol::make_object(lv, p.mode);
        };
        engine["is_player_operator"] = [this](const std::string& name) {
            PlayerInfo p;
            return findPlayer(name, p) ? p.op : (isOperator && isOperator(name));
        };
        // engine.player_set_block(name, x, y, z, block [, meta]) -> true, or false[, reason to show them]:
        // changes a block for a joined player with exactly the checks their own edits get (within
        // reach - with a block of slack, for a 3x3 patch -, spawn protection, can_edit_block).
        // For server-side actions that must only reward what was really allowed: make the change
        // first, then give what it yields only when this returns true. It's false (no reason) when
        // the block and metadata are already exactly that: nothing changed, so nothing was earned.
        engine["player_set_block"] = [this](const std::string& name, int x, int y, int z, const std::string& block,
                                            sol::optional<int> meta, sol::this_state ts) {
            sol::state_view lv(ts);
            std::string why;
            const uint8_t m = static_cast<uint8_t>(std::clamp(meta.value_or(0), 0, 255));
            const bool ok = server && server->running() &&
                            server->playerSetBlock(name, glm::ivec3(x, y, z), block, &why, 2.0f, m);
            return std::make_tuple(ok, why.empty() ? sol::make_object(lv, sol::lua_nil) : sol::make_object(lv, why));
        };
        engine["get_server_info"] = [this](sol::this_state ts) {
            sol::state_view lv(ts);
            sol::table t = lv.create_table();
            t["dedicated"] = dedicated;
            t["running"] = server && server->running();
            t["online"] = static_cast<int>(players().size());
            if (server) { t["game"] = server->game(); t["max_players"] = server->maxPlayers(); }
            if (fillServerInfo) fillServerInfo(t);
            return t;
        };
    }

private:
    std::vector<std::pair<std::string, std::string>> m_pendingKicks;
    std::map<std::string, std::string> m_lastError;

    static bool modText(const sol::object& o, std::string& out) {
        if (o.get_type() == sol::type::string) { out = o.as<std::string>(); return true; }
        if (o.get_type() == sol::type::number) {
            char buf[40];
            std::snprintf(buf, sizeof(buf), "%.14g", o.as<double>());
            out = buf;
            return true;
        }
        if (o.get_type() == sol::type::lua_nil || o.get_type() == sol::type::none) { out.clear(); return true; }
        return false;   // (tables, functions...: send text, e.g. made with a serializer)
    }
    static std::string blockName(BlockRegistry& reg, uint8_t id) {
        return id == 0 ? std::string("air") : reg.getBlock(id).name;
    }
    static glm::vec3 colorOf(const sol::optional<float>& r, const sol::optional<float>& g, const sol::optional<float>& b) {
        glm::vec3 c(r.value_or(1.0f), g.value_or(1.0f), b.value_or(0.6f));
        if (c.r > 1.0f || c.g > 1.0f || c.b > 1.0f) c /= 255.0f;   // 0..255 works too
        return glm::clamp(c, glm::vec3(0.0f), glm::vec3(1.0f));
    }

    std::vector<PlayerInfo> players() const {
        std::vector<PlayerInfo> out;
        PlayerInfo local;
        if (localPlayer && localPlayer(local)) out.push_back(local);
        if (server && server->running()) {
            for (const auto& place : server->playerPlaces()) {
                const std::string& name = place.name;
                PlayerInfo p;
                p.name = name;
                p.pos = place.pos;
                p.dimension = place.dimension;
                p.mode = server->playerMode(name);
                if (p.mode.empty()) p.mode = server->defaultMode();
                p.op = isOperator && isOperator(name);
                out.push_back(p);
            }
        }
        return out;
    }
    bool findPlayer(const std::string& name, PlayerInfo& out) const {
        for (const auto& p : players()) if (p.name == name) { out = p; return true; }
        return false;
    }
    sol::table toTable(sol::state_view& lv, const PlayerInfo& p) const {
        sol::table t = lv.create_table();
        t["name"] = p.name;
        t["x"] = p.pos.x; t["y"] = p.pos.y; t["z"] = p.pos.z;
        t["dimension"] = p.dimension.empty() ? std::string(dims::kOverworld) : p.dimension;
        t["mode"] = p.mode;
        t["operator"] = p.op;
        return t;
    }
    sol::table infoTable(const std::string& name, bool newAccount) {
        sol::table t = lua->create_table();
        PlayerInfo p;
        if (findPlayer(name, p)) { t["operator"] = p.op; t["mode"] = p.mode; }
        else t["operator"] = isOperator && isOperator(name);
        t["new_account"] = newAccount;
        return t;
    }
};
