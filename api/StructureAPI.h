#pragma once
// The structure API (Lua table `structure`): builds as data. Copy a box of the world into a
// structure, turn it, mirror it, swap its blocks, place it anywhere (in the running world, or in
// a chunk while it's being generated), fill, replace and count, move whole builds - seen moving -
// and keep structures as JSON files: shipped inside a mod, or saved for every world of a game.
//
// Structures are data only: block names, cells and sign text, never code.
//
// Positions are tables: { x = 1, y = 2, z = 3 } or { 1, 2, 3 }. Corners of a box are inclusive,
// in any order. The functions marked [world] work where the world runs (singleplayer, hosting,
// the dedicated server: a joined player's game asks its server through a mod), and every chunk
// they touch must be loaded. The others work everywhere, also on world generator threads.
//
// Making and changing structures
//   structure.new(sx, sy, sz) -> an empty structure (air) of that size
//   structure.read(p1, p2) -> structure, or nil + why                                    [world]
//   structure.from_text(text) -> structure, or nil + why (and a list of blocks this game lacks)
//   s:size() -> sx, sy, sz          s:get(x, y, z) -> block, meta    (0-based, inside it)
//   s:set(x, y, z, block [, meta])  s:count([block])                 s:copy()
//   s:rotate(degrees) -> a new structure, turned clockwise seen from above (90, 180, 270, -90...)
//   s:flip("x" | "y" | "z") -> a new structure, mirrored
//   s:replace(from, to [, meta]) -> how many cells changed (in this structure)
//   s:to_text() -> JSON text (the schematic file format)
//
// Placing (`what`: a structure, or the name of a registered one)
//   structure.place(what, pos [, opts]) -> blocks changed, or nil + why                  [world]
//   structure.register_on_generated(function(chunk, cx, cz, dimension) end)
//       (called for every new chunk, after the terrain and its caves, whichever mod made them)
//   structure.place_in_chunk(chunk, cx, cz, what, pos [, opts]) -> cells written
//       (inside generate_chunk: writes the part of it that falls in this chunk. Call it from every
//       chunk the structure overlaps, with the same pos and options, and it comes out whole)
//   opts: rotation = 0 | 90 | 180 | 270 | "random" (place only)   flip = "x" | "z"
//         center = true ("xz") | "x" | "y" | "z" | "xyz"... (pos is the middle on those axes;
//                otherwise pos is the lowest corner)
//         replacements = { ["oak_planks"] = "spruce_planks", ... }
//         air = true (its air clears what's there; false: only its blocks are placed)
//         force = true (false: blocks are placed only into air, fluids and plants)
//
// Mods' own structures (files in the mod's schematics/ folder; every copy of the mod has them,
// generator threads too)
//   structure.register(name, { file = "ruin.json" } | { text = "..." } | { structure = s })
//       -> true, or nil + why       (name: "mymod:ruin"; letters, numbers, _ - and :)
//   structure.get(name) -> a copy, or nil         structure.registered() -> { name, ... }
//
// Editing the world in boxes                                                             [world]
//   structure.fill(p1, p2, block [, meta]) -> blocks changed      ("air" clears)
//   structure.replace(p1, p2, from, to [, meta]) -> blocks changed
//   structure.count(p1, p2 [, block]) -> how many (every block but air, or just that block)
//   structure.move(p1, p2, offset [, { seconds = 1.5, on_done = function(id) end }])
//       -> id, or nil + why. The blocks lift out and glide `offset` (whole blocks) as one piece,
//       then settle there; everyone in that world sees it. 0 seconds: at once.
//   structure.is_moving([id]) -> bool
//   Hook: on_structure_moved(id) - a move has been placed.
//
// Shared schematics: files in schematics/<game>/<name>.json next to the mods folder (on a
// dedicated server, next to where it runs), shared by every world of the game.
//   structure.save(name, s) -> true, or nil + why  [world]   structure.load(name) -> s, or nil + why
//   structure.list() -> { name, ... }                         structure.delete(name) -> bool [world]
//   (names: letters, numbers, _ and -, up to 40)

#include "World.h"
#include "Region.h"
#include "Block.h"
#include "ChunkData.h"
#include "Fluids.h"
#include "SafeReplace.h"
#include <sol/sol.hpp>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <random>
#include <sstream>
#include <string>
#include <vector>

