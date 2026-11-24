#pragma once
// Entities: mobs, dropped items and anything else that moves around the world on its own.
//
// Who runs them: the authority - your game in singleplayer, your game when you host a LAN game,
// the dedicated server on a server. It moves them (gravity, walking, collisions with blocks),
// runs their mods' Lua (entities.register in EntityLua.h) and saves them with the world.
// Everyone else's game only gets *data* about them (EntityView: what type, where, which
// animation...) and draws them smoothly between updates - never code.
//
// This file has no Lua and no OpenGL: the dedicated server uses it as it is.

#include <glm/glm.hpp>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class World;

// A kind of entity, from entities.register (the parts the engine itself needs)
struct EntityType {
    std::string name;            // "mymod:cow"
    float width = 0.6f;          // hitbox (blocks): square footprint, centred on the position
    float height = 1.0f;         // ...and height, from the feet up
    float maxHealth = 10.0f;
    float stepHeight = 0.6f;     // walks up ledges this high without jumping
    bool gravity = true;
    bool collides = true;        // stopped by blocks (false: flies through everything)
    bool autoJump = true;        // walking into a 1-block step jumps it
    bool pushable = true;        // knocked back when hit
    // Bumps into other entities and players instead of walking through them (Minecraft's entity
    // pushing): overlapping bodies are nudged apart. false: passes through (ghosts, small critters)
    bool solid = true;
    bool despawn = false;        // removed when no player is near (most hostile mobs)
    bool isItem = false;         // a dropped item (engine-made, "__item")
    // Looks (only the games that draw it use these)
    std::string model, texture, animations;
    float scale = 1.0f;
    float eyeHeight = -1.0f;     // where it looks from (-1 = 85% of the height)
};

// One entity in the world, on the authority
struct Entity {
    uint32_t id = 0;
    std::string type;
    std::string dimension;       // which dimension it's in ("" = the overworld; Dimensions.h)
    glm::vec3 pos{0.0f};         // feet, centre
    glm::vec3 vel{0.0f};         // blocks per second
    float yaw = 0.0f;            // degrees (0 = facing +x, 90 = +z), the same as engine.add_model
    float health = 10.0f;
    bool onGround = false;
    bool inWater = false;
    bool removed = false;
    float age = 0.0f;            // seconds since it was spawned (or loaded)
    float noPlayerTime = 0.0f;   // seconds with no player nearby (despawning)
    float hurtTimer = 0.0f;      // > 0: just hit (can't be hit again yet; drawn red)
    // Walking: the horizontal speed it wants (set by Lua self:walk / set_velocity). Physics turns
    // it into velocity (with a little acceleration) so knockback isn't wiped out at once.
    glm::vec2 walk{0.0f};
    bool walking = false;
    bool blocked = false;        // walked into something it couldn't step or jump over (last step)
    // Following a path (go_to): waypoints (feet positions, cell centres), where it's going
    std::vector<glm::vec3> path;
    size_t pathIndex = 0;
    float pathSpeed = 1.0f;
    glm::vec3 pathGoal{0.0f};
    bool pathing = false;
    std::string pathState = "none";      // "following" (on its way), then "arrived", "partial" (as near as
                                         // it could get), "stuck"; "none" = no path
    bool pathReaches = true;             // the route goes all the way (false: only as near as it can)
    float repathCooldown = 0.0f;
    float stuckTimer = 0.0f;
    glm::vec3 progressPos{0.0f};
    size_t progressIndex = 0;
    int repaths = 0;
    // What everyone sees (changing these bumps `version`, so players' games get the news)
    std::string anim;            // animation name ("" = none / the rest pose)
    float animSpeed = 1.0f;
    bool animLoop = true;
    std::string nametag;
    std::string item;            // dropped items: what it is
    int itemCount = 0;
    uint8_t hurtCount = 0;       // goes up each time it's hit (the red flash)
    uint32_t version = 1;
    // Saved with the world: what its mod's on_save returned (it gets it back in on_spawn)
    std::string saved;
    float pickupDelay = 0.0f;    // dropped items: seconds before anyone can pick it up
    void touch() { ++version; if (version == 0) version = 1; }
};

