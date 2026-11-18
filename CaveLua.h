#pragma once
// world.set_cave_settings / world.get_cave_settings - shared by the game and the dedicated
// server, so a game's caves come out the same on both (see CaveGenerator.h)
#include <sol/sol.hpp>
#include "CaveGenerator.h"

// world.set_cave_settings({ tunnels = true, tunnel_frequency = 1, tunnel_width = 1, ravines = true,
//   ravine_frequency = 1, caverns = true, cavern_size = 1, noodles = true, lava_level = 10, max_y = 200 })
// Only the keys given change. Call it while your mod loads, before the world generates.
inline void applyCaveSettings(const sol::table& t) {
    CaveSettings s = CaveGenerator::settings();
    s.tunnels         = t.get_or("tunnels", s.tunnels);
    s.tunnelFrequency = t.get_or("tunnel_frequency", s.tunnelFrequency);
    s.tunnelWidth     = t.get_or("tunnel_width", s.tunnelWidth);
    s.ravines         = t.get_or("ravines", s.ravines);
    s.ravineFrequency = t.get_or("ravine_frequency", s.ravineFrequency);
    s.caverns         = t.get_or("caverns", s.caverns);
    s.cavernSize      = t.get_or("cavern_size", s.cavernSize);
    s.noodles         = t.get_or("noodles", s.noodles);
    s.lavaLevel       = t.get_or("lava_level", s.lavaLevel);
    s.maxY            = t.get_or("max_y", s.maxY);
    CaveGenerator::setSettings(s);
}

inline sol::table caveSettingsTable(sol::this_state ts) {
    sol::state_view lua(ts);
    const CaveSettings s = CaveGenerator::settings();
    sol::table t = lua.create_table();
    t["enabled"] = CaveGenerator::isEnabled();
    t["tunnels"] = s.tunnels;
    t["tunnel_frequency"] = s.tunnelFrequency;
    t["tunnel_width"] = s.tunnelWidth;
    t["ravines"] = s.ravines;
    t["ravine_frequency"] = s.ravineFrequency;
    t["caverns"] = s.caverns;
    t["cavern_size"] = s.cavernSize;
    t["noodles"] = s.noodles;
    t["lava_level"] = s.lavaLevel;
    t["max_y"] = s.maxY;
    return t;
}
