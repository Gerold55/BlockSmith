#pragma once
// Block states for Lua (BlockShape.h): how a block is turned, a slab's half, stairs' facing and
// corner, waterlogging - the same in the game and on the dedicated server.
//
//   engine.get_block_state(x, y, z) -> table or nil (air / unloaded):
//     { block = "oak_stairs",
//       facing = "north"|"east"|"south"|"west"                (stairs)
//       upside_down = bool                                     (stairs)
//       shape = "straight"|"inner_left"|"inner_right"|"outer_left"|"outer_right"   (stairs: the
//               shape it has now), shape_auto = bool (true: worked out from the stairs around it)
//       axis = "x"|"y"|"z"                                     (rotation = "axis": logs)
//       half = "bottom"|"top"|"double"                         (slabs)
//       waterlogged = bool, can_waterlog = bool }
//   engine.set_block_state(x, y, z, { facing = ..., upside_down = ..., shape = ..., axis = ...,
//       half = ..., waterlogged = ... }) -> true   (only what's given changes; the block stays).
//       shape = "auto" goes back to joining with the stairs around it.
//   engine.rotate_block(x, y, z [, turns]) -> true: stairs a quarter turn clockwise seen from
//       above (turns may be negative); logs lying down swap X and Z
//   engine.is_waterlogged(x, y, z), engine.can_waterlog(x, y, z),
//   engine.set_waterlogged(x, y, z, bool) -> true if it changed
//   engine.place_block(x, y, z, block [, state]) -> true: a block with a state at once

#include "Block.h"
#include "BlockShape.h"
#include "World.h"
#include <sol/sol.hpp>
#include <functional>
#include <string>

