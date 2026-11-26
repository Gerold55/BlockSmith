#pragma once
// Dimensions: the overworld plus any number of others that mods
// register (dimensions.register in Lua). Each is a world of its own - its own chunks (saved in
// saves/<world>/dimensions/<name>/), its own terrain generator, its own rules - and players can
// be in different ones at the same time. A dimension's rules are its type:
//
//   fixed_time          the sky stays at this time of day (0..1), or follows the day (-1)
//   has_skylight        sunlight lights it (false: only blocks give light, like the Nether)
//   has_ceiling         a roof over everything (no sky drawn, no weather)
//   ultrawarm           water can't be placed: it boils away (and lava flows further)
//   natural             compasses and clocks work, beds can be slept in
//   coordinate_scale    how far one block here is in overworld blocks (the Nether: 8)
//   bed_works           sleeping sets your spawn (false: beds explode, if the game says so)
//   respawn_anchor_works
//   ambient_light       the darkest it gets (0..1): the Nether is 0.1
//   logical_height      portals and teleports stay below this
//   effects             how the sky looks: "overworld" (sun, moon, stars, clouds), "nether" (no
//                       sky, only fog), "end" (a dark sky, no sun or moon)
//   fog_color, sky_color  for "nether" / "end" skies
//   caves               the engine's cave carver runs here (only the overworld by default)

#include <algorithm>
#include <map>
#include <mutex>
#include <string>
#include <vector>
#include <glm/glm.hpp>

struct DimensionType {
    std::string name;
    double fixedTime = -1.0;
    bool hasSkylight = true;
    bool hasCeiling = false;
    bool ultrawarm = false;
    bool natural = true;
    double coordinateScale = 1.0;
    bool bedWorks = true;
    bool respawnAnchorWorks = false;
    float ambientLight = 0.0f;
    int logicalHeight = 256;
    std::string effects = "overworld";
    glm::vec3 fogColor{0.6f, 0.7f, 0.9f};
    glm::vec3 skyColor{0.5f, 0.7f, 1.0f};
    bool caves = true;
    int terrainVersion = 0;       // like the overworld's terrain_version (untouched old chunks regenerate)
    // More settings (the defaults keep a dimension like the overworld)
    float gravity = 1.0f;         // times the overworld's gravity (players and mobs): 0.4 = floaty
    std::string respawn;          // where players who die here respawn: "" = the overworld (default),
                                  // "here" = this dimension, or another dimension's name
    bool hasSpawn = false;        // where players arrive in this dimension when no position is given
    glm::vec3 spawn{0.5f, 80.0f, 0.5f};
    double unloadAfter = 30.0;    // seconds with nobody in it before it's saved and unloaded (< 0: never)
    // What's drawn overhead (`effects` picks a preset; each can be set on its own)
    bool showSky = true, showSun = true, showMoon = true, showClouds = true, showWeather = true;
};

namespace dims {

inline const char* kOverworld = "overworld";

// A dimension name: "overworld", or "mod:name" - letters, digits, _ and one ':' (at most 64)
inline bool validName(const std::string& n) {
    if (n.empty() || n.size() > 64) return false;
    int colons = 0;
    for (char c : n) {
        if (c == ':') { ++colons; continue; }
        const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
        if (!ok) return false;
    }
    return colons <= 1 && n.front() != ':' && n.back() != ':';
}

// Where a dimension's chunks are saved, inside the world's folder ("" = the world folder itself)
inline std::string folderOf(const std::string& name) {
    if (name.empty() || name == kOverworld) return "";
    std::string f = name;
    std::replace(f.begin(), f.end(), ':', '_');
    return "dimensions/" + f;
}

// The dimension code is running "in" (where the player, block or entity it's about is): world functions
// (engine.set_block...) act on this dimension's world. The engine sets it around everything it
// runs for a player, a block, an entity or a dimension's world ("" = the overworld).
inline std::string& currentRef() { static thread_local std::string c; return c; }
inline const std::string& current() { return currentRef(); }
struct InDimension {
    std::string prev;
    explicit InDimension(const std::string& d) : prev(currentRef()) {
        currentRef() = (d == kOverworld) ? std::string() : d;
    }
    ~InDimension() { currentRef() = prev; }
    InDimension(const InDimension&) = delete;
    InDimension& operator=(const InDimension&) = delete;
};

// The dimensions this game knows (the overworld always). Read from any thread.
class Registry {
public:
    static Registry& get() { static Registry r; return r; }

    void add(const DimensionType& t) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_types[t.name] = t;
    }
    bool has(const std::string& name) const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return name == kOverworld || m_types.count(name) > 0;
    }
    // The type of a dimension (the overworld's rules if it isn't registered)
    DimensionType type(const std::string& name) const {
        std::lock_guard<std::mutex> lock(m_mutex);
        const auto it = m_types.find(name);
        if (it != m_types.end()) return it->second;
        DimensionType o;
        o.name = kOverworld;
        return o;
    }
    std::vector<std::string> names() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::vector<std::string> out{ kOverworld };
        for (const auto& [n, t] : m_types) if (n != kOverworld) out.push_back(n);
        return out;
    }
    // The mods are loaded again (another world, another game): only the overworld stays
    void clear() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_types.clear();
    }

private:
    Registry() = default;
    mutable std::mutex m_mutex;
    std::map<std::string, DimensionType> m_types;
};

// Where a player who died in `diedIn` respawns, by its `respawn` setting: "" = the overworld
// (the default, and anything unknown), "here" = the same dimension, or a dimension's name
inline std::string respawnDimension(const std::string& diedIn) {
    if (diedIn.empty() || diedIn == kOverworld) return std::string();
    const DimensionType t = Registry::get().type(diedIn);
    if (t.respawn == "here") return diedIn;
    if (t.respawn.empty() || t.respawn == kOverworld || !Registry::get().has(t.respawn)) return std::string();
    return t.respawn;
}

} // namespace dims