namespace structure {

// The most blocks one fill or replace may change (the same as the biggest structure)
constexpr size_t kMaxEdit = Region::kMaxCells;
// The biggest structure file read (JSON text)
constexpr size_t kMaxFileBytes = 64u * 1024u * 1024u;
constexpr const char* kSchematicExt = ".json";
constexpr const char* kRegistryKey = "blocksmith.structures";

inline glm::ivec3 posFrom(const sol::object& o, const char* what) {
    auto bad = [what]() { return sol::error(std::string(what) + ": a position, { x = ..., y = ..., z = ... }"); };
    if (!o.is<sol::table>()) throw bad();
    const sol::table t = o.as<sol::table>();
    const sol::optional<double> x = t["x"], y = t["y"], z = t["z"];
    const sol::optional<double> x1 = t[1], y1 = t[2], z1 = t[3];
    auto pick = [&](const sol::optional<double>& a, const sol::optional<double>& b) -> double {
        if (a) return *a;
        if (b) return *b;
        throw bad();
    };
    const double vx = pick(x, x1), vy = pick(y, y1), vz = pick(z, z1);
    if (!std::isfinite(vx) || !std::isfinite(vy) || !std::isfinite(vz) || std::fabs(vx) > 3.0e7 || std::fabs(vy) > 3.0e7 || std::fabs(vz) > 3.0e7)
        throw bad();
    return glm::ivec3(static_cast<int>(std::floor(vx)), static_cast<int>(std::floor(vy)), static_cast<int>(std::floor(vz)));
}

inline uint8_t blockFrom(const std::string& name, const char* what) {
    if (name == "air" || name.empty()) return 0;
    const uint8_t id = BlockRegistry::getInstance().getBlockId(name);
    if (id == 0) throw sol::error(std::string(what) + ": no block called " + name);
    return id;
}

inline std::string nameOf(uint8_t id) { return id == 0 ? std::string("air") : BlockRegistry::getInstance().getBlock(id).name; }

// What force = false may place into: air, fluids and plants (as engine.is_replaceable)
inline bool replaceable(uint8_t id) {
    if (id == 0) return true;
    if (FluidRegistry::get().forBlock(id) != nullptr) return true;
    return BlockRegistry::getInstance().getBlock(id).pattern == "plantlike";
}

// ---------------- Moves' callbacks ----------------

struct MoveCallbacks {
    std::map<std::pair<const World*, uint32_t>, sol::protected_function> pending;
};
inline MoveCallbacks& moveCallbacks() { static MoveCallbacks m; return m; }

// Call after the world's update (where the world runs): runs on_done callbacks and the
// on_structure_moved hook for moves that have just been placed
inline void runMoveCallbacks(sol::state& lua, World& w) {
    const auto done = w.takeMovesDone();
    if (done.empty()) return;
    for (uint32_t id : done) {
        auto& pending = moveCallbacks().pending;
        const auto it = pending.find({ &w, id });
        if (it != pending.end()) {
            sol::protected_function fn = it->second;
            pending.erase(it);
            auto r = fn(id);
            if (!r.valid()) { sol::error e = r; std::cerr << "[Lua] structure.move on_done: " << e.what() << std::endl; }
        }
        sol::protected_function hook = lua["on_structure_moved"];
        if (hook.valid()) {
            auto r = hook(id);
            if (!r.valid()) { sol::error e = r; std::cerr << "[Lua] on_structure_moved: " << e.what() << std::endl; }
        }
    }
}
// Forget callbacks (mods reloaded, world left)
inline void clearMoveCallbacks() { moveCallbacks().pending.clear(); }

// ---------------- Names and files ----------------

inline bool schematicName(const std::string& n) {
    if (n.empty() || n.size() > 40) return false;
    for (char c : n) if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) return false;
    return true;
}
inline bool registeredName(const std::string& n) {
    if (n.empty() || n.size() > 80) return false;
    for (char c : n) if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == ':')) return false;
    return true;
}
// A file inside a mod's schematics/ folder: "ruin.json", "village/house_1.json" (no "..", no
// absolute paths, .json only)
inline bool modFileName(const std::string& f) {
    if (f.empty() || f.size() > 200 || f.front() == '/' || f.front() == '\\') return false;
    if (f.find("..") != std::string::npos || f.find(':') != std::string::npos || f.find('\\') != std::string::npos) return false;
    if (f.size() < 6 || f.compare(f.size() - 5, 5, kSchematicExt) != 0) return false;
    for (char c : f) if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.' || c == '/')) return false;
    return true;
}

inline bool readFile(const std::filesystem::path& file, std::string& out, std::string& why) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(file, ec)) { why = "no file " + file.generic_string(); return false; }
    if (std::filesystem::file_size(file, ec) > kMaxFileBytes) { why = "too big"; return false; }
    std::ifstream in(file, std::ios::binary);
    if (!in) { why = "can't read " + file.generic_string(); return false; }
    std::stringstream ss;
    ss << in.rdbuf();
    out = ss.str();
    return true;
}