// What a player's game knows about an entity (the network message, and what's drawn)
struct EntityView {
    uint32_t id = 0;
    std::string type;
    glm::vec3 pos{0.0f};
    float yaw = 0.0f;
    std::string anim;
    float animSpeed = 1.0f;
    bool animLoop = true;
    std::string nametag;
    std::string item;
    int itemCount = 0;
    uint8_t hurtCount = 0;
    float width = 0.6f, height = 1.0f;   // the hitbox (clicking it; a box if the model is unknown)
    float health = 0.0f, maxHealth = 0.0f;
    uint32_t version = 0;
};

// Spawning rules (entities.register_spawn): the engine tries a few spots near players every
// couple of seconds and spawns where all of these hold.
struct SpawnRule {
    std::string type;
    std::vector<std::string> onBlocks;   // standing on one of these ("" = anything solid)
    int minLight = 0, maxLight = 15;     // engine.get_light_level at its feet
    int minY = -1000000, maxY = 1000000;
    float chance = 1.0f;                 // of spawning when a spot qualifies
    int groupMin = 1, groupMax = 1;
    int maxNearby = 6;                   // no more of this type than this within 48 blocks
    std::string when = "any";            // "day", "night" or "any"
    bool needsSky = false;               // only where the sky is overhead (animals in fields)
    // The dimensions it spawns in ("" = the overworld; "*" = every one). Default: the overworld
    // only, so a mob written before dimensions doesn't turn up in a new one.
    std::vector<std::string> dimensions{ std::string() };
};

class EntityManager {
public:
    static constexpr int kMaxEntities = 2000;
    static constexpr float kActiveRange = 128.0f;   // further than this from every player: frozen

    void clear();
    void registerType(const EntityType& t) { m_types[t.name] = t; }
    const EntityType* type(const std::string& name) const;
    const std::map<std::string, EntityType>& types() const { return m_types; }

    // 0 if it can't (unknown type, too many entities)
    uint32_t spawn(const std::string& type, const glm::vec3& pos);
    Entity* get(uint32_t id);
    const Entity* get(uint32_t id) const;
    void remove(uint32_t id);            // gone at the end of this step (players' games told)
    const std::unordered_map<uint32_t, Entity>& all() const { return m_entities; }
    std::unordered_map<uint32_t, Entity>& all() { return m_entities; }

    // Moves everything one step: gravity, walking, water, block collisions, ageing. Entities
    // whose chunk isn't loaded, or with no player within kActiveRange, wait where they are.
    void step(float dt, const World& world, const std::vector<glm::vec3>& players);
    // Removed entities (since the last call) - for telling players' games
    std::vector<uint32_t> takeRemoved() { std::vector<uint32_t> out; out.swap(m_removedIds); return out; }

    // Hit: health down, knockback away from `from`, the red flash. Returns true if it died
    // (health <= 0; the caller decides what happens then).
    bool damage(Entity& e, float amount, const glm::vec3* from, float knockback = 1.0f);

    // The nearest entity the ray hits within maxDist (and how far), or 0
    uint32_t raycast(const glm::vec3& origin, const glm::vec3& dir, float maxDist, float* dist = nullptr) const;

    // The view players get
    EntityView view(const Entity& e) const;

    // Saving: entities.txt in the world folder (one line each). `saveData` fills e.saved
    // first (the mods' on_save).
    bool save(const std::string& worldDir, const std::function<void(Entity&)>& saveData);
    // Loads them back (their types must be registered by then; unknown ones are kept as they
    // were and saved again, so a missing mod doesn't delete its mobs). Returns how many.
    int load(const std::string& worldDir);

