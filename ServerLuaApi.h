#pragma once
// Server-side mod events and functions, the same on the dedicated server (ServerMain.cpp) and
// in the game (main.cpp: singleplayer and hosting a LAN game).
//
// Events (globals a mod may define; chain them like any hook):
//   on_player_join(name, info)          info = { operator, mode, new_account }
//   on_player_leave(name, x, y, z)
//   on_player_chat(name, text) -> false to drop the line, or a string to replace it
//   can_edit_block(name, x, y, z, old_block, new_block) -> false[, reason] to refuse
//   on_block_broken(x, y, z, block, meta, player) / on_block_placed(x, y, z, block, player)
//   on_player_harvest(name, item, x, y, z)   a joined player used `item` (the scythe) on that
//       block: the server works out what it does, from its own world (engine.player_set_block)
//   on_server_start() / on_server_stop()
// Each call is time-limited (luasandbox::TimeLimit) and errors are printed once until they change.
//
// Functions (engine.*): get_online_players, get_player_info, message_player, kick_player,
// teleport_player, set_player_mode, get_player_mode, is_player_operator, get_server_info,
// player_set_block (a change made for a joined player, checked like their own edits).
// Kicks from mods wait until flush() (after the network update), so a mod can never remove a
// player while the server is still handling that player's packet.

#include "LuaSandbox.h"
#include "Net.h"
#include "Block.h"
#include <glm/glm.hpp>
#include <algorithm>
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
    std::string canEdit(const std::string& name, const glm::ivec3& pos, uint8_t oldId, uint8_t newId) {
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
        }, name, pos.x, pos.y, pos.z, blockName(reg, oldId), blockName(reg, newId));
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
        // engine.player_set_block(name, x, y, z, block) -> true, or false[, reason to show them]:
        // changes a block for a joined player with exactly the checks their own edits get (within
        // reach - with a block of slack, for a 3x3 patch -, spawn protection, can_edit_block).
        // For server-side actions that must only reward what was really allowed: make the change
        // first, then give what it yields only when this returns true.
        engine["player_set_block"] = [this](const std::string& name, int x, int y, int z, const std::string& block,
                                            sol::this_state ts) {
            sol::state_view lv(ts);
            std::string why;
            const bool ok = server && server->running() &&
                            server->playerSetBlock(name, glm::ivec3(x, y, z), block, &why, 2.0f);
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
            for (const auto& [name, pos] : server->playerPositions()) {
                PlayerInfo p;
                p.name = name;
                p.pos = pos;
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