// ---------------- Placing ----------------

struct PlaceOptions {
    int quarterTurns = 0;
    char flip = 0;                         // 0, 'x' or 'z'
    bool centerX = false, centerY = false, centerZ = false;
    bool air = true, force = true;
    std::vector<std::pair<uint8_t, uint8_t>> replacements;   // from -> to
};

// `random`: for rotation = "random" (nullptr: not allowed here)
inline PlaceOptions placeOptions(const sol::optional<sol::table>& opts, const char* what, const std::function<int()>& random) {
    PlaceOptions o;
    if (!opts) return o;
    const sol::table& t = *opts;
    const sol::object rot = t["rotation"];
    if (rot.is<std::string>() && rot.as<std::string>() == "random") {
        if (!random) throw sol::error(std::string(what) + ": rotation \"random\" isn't allowed here (work it out from the seed and the position, so every chunk agrees)");
        o.quarterTurns = random();
    } else if (rot.is<double>()) {
        const double d = rot.as<double>();
        if (!std::isfinite(d) || std::fmod(d, 90.0) != 0.0) throw sol::error(std::string(what) + ": rotation is 0, 90, 180 or 270");
        o.quarterTurns = static_cast<int>(d / 90.0);
    } else if (rot.valid() && rot.get_type() != sol::type::lua_nil) {
        throw sol::error(std::string(what) + ": rotation is 0, 90, 180, 270 or \"random\"");
    }
    const sol::optional<std::string> flip = t["flip"];
    if (flip) {
        if (*flip != "x" && *flip != "z") throw sol::error(std::string(what) + ": flip is \"x\" or \"z\"");
        o.flip = (*flip)[0];
    }
    const sol::object center = t["center"];
    if (center.is<bool>()) {
        o.centerX = o.centerZ = center.as<bool>();
    } else if (center.is<std::string>()) {
        const std::string c = center.as<std::string>();
        o.centerX = c.find('x') != std::string::npos;
        o.centerY = c.find('y') != std::string::npos;
        o.centerZ = c.find('z') != std::string::npos;
    }
    o.air = t.get_or("air", true);
    o.force = t.get_or("force", true);
    const sol::optional<sol::table> rep = t["replacements"];
    if (rep) {
        for (const auto& kv : *rep) {
            if (!kv.first.is<std::string>() || !kv.second.is<std::string>())
                throw sol::error(std::string(what) + ": replacements are { [\"from\"] = \"to\", ... }");
            const std::string from = kv.first.as<std::string>();
            const uint8_t fromId = from == "air" ? 0 : BlockRegistry::getInstance().getBlockId(from);
            if (fromId == 0 && from != "air") continue;   // (a block this game lacks: nothing to replace)
            o.replacements.emplace_back(fromId, blockFrom(kv.second.as<std::string>(), what));
        }
    }
    return o;
}

// The structure as it will be placed: mirrored, turned, blocks swapped
inline Region prepared(const Region& src, const PlaceOptions& o) {
    Region r = o.flip ? src.flipped(o.flip) : src;
    if (o.quarterTurns % 4 != 0) r = r.rotated(o.quarterTurns);
    if (!o.replacements.empty()) {
        uint8_t map[256];
        for (int i = 0; i < 256; ++i) map[i] = static_cast<uint8_t>(i);
        for (const auto& [from, to] : o.replacements) map[from] = to;
        for (size_t i = 0; i < r.ids.size(); ++i) {
            const uint8_t before = r.ids[i];
            r.ids[i] = map[before];
            if (r.ids[i] == 0) r.metas[i] = 0;
            if (r.ids[i] != before) r.texts.erase(static_cast<uint32_t>(i));
        }
    }
    return r;
}

// The lowest corner, from pos and the centre options
inline glm::ivec3 originOf(const Region& r, const glm::ivec3& pos, const PlaceOptions& o) {
    glm::ivec3 p = pos;
    if (o.centerX) p.x -= r.size.x / 2;
    if (o.centerY) p.y -= r.size.y / 2;
    if (o.centerZ) p.z -= r.size.z / 2;
    return p;
}