    // Spawning rules
    void addSpawnRule(const SpawnRule& r) { m_spawnRules.push_back(r); }
    const std::vector<SpawnRule>& spawnRules() const { return m_spawnRules; }
    // Every couple of seconds: tries spots around the players. `lightLevel(x,y,z)` = 0..15 now;
    // `isDay`. `skyVisible(x,y,z)`: is the sky overhead there (for rules with needsSky)? Left
    // empty, the world's own sunlight decides (full sunlight = sky). Returns the entities it
    // spawned (their on_spawn still has to run).
    std::vector<uint32_t> trySpawning(float dt, const World& world, const std::vector<glm::vec3>& players,
                                      const std::function<int(int, int, int)>& lightLevel, bool isDay,
                                      bool enabled, const std::function<bool(int, int, int)>& skyVisible = {});

    // Dropped items: "__item" entities that hold `item` x count
    static constexpr const char* kItemType = "__item";
    uint32_t dropItem(const std::string& item, int count, const glm::vec3& pos, const glm::vec3& vel);

    // ---- Finding the way ----
    // Can an entity of this type stand with its feet in this cell? (something to stand on
    // below, or water to swim in; room for its whole height; no lava)
    bool standable(const World& world, const EntityType& t, int x, int y, int z) const;
    // A walkable route (A* over the blocks): steps up 1 block only with headroom to jump,
    // drops of up to 3, no gaps lower than the entity, no corner cutting, water costs more.
    // Waypoints are feet positions; `reached` = false: it only gets as near as it can (the
    // route goes there). Empty = no way at all. maxNodes limits the search (cost).
    std::vector<glm::vec3> findPath(const World& world, const EntityType& t, const glm::vec3& from,
                                    const glm::vec3& to, bool* reached = nullptr, int maxNodes = 1500) const;
    // Sends it along a path to `goal` at `speed`. Calling it every tick is fine: it only looks
    // for a new route when the goal moves more than a block (at most ~3 times a second).
    // Returns false if there's no way at all.
    bool goTo(Entity& e, const World& world, const glm::vec3& goal, float speed);
    void stopPath(Entity& e) { e.pathing = false; e.path.clear(); e.pathState = "none"; }
    // The nearest dry land it can walk (or swim) to, within `radius` blocks. False if none.
    bool findLand(const World& world, const Entity& e, float radius, glm::vec3& out);
    // A random spot it can walk to, within `radius` blocks (for wandering), never in water. In
    // water it's the nearest land instead. False if none found.
    bool randomDestination(const World& world, const Entity& e, float radius, glm::vec3& out);
    // Can you see from a to b? (a ray through the blocks: anything solid or opaque stops it)
    static bool lineOfSight(const World& world, const glm::vec3& a, const glm::vec3& b);

    // Dropped items a player is close enough to pick up: { item entity, index into players }
    std::vector<std::pair<uint32_t, size_t>> itemsInReach(const std::vector<glm::vec3>& players) const;

private:
    void followPath(Entity& e, const EntityType& t, float dt, const World& world);
    // Overlapping solid entities (and players) nudged apart, through block collision
    void pushApart(float dt, const World& world, const std::vector<glm::vec3>& players,
                   const std::vector<Entity*>& active);
    void moveAndCollide(Entity& e, const EntityType& t, float dt, const World& world);
    std::map<std::string, EntityType> m_types;
    std::unordered_map<uint32_t, Entity> m_entities;
    std::vector<uint32_t> m_removedIds;
    std::vector<std::string> m_unknownLines;    // saved entities whose mod isn't loaded
    uint32_t m_nextId = 1;
    std::vector<SpawnRule> m_spawnRules;
    std::map<std::string, float> m_spawnTimers;   // per dimension
    // Dimensions: while stepping one dimension's entities, only theirs (nullptr = all)
    const std::string* m_only = nullptr;
    bool here(const Entity& e) const { return !m_only || e.dimension == *m_only; }
public:
    // Steps, spawning and pick-ups after this only see one dimension's entities (nullptr: all).
    // EntityLua sets it around each dimension's turn.
    void onlyDimension(const std::string* dimension) { m_only = dimension; }
private:
    uint32_t m_rng = 0x9E3779B9u;
    float random01();
};