namespace blockstate {

inline const char* facingName(int f) {
    static const char* n[4] = { "north", "east", "south", "west" };
    return (f >= 0 && f < 4) ? n[f] : "north";
}
inline int facingFrom(const sol::object& o, int fallback) {
    if (o.get_type() == sol::type::number) return o.as<int>();
    if (o.get_type() != sol::type::string) return fallback;
    const std::string s = o.as<std::string>();
    for (int f = 0; f < 4; ++f) if (s == facingName(f)) return f;
    return fallback;
}
inline int axisFrom(const sol::object& o, int fallback) {
    if (o.get_type() == sol::type::number) return o.as<int>();
    if (o.get_type() != sol::type::string) return fallback;
    const std::string s = o.as<std::string>();
    return s == "y" ? 0 : s == "x" ? 1 : s == "z" ? 2 : fallback;
}

// The metadata for a block with these state fields, starting from `meta`. False if a field
// doesn't fit the block (shown as a Lua error by the caller).
inline bool applyState(const BlockProperty& p, uint8_t& meta, const sol::table& t, std::string& err) {
    const sol::object facing = t["facing"], axis = t["axis"], half = t["half"], upside = t["upside_down"], wet = t["waterlogged"];
    const sol::object shapeObj = t["shape"];
    if (shape::isSlab(p)) {
        if (half.valid() && half.get_type() != sol::type::lua_nil) {
            const std::string h = half.get_type() == sol::type::string ? half.as<std::string>() : "";
            const int v = h == "bottom" ? 0 : h == "top" ? 1 : h == "double" ? 2 : -1;
            if (v < 0) { err = "half must be \"bottom\", \"top\" or \"double\""; return false; }
            meta = static_cast<uint8_t>((meta & ~3) | v);
        }
    } else if (shape::isStairs(p)) {
        if (facing.valid() && facing.get_type() != sol::type::lua_nil) {
            const int f = facingFrom(facing, -1);
            if (f < 0 || f > 3) { err = "stairs face north, east, south or west"; return false; }
            meta = static_cast<uint8_t>((meta & ~shape::kStairFacing) | f);
        }
        if (upside.get_type() == sol::type::boolean)
            meta = static_cast<uint8_t>(upside.as<bool>() ? (meta | shape::kStairUpsideDown) : (meta & ~shape::kStairUpsideDown));
        if (shapeObj.valid() && shapeObj.get_type() != sol::type::lua_nil) {
            const int sh = shapeObj.get_type() == sol::type::string ? shape::stairShapeFrom(shapeObj.as<std::string>()) : -1;
            if (sh < 0) { err = "shape must be \"auto\", \"straight\", \"inner_left\", \"inner_right\", \"outer_left\" or \"outer_right\""; return false; }
            meta = static_cast<uint8_t>((meta & ~shape::kStairShapeMask) | (sh << shape::kStairShapeShift));
        }
    } else if (p.rotation == shape::kRotAxis) {
        if (axis.valid() && axis.get_type() != sol::type::lua_nil) {
            const int a = axisFrom(axis, -1);
            if (a < 0 || a > 2) { err = "axis must be \"x\", \"y\" or \"z\""; return false; }
            meta = static_cast<uint8_t>((meta & ~3) | a);
        }
    }
    if (wet.get_type() == sol::type::boolean) {
        if (wet.as<bool>()) {
            if (!shape::canWaterlog(p, meta)) { err = "this block can't hold water"; return false; }
            meta |= shape::kWaterlogged;
        } else {
            meta &= static_cast<uint8_t>(~shape::kWaterlogged);
        }
    }
    // a double slab is a full block: no water in it
    if (shape::isSlab(p) && (meta & 3) == shape::kSlabDouble) meta &= static_cast<uint8_t>(~shape::kWaterlogged);
    return true;
}

inline void install(sol::table engine, std::function<World*()> worldNow) {
    auto& reg = BlockRegistry::getInstance();
    engine["get_block_state"] = [worldNow, &reg](int x, int y, int z, sol::this_state ts) -> sol::object {
        sol::state_view lv(ts);
        World* w = worldNow ? worldNow() : nullptr;
        if (!w) return sol::make_object(lv, sol::lua_nil);
        const uint8_t id = w->getBlockAt({x, y, z});
        if (id == 0) return sol::make_object(lv, sol::lua_nil);
        const BlockProperty& p = reg.getBlock(id);
        const uint8_t meta = w->getMetadataAt({x, y, z});
        sol::table t = lv.create_table();
        t["block"] = p.name;
        if (shape::isSlab(p)) {
            const int h = meta & 3;
            t["half"] = h == 1 ? "top" : h == 2 ? "double" : "bottom";
        } else if (shape::isStairs(p)) {
            t["facing"] = facingName(meta & shape::kStairFacing);
            t["upside_down"] = (meta & shape::kStairUpsideDown) != 0;
            auto get = [&](int dx, int dy, int dz, uint8_t& nid, uint8_t& nm) {
                nid = w->getBlockAt({x + dx, y + dy, z + dz});
                nm = w->getMetadataAt({x + dx, y + dy, z + dz});
                return true;
            };
            int other = 0;
            const int kind = shape::stairShape(p, meta, get, other);
            const bool left = other == shape::turnLeft(meta & shape::kStairFacing);
            t["shape"] = kind == 1 ? (left ? "outer_left" : "outer_right") : kind == 2 ? (left ? "inner_left" : "inner_right") : "straight";
            t["shape_auto"] = (meta & shape::kStairShapeMask) == 0;
        } else if (p.rotation == shape::kRotAxis) {
            const int a = meta & 3;
            t["axis"] = a == 1 ? "x" : a == 2 ? "z" : "y";
        }
        t["can_waterlog"] = shape::canWaterlog(p, meta);
        t["waterlogged"] = shape::isWaterlogged(p, meta);
        return sol::make_object(lv, t);
    };
    engine["set_block_state"] = [worldNow, &reg](int x, int y, int z, sol::table state) {
        World* w = worldNow ? worldNow() : nullptr;
        if (!w) return false;
        const uint8_t id = w->getBlockAt({x, y, z});
        if (id == 0) return false;
        uint8_t meta = w->getMetadataAt({x, y, z});
        std::string err;
        if (!applyState(reg.getBlock(id), meta, state, err)) throw sol::error("engine.set_block_state: " + err);
        if (meta != w->getMetadataAt({x, y, z})) w->setMetadataAt({x, y, z}, meta);
        return true;
    };
    engine["place_block"] = [worldNow, &reg](int x, int y, int z, const std::string& name, sol::optional<sol::table> state) {
        World* w = worldNow ? worldNow() : nullptr;
        if (!w) return false;
        const uint8_t id = reg.getBlockId(name);
        if (id == 0 && name != "air") throw sol::error("engine.place_block: no block called " + name);
        uint8_t meta = 0;
        if (state) {
            std::string err;
            if (!applyState(reg.getBlock(id), meta, *state, err)) throw sol::error("engine.place_block: " + err);
        }
        w->setBlockWithMetaAt({x, y, z}, id, meta);
        return true;
    };
    engine["rotate_block"] = [worldNow, &reg](int x, int y, int z, sol::optional<int> turnsIn) {
        World* w = worldNow ? worldNow() : nullptr;
        if (!w) return false;
        const uint8_t id = w->getBlockAt({x, y, z});
        if (id == 0) return false;
        const BlockProperty& p = reg.getBlock(id);
        uint8_t meta = w->getMetadataAt({x, y, z});
        const int turns = ((turnsIn.value_or(1) % 4) + 4) % 4;
        if (shape::isStairs(p)) {
            meta = static_cast<uint8_t>((meta & ~3) | (((meta & 3) + turns) & 3));
        } else if (p.rotation == shape::kRotAxis) {
            const int a = meta & 3;
            if ((turns & 1) && a != 0) meta = static_cast<uint8_t>((meta & ~3) | (a == 1 ? 2 : 1));
        } else {
            return false;   // nothing to turn
        }
        w->setMetadataAt({x, y, z}, meta);
        return true;
    };
    engine["can_waterlog"] = [worldNow, &reg](int x, int y, int z) {
        World* w = worldNow ? worldNow() : nullptr;
        if (!w) return false;
        const uint8_t id = w->getBlockAt({x, y, z});
        return id != 0 && shape::canWaterlog(reg.getBlock(id), w->getMetadataAt({x, y, z}));
    };
    engine["is_waterlogged"] = [worldNow, &reg](int x, int y, int z) {
        World* w = worldNow ? worldNow() : nullptr;
        if (!w) return false;
        const uint8_t id = w->getBlockAt({x, y, z});
        return id != 0 && shape::isWaterlogged(reg.getBlock(id), w->getMetadataAt({x, y, z}));
    };
    engine["set_waterlogged"] = [worldNow, &reg](int x, int y, int z, bool on) {
        World* w = worldNow ? worldNow() : nullptr;
        if (!w) return false;
        const uint8_t id = w->getBlockAt({x, y, z});
        if (id == 0) return false;
        const uint8_t meta = w->getMetadataAt({x, y, z});
        if (!shape::canWaterlog(reg.getBlock(id), meta)) return false;
        const uint8_t next = on ? static_cast<uint8_t>(meta | shape::kWaterlogged) : static_cast<uint8_t>(meta & ~shape::kWaterlogged);
        if (next == meta) return false;
        w->setMetadataAt({x, y, z}, next);
        return true;
    };
}

} // namespace blockstate