// Into the running world -> blocks changed
inline size_t placeInWorld(World& w, const Region& r, const glm::ivec3& origin, const PlaceOptions& o) {
    if (o.air && o.force) return w.writeRegion(r, origin, true);
    size_t changed = 0;
    for (int x = 0; x < r.size.x; ++x)
        for (int z = 0; z < r.size.z; ++z)
            for (int y = 0; y < r.size.y; ++y) {
                const glm::ivec3 p = origin + glm::ivec3(x, y, z);
                if (p.y < 0 || p.y >= CHUNK_HEIGHT) continue;
                const size_t i = r.index(x, y, z);
                const uint8_t id = r.ids[i], meta = r.metas[i];
                if (id == 0 && !o.air) continue;
                const uint8_t cur = w.getBlockAt(p);
                if (!o.force && !replaceable(cur)) continue;
                if (cur != id || w.getMetadataAt(p) != meta) {
                    w.setBlockWithMetaAt(p, id, meta);
                    ++changed;
                }
                const auto t = r.texts.find(static_cast<uint32_t>(i));
                if (t != r.texts.end()) w.setBlockTextAt(p, t->second);
                else if (w.getBlockTextAt(p)) w.clearBlockTextAt(p);
            }
    return changed;
}

// Into a chunk being generated (cx, cz): only the cells inside it -> cells written
inline size_t placeInChunk(ChunkData& c, int cx, int cz, const Region& r, const glm::ivec3& origin, const PlaceOptions& o) {
    const int x0 = cx * CHUNK_SIZE, z0 = cz * CHUNK_SIZE;
    const int ax = std::max(0, x0 - origin.x), bx = std::min(r.size.x, x0 + CHUNK_SIZE - origin.x);
    const int az = std::max(0, z0 - origin.z), bz = std::min(r.size.z, z0 + CHUNK_SIZE - origin.z);
    size_t n = 0;
    for (int x = ax; x < bx; ++x)
        for (int z = az; z < bz; ++z)
            for (int y = 0; y < r.size.y; ++y) {
                const int wy = origin.y + y;
                if (wy < 0 || wy >= CHUNK_HEIGHT) continue;
                const int lx = origin.x + x - x0, lz = origin.z + z - z0;
                const size_t i = r.index(x, y, z);
                const uint8_t id = r.ids[i];
                if (id == 0 && !o.air) continue;
                if (!o.force && !replaceable(c.getLocalBlock(lx, wy, lz))) continue;
                c.setBlockWithMeta(lx, wy, lz, id, r.metas[i]);
                const auto t = r.texts.find(static_cast<uint32_t>(i));
                if (t != r.texts.end()) c.setText(lx, wy, lz, t->second);
                else c.clearText(lx, wy, lz);
                ++n;
            }
    return n;
}

// ---------------- Lua ----------------

// structure + nil, or nil + why (+ the blocks this game lacks)
inline std::tuple<sol::object, sol::object, sol::object> fromTextResult(sol::state_view lv, const std::string& text) {
    if (text.size() > kMaxFileBytes) return { sol::make_object(lv, sol::lua_nil), sol::make_object(lv, std::string("too big")), sol::make_object(lv, sol::lua_nil) };
    Region r;
    std::string why;
    std::vector<std::string> missing;
    if (!Region::fromText(text, r, why, &missing))
        return { sol::make_object(lv, sol::lua_nil), sol::make_object(lv, why), sol::make_object(lv, sol::lua_nil) };
    sol::table m = lv.create_table();
    for (size_t i = 0; i < missing.size(); ++i) m[i + 1] = missing[i];
    return { sol::make_object(lv, std::move(r)), sol::make_object(lv, sol::lua_nil), sol::make_object(lv, m) };
}

inline sol::table registryOf(sol::state_view lv) {
    sol::object t = lv.registry()[kRegistryKey];
    if (t.is<sol::table>()) return t.as<sol::table>();
    sol::table fresh = lv.create_table();
    lv.registry()[kRegistryKey] = fresh;
    return fresh;
}

// A registered structure. Ones from text or a file are kept as text until first used: block names
// are matched to blocks then, after every mod has registered its blocks (whatever order the mods
// load in). nullptr: none of that name.
inline const Region* registered(sol::state_view lv, const std::string& name) {
    sol::table reg = registryOf(lv);
    const sol::object e = reg[name];
    if (e.is<Region>()) return &e.as<const Region&>();
    if (!e.is<std::string>()) return nullptr;
    Region r;
    std::string why;
    std::vector<std::string> missing;
    Region::fromText(e.as<std::string>(), r, why, &missing);   // (checked when registered)
    // (said once, by the main thread's copy: generator threads load the same mods)
    const bool quiet = lv.registry()["blocksmith.structures.quiet"].get_or(false);
    if (!missing.empty() && !quiet) {
        std::string list;
        for (const auto& m : missing) list += (list.empty() ? "" : ", ") + m;
        std::cerr << "[Structure] " << name << ": blocks this game doesn't have became air: " << list << std::endl;
    }
    reg[name] = std::move(r);
    const sol::object done = reg[name];
    return &done.as<const Region&>();
}

