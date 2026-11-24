#pragma once
// Block text for Lua (ChunkData.h's BlockText: signs and anything else a player writes on),
// the same in the game and on the dedicated server:
//
//   engine.get_block_text(x, y, z) -> { front = { lines = {...}, color = "#rrggbb", glowing = bool },
//                                        back = { ... }, waxed = bool } or nil
//   engine.set_block_text(x, y, z, text or nil) -> true   (where the world runs: any text, any
//       block - a mod's own label; nil removes it)
//   engine.edit_block_text(x, y, z, side, lines) -> true if sent   ("front" / "back"; a player's
//       own edit, checked like any player's: a block that holds text, not waxed, can_edit_text)
//
// Hook (where the world runs): can_edit_text(name, x, y, z, side, lines) -> false[, reason] to
// refuse, a table of lines to write those instead, or nothing to allow it.

#include "ChunkData.h"
#include "World.h"
#include <sol/sol.hpp>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace blocktext {

inline std::string colorText(uint32_t c) {
    char buf[8];
    std::snprintf(buf, sizeof(buf), "#%06x", c & 0xFFFFFF);
    return buf;
}
inline uint32_t colorFrom(const sol::object& o, uint32_t fallback) {
    if (o.get_type() == sol::type::string) {
        std::string s = o.as<std::string>();
        if (!s.empty() && s[0] == '#') s.erase(0, 1);
        if (s.size() == 6) {
            unsigned v = 0;
            if (std::sscanf(s.c_str(), "%6x", &v) == 1) return v & 0xFFFFFF;
        }
    } else if (o.get_type() == sol::type::number) {
        return static_cast<uint32_t>(o.as<double>()) & 0xFFFFFF;
    }
    return fallback;
}

inline sol::table sideTable(sol::state_view lv, const BlockTextSide& s) {
    sol::table t = lv.create_table();
    sol::table lines = lv.create_table();
    for (size_t i = 0; i < s.lines.size(); ++i) lines[i + 1] = s.lines[i];
    t["lines"] = lines;
    t["color"] = colorText(s.color);
    t["glowing"] = s.glowing;
    return t;
}
inline sol::table toTable(sol::state_view lv, const BlockText& t) {
    sol::table o = lv.create_table();
    o["front"] = sideTable(lv, t.front);
    o["back"] = sideTable(lv, t.back);
    o["waxed"] = t.waxed;
    return o;
}
inline std::vector<std::string> linesFrom(const sol::object& o) {
    std::vector<std::string> out;
    if (o.get_type() != sol::type::table) return out;
    sol::table t = o.as<sol::table>();
    for (size_t i = 1; i <= BlockText::kMaxLines; ++i) {
        sol::object l = t[i];
        if (l.get_type() == sol::type::string) out.push_back(l.as<std::string>().substr(0, BlockText::kMaxLineBytes));
        else if (l.get_type() == sol::type::number) out.push_back(std::to_string(l.as<long long>()));
        else if (i > t.size()) break;
        else out.emplace_back();
    }
    return out;
}
inline void sideFrom(const sol::object& o, BlockTextSide& s) {
    if (o.get_type() != sol::type::table) return;
    sol::table t = o.as<sol::table>();
    s.lines = linesFrom(t["lines"]);
    s.color = colorFrom(t["color"], s.color);
    s.glowing = t.get_or("glowing", s.glowing);
}
inline BlockText fromTable(const sol::table& t, const BlockText* base = nullptr) {
    BlockText out = base ? *base : BlockText();
    sideFrom(t["front"], out.front);
    sideFrom(t["back"], out.back);
    out.waxed = t.get_or("waxed", out.waxed);
    return out;
}

// worldNow: the world code runs in (its dimension). editAsPlayer: this game's player writes
// (joined: sent to the server; otherwise checked and done here). authority: may set any text.
inline void install(sol::table engine, std::function<World*()> worldNow, std::function<bool()> authority,
                    std::function<bool(const glm::ivec3&, int, const std::vector<std::string>&)> editAsPlayer) {
    engine["get_block_text"] = [worldNow](int x, int y, int z, sol::this_state ts) -> sol::object {
        sol::state_view lv(ts);
        World* w = worldNow ? worldNow() : nullptr;
        const BlockText* t = w ? w->getBlockTextAt({x, y, z}) : nullptr;
        if (!t) return sol::make_object(lv, sol::lua_nil);
        return sol::make_object(lv, toTable(lv, *t));
    };
    engine["set_block_text"] = [worldNow, authority](int x, int y, int z, sol::object text) {
        if (authority && !authority()) return false;
        World* w = worldNow ? worldNow() : nullptr;
        if (!w) return false;
        if (text.get_type() != sol::type::table) return w->clearBlockTextAt({x, y, z}) || true;
        return w->setBlockTextAt({x, y, z}, fromTable(text.as<sol::table>(), w->getBlockTextAt({x, y, z})));
    };
    engine["edit_block_text"] = [editAsPlayer](int x, int y, int z, const std::string& side, sol::object lines) {
        if (!editAsPlayer) return false;
        return editAsPlayer({x, y, z}, side == "back" ? 1 : 0, linesFrom(lines));
    };
}

} // namespace blocktext
