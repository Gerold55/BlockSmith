#pragma once
// dimensions.register / get / list for every Lua state: the game's, the server's and each
// world generator thread's (so a dimension's generate_chunk runs on the generator threads,
// like the overworld's). See Dimensions.h for what a dimension's settings mean.
//
//   dimensions.register("mymod:depths", {
//       generate_chunk = function(chunk, cx, cz, seed) ... end,
//       has_skylight = false, has_ceiling = true, ultrawarm = true, natural = false,
//       coordinate_scale = 8, bed_works = false, respawn_anchor_works = true,
//       ambient_light = 0.1, logical_height = 128, fixed_time = 0.5,
//       effects = "nether", fog_color = "#330808",
//   })

#include "Dimensions.h"
#include <sol/sol.hpp>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>

namespace dims {

inline glm::vec3 colorFrom(const sol::object& o, const glm::vec3& fallback) {
    if (o.get_type() == sol::type::string) {
        const std::string s = o.as<std::string>();
        unsigned r = 0, g = 0, b = 0;
        const char* p = s.c_str();
        if (*p == '#') ++p;
        if (std::strlen(p) == 6 && std::sscanf(p, "%2x%2x%2x", &r, &g, &b) == 3) return glm::vec3(r, g, b) / 255.0f;
    } else if (o.get_type() == sol::type::table) {
        sol::table t = o.as<sol::table>();
        glm::vec3 c(t.get_or(1, fallback.r), t.get_or(2, fallback.g), t.get_or(3, fallback.b));
        if (c.r > 1.0f || c.g > 1.0f || c.b > 1.0f) c /= 255.0f;
        return glm::clamp(c, glm::vec3(0.0f), glm::vec3(1.0f));
    }
    return fallback;
}

inline sol::table typeTable(sol::state_view lv, const DimensionType& t) {
    sol::table o = lv.create_table();
    o["name"] = t.name;
    o["fixed_time"] = t.fixedTime < 0.0 ? sol::make_object(lv, sol::lua_nil) : sol::make_object(lv, t.fixedTime);
    o["has_skylight"] = t.hasSkylight;
    o["has_ceiling"] = t.hasCeiling;
    o["ultrawarm"] = t.ultrawarm;
    o["natural"] = t.natural;
    o["coordinate_scale"] = t.coordinateScale;
    o["bed_works"] = t.bedWorks;
    o["respawn_anchor_works"] = t.respawnAnchorWorks;
    o["ambient_light"] = t.ambientLight;
    o["logical_height"] = t.logicalHeight;
    o["effects"] = t.effects;
    sol::table fog = lv.create_table(); fog[1] = t.fogColor.r; fog[2] = t.fogColor.g; fog[3] = t.fogColor.b;
    sol::table sky = lv.create_table(); sky[1] = t.skyColor.r; sky[2] = t.skyColor.g; sky[3] = t.skyColor.b;
    o["fog_color"] = fog;
    o["sky_color"] = sky;
    o["caves"] = t.caves;
    o["gravity"] = t.gravity;
    o["respawn"] = t.respawn.empty() ? std::string(kOverworld) : t.respawn;
    if (t.hasSpawn) {
        sol::table sp = lv.create_table();
        sp["x"] = t.spawn.x; sp["y"] = t.spawn.y; sp["z"] = t.spawn.z;
        o["spawn"] = sp;
    }
    o["unload_after"] = t.unloadAfter;
    sol::table skyFlags = lv.create_table();
    skyFlags["sky"] = t.showSky; skyFlags["sun"] = t.showSun; skyFlags["moon"] = t.showMoon;
    skyFlags["clouds"] = t.showClouds; skyFlags["weather"] = t.showWeather;
    o["sky"] = skyFlags;
    // and every other field the mod gave (its own rules for its own mods to read)
    sol::object defs = lv["_dimension_defs"];
    if (defs.get_type() == sol::type::table) {
        sol::object def = defs.as<sol::table>()[t.name];
        if (def.get_type() == sol::type::table) {
            for (const auto& [k, v] : def.as<sol::table>()) {
                if (v.get_type() == sol::type::function) continue;
                const sol::object have = o[k];
                if (!have.valid() || have.get_type() == sol::type::lua_nil || have.get_type() == sol::type::none) o[k] = v;
            }
        }
    }
    return o;
}

// The settings a mod gave (anything left out: the overworld's, except caves: off)
inline DimensionType typeFrom(const std::string& name, const sol::table& d) {
    DimensionType t;
    t.name = name;
    t.caves = false;
    sol::object ft = d["fixed_time"];
    if (ft.get_type() == sol::type::number) t.fixedTime = std::clamp(ft.as<double>(), 0.0, 1.0);
    t.hasSkylight = d.get_or("has_skylight", true);
    t.hasCeiling = d.get_or("has_ceiling", false);
    t.ultrawarm = d.get_or("ultrawarm", false);
    t.natural = d.get_or("natural", true);
    t.coordinateScale = std::clamp(d.get_or("coordinate_scale", 1.0), 0.00001, 30000000.0);
    t.bedWorks = d.get_or("bed_works", true);
    t.respawnAnchorWorks = d.get_or("respawn_anchor_works", false);
    t.ambientLight = std::clamp(d.get_or("ambient_light", 0.0f), 0.0f, 1.0f);
    t.logicalHeight = std::clamp(d.get_or("logical_height", 256), 1, 256);
    const std::string fx = d.get_or("effects", std::string("overworld"));
    t.effects = (fx == "nether" || fx == "end") ? fx : "overworld";
    t.fogColor = colorFrom(d["fog_color"], fx == "nether" ? glm::vec3(0.2f, 0.03f, 0.03f) : fx == "end" ? glm::vec3(0.06f, 0.05f, 0.08f) : t.fogColor);
    t.skyColor = colorFrom(d["sky_color"], fx == "end" ? glm::vec3(0.09f, 0.07f, 0.12f) : t.skyColor);
    t.caves = d.get_or("caves", false);
    t.gravity = std::clamp(d.get_or("gravity", 1.0f), -4.0f, 10.0f);
    {
        const std::string r = d.get_or("respawn", std::string());
        t.respawn = (r == kOverworld) ? std::string() : r;
    }
    if (sol::optional<sol::table> sp = d["spawn"]) {
        t.hasSpawn = true;
        t.spawn = glm::vec3(sp->get_or("x", sp->get_or(1, 0.5f)), sp->get_or("y", sp->get_or(2, 80.0f)),
                            sp->get_or("z", sp->get_or(3, 0.5f)));
    }
    t.unloadAfter = d.get_or("unload_after", 30.0);
    // the sky: the preset's, then each piece the mod says
    const bool normal = t.effects == "overworld";
    t.showSky = t.showSun = t.showMoon = t.showClouds = t.showWeather = normal;
    sol::object skyOpt = d["sky"];
    if (skyOpt.get_type() == sol::type::table) {
        sol::table s = skyOpt.as<sol::table>();
        t.showSky = s.get_or("sky", t.showSky);
        t.showSun = s.get_or("sun", t.showSun);
        t.showMoon = s.get_or("moon", t.showMoon);
        t.showClouds = s.get_or("clouds", t.showClouds);
        t.showWeather = s.get_or("weather", t.showWeather);
    }
    if (t.hasCeiling) t.showWeather = d.get_or("weather_under_ceiling", false);
    t.terrainVersion = std::clamp(d.get_or("terrain_version", 0), 0, 65535);
    return t;
}

inline void installLua(sol::state& lua) {
    sol::table dimensions = lua["dimensions"].get_or_create<sol::table>();
    sol::table gens = lua["_dimension_generators"].get_or_create<sol::table>();
    (void)gens;
    dimensions["register"] = [](const std::string& name, sol::table def, sol::this_state ts) {
        if (name == kOverworld) throw std::runtime_error("dimensions.register: 'overworld' is the engine's own");
        if (!validName(name)) {
            throw std::runtime_error("dimensions.register: '" + name + "' isn't a dimension name "
                                     "(\"mymod:name\": lower-case letters, digits and _)");
        }
        sol::state_view lv(ts);
        sol::table g = lv["_dimension_generators"].get_or_create<sol::table>();
        sol::object fn = def["generate_chunk"];
        if (fn.get_type() == sol::type::function) g[name] = fn;
        lv["_dimension_defs"].get_or_create<sol::table>()[name] = def;   // (its own fields, for dimensions.get)
        Registry::get().add(typeFrom(name, def));
        return true;
    };
    dimensions["get"] = [](const std::string& name, sol::this_state ts) -> sol::object {
        sol::state_view lv(ts);
        if (!Registry::get().has(name)) return sol::make_object(lv, sol::lua_nil);
        return sol::make_object(lv, typeTable(lv, Registry::get().type(name)));
    };
    dimensions["list"] = [](sol::this_state ts) {
        sol::state_view lv(ts);
        sol::table out = lv.create_table();
        int i = 1;
        for (const auto& n : Registry::get().names()) out[i++] = n;
        return out;
    };
    dimensions["exists"] = [](const std::string& name) { return Registry::get().has(name); };
}

} // namespace dims