// `what`: a structure, or a registered name
inline const Region& structureOf(sol::state_view lv, const sol::object& what, const char* fn) {
    if (what.is<Region>()) return what.as<const Region&>();
    if (what.is<std::string>()) {
        if (const Region* r = registered(lv, what.as<std::string>())) return *r;
        throw sol::error(std::string(fn) + ": no structure registered as " + what.as<std::string>());
    }
    throw sol::error(std::string(fn) + ": a structure, or the name of a registered one");
}

// The part that works everywhere, world generator threads included: the Structure type, making
// and reading structures, mods' registered structures, and placing into a chunk being generated.
// Returns the `structure` table.
// quiet: a world generator thread (its copy of the mods loads the same files: the main thread's
// copy reports their problems)
inline sol::table installData(sol::state_view lua, bool quiet = false) {
    sol::table st = lua["structure"].get_or_create<sol::table>();
    lua.registry()["blocksmith.structures.quiet"] = quiet;

    lua.new_usertype<Region>("Structure", sol::no_constructor,
        "size", [](const Region& r) { return std::make_tuple(r.size.x, r.size.y, r.size.z); },
        "get", [](const Region& r, int x, int y, int z) -> std::tuple<std::string, int> {
            if (!r.inside(x, y, z)) return { "air", 0 };
            const size_t i = r.index(x, y, z);
            return { nameOf(r.ids[i]), r.metas[i] };
        },
        "set", [](Region& r, int x, int y, int z, const std::string& block, sol::optional<int> meta) {
            if (!r.inside(x, y, z)) throw sol::error("structure:set: outside the structure");
            const size_t i = r.index(x, y, z);
            r.ids[i] = blockFrom(block, "structure:set");
            r.metas[i] = r.ids[i] ? static_cast<uint8_t>(std::clamp(meta.value_or(0), 0, 255)) : 0;
            r.texts.erase(static_cast<uint32_t>(i));
        },
        "count", [](const Region& r, sol::optional<std::string> block) {
            size_t n = 0;
            if (block) {
                const uint8_t id = blockFrom(*block, "structure:count");
                for (uint8_t v : r.ids) if (v == id) ++n;
            } else {
                for (uint8_t v : r.ids) if (v != 0) ++n;
            }
            return n;
        },
        "replace", [](Region& r, const std::string& from, const std::string& to, sol::optional<int> meta) {
            const uint8_t fromId = blockFrom(from, "structure:replace"), toId = blockFrom(to, "structure:replace");
            const uint8_t m = toId ? static_cast<uint8_t>(std::clamp(meta.value_or(0), 0, 255)) : 0;
            size_t n = 0;
            for (size_t i = 0; i < r.ids.size(); ++i) {
                if (r.ids[i] != fromId) continue;
                r.ids[i] = toId;
                if (toId == 0 || meta) r.metas[i] = m;
                r.texts.erase(static_cast<uint32_t>(i));
                ++n;
            }
            return n;
        },
        "rotate", [](const Region& r, int degrees) {
            if (degrees % 90 != 0) throw sol::error("structure:rotate: a multiple of 90 degrees");
            return r.rotated(degrees / 90);
        },
        "flip", [](const Region& r, const std::string& axis) {
            if (axis != "x" && axis != "y" && axis != "z") throw sol::error("structure:flip: \"x\", \"y\" or \"z\"");
            return r.flipped(axis[0]);
        },
        "copy", [](const Region& r) { return r; },
        "to_text", [](const Region& r) { return r.toText(); });

    st["new"] = [](int sx, int sy, int sz) {
        const glm::ivec3 size(sx, sy, sz);
        if (!Region::validSize(size))
            throw sol::error("structure.new: x/z up to 4096, y up to 256, and at most " +
                             std::to_string(Region::kMaxCells) + " blocks");
        Region r;
        r.resize(size);
        return r;
    };
    st["from_text"] = [](const std::string& text, sol::this_state ts) { return fromTextResult(ts, text); };

    // ---- Mods' own structures ----
    st["register"] = [](const std::string& name, sol::table def, sol::this_state ts) {
        sol::state_view lv(ts);
        auto fail = [&](const std::string& why) { return std::make_tuple(sol::make_object(lv, sol::lua_nil), sol::make_object(lv, why)); };
        if (!registeredName(name)) throw sol::error("structure.register: a name like \"mymod:ruin\" (letters, numbers, _ - and :)");
        const sol::object given = def["structure"];
        const sol::optional<std::string> file = def["file"], text = def["text"];
        std::string content, why;
        if (given.is<Region>()) {
            if (given.as<const Region&>().empty()) return fail("an empty structure");
            registryOf(lv)[name] = Region(given.as<const Region&>());
            return std::make_tuple(sol::make_object(lv, true), sol::make_object(lv, sol::lua_nil));
        }
        if (text) {
            content = *text;
        } else if (file) {
            if (!modFileName(*file)) throw sol::error("structure.register: file is a .json file in your mod's schematics folder, like \"ruin.json\"");
            const sol::optional<std::string> modPath = lv["g_CurrentModPath"];
            if (!modPath || modPath->empty()) return fail("register file structures while your mod loads");
            if (!readFile(std::filesystem::path(*modPath) / "schematics" / *file, content, why)) return fail(why);
        } else {
            throw sol::error("structure.register: { file = \"ruin.json\" }, { text = ... } or { structure = s }");
        }
        if (content.size() > kMaxFileBytes) return fail("too big");
        Region check;   // (malformed text is refused now; block names are matched when it's first used)
        if (!Region::fromText(content, check, why)) return fail((file ? *file + ": " : std::string()) + why);
        if (check.empty()) return fail("an empty structure");
        registryOf(lv)[name] = content;
        return std::make_tuple(sol::make_object(lv, true), sol::make_object(lv, sol::lua_nil));
    };
    st["get"] = [](const std::string& name, sol::this_state ts) -> sol::object {
        sol::state_view lv(ts);
        const Region* r = registered(lv, name);
        if (!r) return sol::make_object(lv, sol::lua_nil);
        return sol::make_object(lv, Region(*r));
    };
    st["registered"] = [](sol::this_state ts) {
        sol::state_view lv(ts);
        std::vector<std::string> names;
        for (const auto& kv : registryOf(lv)) if (kv.first.is<std::string>()) names.push_back(kv.first.as<std::string>());
        std::sort(names.begin(), names.end());
        sol::table t = lv.create_table();
        for (size_t i = 0; i < names.size(); ++i) t[i + 1] = names[i];
        return t;
    };

    // ---- World generation ----
    // structure.register_on_generated(function(chunk, cx, cz, dimension) end): called for every new
    // chunk, after the terrain (whichever mod makes it) and its caves. On world generator threads.
    st["register_on_generated"] = [](sol::protected_function fn, sol::this_state ts) {
        sol::state_view lv(ts);
        if (!fn.valid()) throw sol::error("structure.register_on_generated: a function(chunk, cx, cz, dimension)");
        sol::object list = lv.registry()["blocksmith.on_generated"];
        if (!list.is<sol::table>()) { lv.registry()["blocksmith.on_generated"] = lv.create_table(); list = lv.registry()["blocksmith.on_generated"]; }
        sol::table t = list.as<sol::table>();
        t[t.size() + 1] = fn;
    };
    st["place_in_chunk"] = [](ChunkData& chunk, int cx, int cz, sol::object what, sol::object pos, sol::optional<sol::table> opts, sol::this_state ts) {
        sol::state_view lv(ts);
        const Region& src = structureOf(lv, what, "structure.place_in_chunk");
        const PlaceOptions o = placeOptions(opts, "structure.place_in_chunk", nullptr);
        const glm::ivec3 p = posFrom(pos, "structure.place_in_chunk");
        if (o.quarterTurns % 4 == 0 && !o.flip && o.replacements.empty())
            return placeInChunk(chunk, cx, cz, src, originOf(src, p, o), o);
        const Region r = prepared(src, o);
        return placeInChunk(chunk, cx, cz, r, originOf(r, p, o), o);
    };
    return st;
}

// Everything: installData, plus the [world] functions and shared schematics.
// worldNow: the world mods are working in now; authority: true where the world runs;
// schematicDir: the folder this game's shared schematics are in
inline void install(sol::state_view lua, std::function<World*()> worldNow, std::function<bool()> authority,
                    std::function<std::filesystem::path()> schematicDir = nullptr) {
    namespace fs = std::filesystem;
    sol::table st = installData(lua);

    auto need = [worldNow, authority](const char* what) -> World* {
        World* w = worldNow ? worldNow() : nullptr;
        if (!w) throw sol::error(std::string(what) + ": not in a world");
        if (authority && !authority()) throw sol::error(std::string(what) + ": only where the world runs (a joined player's game can ask its server through a mod)");
        return w;
    };

    // ---- Reading and placing ----
    st["read"] = [need](sol::object a, sol::object b, sol::this_state ts) {
        sol::state_view lv(ts);
        World* w = need("structure.read");
        Region r;
        std::string why;
        if (!w->readRegion(posFrom(a, "structure.read"), posFrom(b, "structure.read"), r, why))
            return std::make_tuple(sol::make_object(lv, sol::lua_nil), sol::make_object(lv, why));
        return std::make_tuple(sol::make_object(lv, std::move(r)), sol::make_object(lv, sol::lua_nil));
    };
    st["place"] = [need](sol::object what, sol::object pos, sol::optional<sol::table> opts, sol::this_state ts) {
        sol::state_view lv(ts);
        World* w = need("structure.place");
        const Region& src = structureOf(lv, what, "structure.place");
        const PlaceOptions o = placeOptions(opts, "structure.place", [lv]() {
            sol::protected_function rnd = lv["math"]["random"];
            auto r = rnd(0, 3);
            return r.valid() ? r.get<int>() : 0;
        });
        const glm::ivec3 p = posFrom(pos, "structure.place");
        const Region r = prepared(src, o);
        const glm::ivec3 origin = originOf(r, p, o);
        const glm::ivec3 top = origin + r.size - glm::ivec3(1);
        const glm::ivec3 lo(origin.x, std::max(origin.y, 0), origin.z), hi(top.x, std::min(top.y, CHUNK_HEIGHT - 1), top.z);
        if (hi.y >= lo.y && !w->regionLoaded(lo, hi))
            return std::make_tuple(sol::make_object(lv, sol::lua_nil), sol::make_object(lv, std::string("part of it isn't loaded")));
        return std::make_tuple(sol::make_object(lv, placeInWorld(*w, r, origin, o)), sol::make_object(lv, sol::lua_nil));
    };

    // a box's corners, checked: inside the world's height, loaded, not too big
    auto box = [](World* w, sol::object a, sol::object b, const char* what, glm::ivec3& mn, glm::ivec3& mx) {
        const glm::ivec3 p = posFrom(a, what), q = posFrom(b, what);
        mn = glm::min(p, q);
        mx = glm::max(p, q);
        mn.y = std::max(mn.y, 0);
        mx.y = std::min(mx.y, CHUNK_HEIGHT - 1);
        if (mx.y < mn.y) return false;
        const glm::ivec3 s = mx - mn + glm::ivec3(1);
        if (!Region::validSize(s))
            throw sol::error(std::string(what) + ": invalid or too big (x/z up to 4096, y up to 256, " +
                             std::to_string(kMaxEdit) + " blocks at most)");
        if (!w->regionLoaded(mn, mx)) throw sol::error(std::string(what) + ": part of the box isn't loaded");
        return true;
    };

    st["fill"] = [need, box](sol::object a, sol::object b, const std::string& block, sol::optional<int> meta) {
        World* w = need("structure.fill");
        glm::ivec3 mn, mx;
        if (!box(w, a, b, "structure.fill", mn, mx)) return size_t(0);
        Region r;
        r.resize(mx - mn + glm::ivec3(1));
        const uint8_t id = blockFrom(block, "structure.fill");
        std::fill(r.ids.begin(), r.ids.end(), id);
        std::fill(r.metas.begin(), r.metas.end(), id ? static_cast<uint8_t>(std::clamp(meta.value_or(0), 0, 255)) : uint8_t(0));
        return w->writeRegion(r, mn, true);
    };
    st["replace"] = [need, box](sol::object a, sol::object b, const std::string& from, const std::string& to, sol::optional<int> meta) {
        World* w = need("structure.replace");
        glm::ivec3 mn, mx;
        if (!box(w, a, b, "structure.replace", mn, mx)) return size_t(0);
        const uint8_t fromId = blockFrom(from, "structure.replace"), toId = blockFrom(to, "structure.replace");
        const uint8_t m = toId ? static_cast<uint8_t>(std::clamp(meta.value_or(0), 0, 255)) : 0;
        size_t n = 0;
        for (int x = mn.x; x <= mx.x; ++x)
            for (int z = mn.z; z <= mx.z; ++z)
                for (int y = mn.y; y <= mx.y; ++y) {
                    const glm::ivec3 p(x, y, z);
                    if (w->getBlockAt(p) != fromId) continue;
                    w->setBlockWithMetaAt(p, toId, m);
                    ++n;
                }
        return n;
    };
    st["count"] = [worldNow, box](sol::object a, sol::object b, sol::optional<std::string> block) {
        World* w = worldNow ? worldNow() : nullptr;
        if (!w) throw sol::error("structure.count: not in a world");
        glm::ivec3 mn, mx;
        if (!box(w, a, b, "structure.count", mn, mx)) return size_t(0);
        const int only = block ? blockFrom(*block, "structure.count") : -1;
        size_t n = 0;
        for (int x = mn.x; x <= mx.x; ++x)
            for (int z = mn.z; z <= mx.z; ++z)
                for (int y = mn.y; y <= mx.y; ++y) {
                    const uint8_t id = w->getBlockAt(glm::ivec3(x, y, z));
                    if (only >= 0 ? id == only : id != 0) ++n;
                }
        return n;
    };

    // ---- Moving ----
    st["move"] = [need](sol::object a, sol::object b, sol::object offset, sol::optional<sol::table> opts, sol::this_state ts) {
        sol::state_view lv(ts);
        World* w = need("structure.move");
        float seconds = 1.5f;
        sol::protected_function onDone;
        if (opts) {
            const sol::optional<double> s = (*opts)["seconds"];
            if (s && std::isfinite(*s)) seconds = static_cast<float>(*s);
            const sol::object f = (*opts)["on_done"];
            if (f.is<sol::function>()) onDone = f.as<sol::protected_function>();
        }
        std::string why;
        const uint32_t id = w->startMove(posFrom(a, "structure.move"), posFrom(b, "structure.move"),
                                         posFrom(offset, "structure.move"), seconds, why);
        if (id == 0) return std::make_tuple(sol::make_object(lv, sol::lua_nil), sol::make_object(lv, why));
        if (onDone.valid()) moveCallbacks().pending[{ w, id }] = onDone;
        return std::make_tuple(sol::make_object(lv, id), sol::make_object(lv, sol::lua_nil));
    };
    st["is_moving"] = [worldNow](sol::optional<uint32_t> id) {
        World* w = worldNow ? worldNow() : nullptr;
        if (!w) return false;
        if (!id) return w->isMoving();
        for (const auto& m : w->movingRegions()) if (m.id == *id) return true;
        return false;
    };

    // ---- Schematics shared between worlds of the same game ----
    auto fileOf = [schematicDir](const std::string& name, const char* what) {
        if (!schematicName(name)) throw sol::error(std::string(what) + ": a name of letters, numbers, _ and - (up to 40)");
        if (!schematicDir) throw sol::error(std::string(what) + ": no schematics folder here");
        return schematicDir() / (name + kSchematicExt);
    };
    st["save"] = [fileOf, authority](const std::string& name, const Region& r, sol::this_state ts) {
        sol::state_view lv(ts);
        const fs::path file = fileOf(name, "structure.save");
        if (authority && !authority()) return std::make_tuple(sol::make_object(lv, sol::lua_nil), sol::make_object(lv, std::string("only where the world runs")));
        if (!r.valid()) return std::make_tuple(sol::make_object(lv, sol::lua_nil), sol::make_object(lv, std::string("invalid or oversized structure")));
        std::error_code ec;
        fs::create_directories(file.parent_path(), ec);
        const fs::path tmp = file.string() + ".tmp";
        {
            std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
            if (!out) return std::make_tuple(sol::make_object(lv, sol::lua_nil), sol::make_object(lv, std::string("can't write ") + file.string()));
            out << r.toText();
        }
        if (!replaceFile(tmp, file, ec)) return std::make_tuple(sol::make_object(lv, sol::lua_nil), sol::make_object(lv, "can't save: " + ec.message()));
        return std::make_tuple(sol::make_object(lv, true), sol::make_object(lv, sol::lua_nil));
    };
    st["load"] = [fileOf](const std::string& name, sol::this_state ts) {
        sol::state_view lv(ts);
        const fs::path file = fileOf(name, "structure.load");
        std::string text, why;
        std::error_code ec;
        if (!fs::is_regular_file(file, ec)) why = "no schematic called " + name;
        else readFile(file, text, why);
        if (!why.empty()) return std::make_tuple(sol::make_object(lv, sol::lua_nil), sol::make_object(lv, why), sol::make_object(lv, sol::lua_nil));
        auto [s, err, missing] = fromTextResult(lv, text);
        return std::make_tuple(s, err, missing);
    };
    st["list"] = [schematicDir](sol::this_state ts) {
        sol::state_view lv(ts);
        sol::table t = lv.create_table();
        if (!schematicDir) return t;
        std::vector<std::string> names;
        std::error_code ec;
        for (const auto& e : fs::directory_iterator(schematicDir(), ec)) {
            if (e.path().extension() != kSchematicExt) continue;
            const std::string n = e.path().stem().string();
            if (schematicName(n)) names.push_back(n);
        }
        std::sort(names.begin(), names.end());
        for (size_t i = 0; i < names.size(); ++i) t[i + 1] = names[i];
        return t;
    };
    st["delete"] = [fileOf, authority](const std::string& name) {
        const fs::path file = fileOf(name, "structure.delete");
        if (authority && !authority()) return false;
        std::error_code ec;
        return fs::remove(file, ec);
    };
}

} // namespace structure
