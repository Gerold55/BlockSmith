// Entities.cpp: see Entities.h
#include "Entities.h"
#include "World.h"
#include "Block.h"
#include "Fluids.h"
#include "DoorShape.h"
#include "Light.h"
#include "Types.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <queue>
#include <unordered_map>

namespace fs = std::filesystem;

namespace {

// A block's collision box (world coordinates); false = nothing to bump into
bool solidBox(const World& world, int x, int y, int z, glm::vec3& mn, glm::vec3& mx) {
    if (y < 0 || y >= CHUNK_HEIGHT) return false;
    const uint8_t id = world.getBlockAt({x, y, z});
    if (id == 0) return false;
    if (FluidRegistry::get().forBlock(id)) return false;
    const auto& prop = BlockRegistry::getInstance().getBlock(id);
    if (!prop.isWalkable) return false;
    const glm::vec3 cell(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z));
    if (prop.pattern == "door") {
        door::box(world.getMetadataAt({x, y, z}), mn, mx);
        mn += cell;
        mx += cell;
    } else {
        mn = cell;
        mx = cell + glm::vec3(1.0f);
    }
    return true;
}

// How far the box [mn, mx] can move along `axis` by `d` before touching a block. Boxes it
// already overlaps are ignored, so something stuck inside a block can still get out.
float sweep(const World& world, const glm::vec3& mn, const glm::vec3& mx, int axis, float d) {
    if (d == 0.0f) return 0.0f;
    constexpr float kEps = 1e-4f;
    glm::vec3 lo = mn, hi = mx;
    if (d > 0.0f) hi[axis] += d; else lo[axis] += d;
    float allowed = d;
    for (int x = static_cast<int>(std::floor(lo.x)); x <= static_cast<int>(std::floor(hi.x - kEps)); ++x) {
        for (int y = static_cast<int>(std::floor(lo.y)); y <= static_cast<int>(std::floor(hi.y - kEps)); ++y) {
            for (int z = static_cast<int>(std::floor(lo.z)); z <= static_cast<int>(std::floor(hi.z - kEps)); ++z) {
                glm::vec3 bmn, bmx;
                if (!solidBox(world, x, y, z, bmn, bmx)) continue;
                bool across = true;
                for (int k = 0; k < 3 && across; ++k) {
                    if (k == axis) continue;
                    across = mn[k] < bmx[k] - kEps && mx[k] > bmn[k] + kEps;
                }
                if (!across) continue;
                if (d > 0.0f) {
                    if (bmn[axis] < mx[axis] - kEps) continue;             // already inside it
                    allowed = std::min(allowed, std::max(0.0f, bmn[axis] - mx[axis] - (axis == 1 ? 0.0f : kEps)));
                } else {
                    if (bmx[axis] > mn[axis] + kEps) continue;
                    allowed = std::max(allowed, std::min(0.0f, bmx[axis] - mn[axis] + (axis == 1 ? 0.0f : kEps)));
                }
            }
        }
    }
    return allowed;
}

bool boxFree(const World& world, const glm::vec3& mn, const glm::vec3& mx) {
    for (int x = static_cast<int>(std::floor(mn.x)); x <= static_cast<int>(std::floor(mx.x - 1e-4f)); ++x)
        for (int y = static_cast<int>(std::floor(mn.y)); y <= static_cast<int>(std::floor(mx.y - 1e-4f)); ++y)
            for (int z = static_cast<int>(std::floor(mn.z)); z <= static_cast<int>(std::floor(mx.z - 1e-4f)); ++z) {
                glm::vec3 bmn, bmx;
                if (!solidBox(world, x, y, z, bmn, bmx)) continue;
                if (mn.x < bmx.x && mx.x > bmn.x && mn.y < bmx.y && mx.y > bmn.y && mn.z < bmx.z && mx.z > bmn.z) return false;
            }
    return true;
}

// ---- Finding the way: what a body can be in, and stand on ----
int bodyCells(float height) { return std::max(1, static_cast<int>(std::ceil(height - 0.01f))); }

bool dangerousBlock(uint8_t id) {
    if (id == 0) return false;
    if (const FluidDef* f = FluidRegistry::get().forBlock(id)) {
        return f->name.find("lava") != std::string::npos || !f->swimmable;
    }
    const std::string& n = BlockRegistry::getInstance().getBlock(id).name;
    return n.find("lava") != std::string::npos || n.find("fire") != std::string::npos ||
           n.find("cactus") != std::string::npos;
}

// Can a body be in this cell? (air, plants, water - not solid blocks, not lava)
bool passable(const World& world, int x, int y, int z) {
    if (y < 0 || y >= CHUNK_HEIGHT) return false;
    if (!world.isLoaded({x, y, z})) return false;
    const uint8_t id = world.getBlockAt({x, y, z});
    if (id == 0) return true;
    if (dangerousBlock(id)) return false;
    if (FluidRegistry::get().forBlock(id)) return true;
    return !BlockRegistry::getInstance().getBlock(id).isWalkable;
}

bool columnClear(const World& world, int x, int y, int z, int cells) {
    for (int k = 0; k < cells; ++k) if (!passable(world, x, y + k, z)) return false;
    return true;
}

bool isWaterAt(const World& world, int x, int y, int z) {
    const uint8_t id = world.getBlockAt({x, y, z});
    const FluidDef* f = FluidRegistry::get().forBlock(id);
    return f && !dangerousBlock(id);
}

int64_t cellKey(int x, int y, int z) {
    return (static_cast<int64_t>(x) & 0x1FFFFF) | ((static_cast<int64_t>(z) & 0x1FFFFF) << 21) |
           ((static_cast<int64_t>(y) & 0x3FFFF) << 42);
}

std::string escapeField(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '\\') out += "\\\\";
        else if (c == '\t') out += "\\t";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else out += c;
    }
    return out;
}
std::string unescapeField(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            const char n = s[++i];
            out += n == 't' ? '\t' : n == 'n' ? '\n' : n == 'r' ? '\r' : n;
        } else {
            out += s[i];
        }
    }
    return out;
}

} // namespace

void EntityManager::clear() {
    m_entities.clear();
    m_removedIds.clear();
    m_unknownLines.clear();
    m_spawnTimer = 0.0f;
}

const EntityType* EntityManager::type(const std::string& name) const {
    auto it = m_types.find(name);
    if (it != m_types.end()) return &it->second;
    if (name == kItemType) {
        static const EntityType item = [] {
            EntityType t;
            t.name = kItemType;
            t.width = 0.25f; t.height = 0.25f;
            t.maxHealth = 5.0f;
            t.stepHeight = 0.0f;
            t.autoJump = false;
            t.pushable = false;
            t.isItem = true;
            return t;
        }();
        return &item;
    }
    return nullptr;
}

float EntityManager::random01() {
    m_rng ^= m_rng << 13; m_rng ^= m_rng >> 17; m_rng ^= m_rng << 5;
    return static_cast<float>(m_rng & 0xFFFFFF) / static_cast<float>(0x1000000);
}

uint32_t EntityManager::spawn(const std::string& typeName, const glm::vec3& pos) {
    const EntityType* t = type(typeName);
    if (!t || m_entities.size() >= static_cast<size_t>(kMaxEntities)) return 0;
    if (!std::isfinite(pos.x) || !std::isfinite(pos.y) || !std::isfinite(pos.z)) return 0;
    Entity e;
    e.id = m_nextId++;
    if (m_nextId == 0) m_nextId = 1;
    e.type = typeName;
    e.pos = pos;
    e.health = t->maxHealth;
    m_entities[e.id] = e;
    return e.id;
}

Entity* EntityManager::get(uint32_t id) {
    auto it = m_entities.find(id);
    return (it == m_entities.end() || it->second.removed) ? nullptr : &it->second;
}
const Entity* EntityManager::get(uint32_t id) const {
    auto it = m_entities.find(id);
    return (it == m_entities.end() || it->second.removed) ? nullptr : &it->second;
}

void EntityManager::remove(uint32_t id) {
    auto it = m_entities.find(id);
    if (it != m_entities.end()) it->second.removed = true;
}

uint32_t EntityManager::dropItem(const std::string& item, int count, const glm::vec3& pos, const glm::vec3& vel) {
    if (item.empty() || count <= 0) return 0;
    const uint32_t id = spawn(kItemType, pos);
    if (Entity* e = get(id)) {
        e->item = item;
        e->itemCount = count;
        e->vel = vel;
        e->yaw = random01() * 360.0f;
        e->pickupDelay = 0.5f;
    }
    return id;
}

bool EntityManager::damage(Entity& e, float amount, const glm::vec3* from, float knockback) {
    const EntityType* t = type(e.type);
    e.health -= amount;
    e.hurtTimer = 0.5f;
    ++e.hurtCount;
    e.touch();
    if (from && t && t->pushable && knockback > 0.0f) {
        glm::vec2 away(e.pos.x - from->x, e.pos.z - from->z);
        const float len = glm::length(away);
        away = len > 1e-3f ? away / len : glm::vec2(1.0f, 0.0f);
        e.vel.x = away.x * 6.0f * knockback;
        e.vel.z = away.y * 6.0f * knockback;
        e.vel.y = std::max(e.vel.y, 5.0f * std::min(knockback, 1.5f));
        e.onGround = false;
    }
    return e.health <= 0.0f;
}

void EntityManager::moveAndCollide(Entity& e, const EntityType& t, float dt, const World& world) {
    // Water (and other fluids) around its body: how deep it is in. The surface is the top of
    // the water in its column; `submerged` = how much of its height is under it.
    float submerged = 0.0f;
    {
        const int cx = static_cast<int>(std::floor(e.pos.x)), cz = static_cast<int>(std::floor(e.pos.z));
        const int feet = static_cast<int>(std::floor(e.pos.y));
        auto fluidAt = [&](int y) { return FluidRegistry::get().forBlock(world.getBlockAt({cx, y, cz})) != nullptr; };
        int y = feet;
        if (!fluidAt(y) && fluidAt(y - 1) && e.pos.y - feet < 0.05f) y = feet - 1;   // (feet right on the surface)
        if (fluidAt(y)) {
            while (y < feet + 8 && fluidAt(y + 1)) ++y;
            submerged = std::clamp(static_cast<float>(y + 1) - e.pos.y, 0.0f, t.height);
        }
        e.inWater = submerged > 0.05f;
    }

    // Walking: the speed it wants, reached quickly on the ground and slowly in the air
    const glm::vec2 horiz(e.vel.x, e.vel.z);
    if (e.walking) {
        const float k = e.onGround || e.inWater ? 12.0f : 2.0f;
        // swimming is slower than walking (legs in the water)
        const glm::vec2 want = e.inWater && submerged > t.height * 0.3f ? e.walk * 0.55f : e.walk;
        const glm::vec2 v = horiz + (want - horiz) * std::min(1.0f, k * dt);
        e.vel.x = v.x; e.vel.z = v.y;
    } else {
        const float friction = e.onGround ? (t.isItem ? 8.0f : 10.0f) : (e.inWater ? 3.0f : 0.5f);
        const float f = std::exp(-friction * dt);
        e.vel.x *= f; e.vel.z *= f;
    }

    if (t.gravity) {
        if (e.inWater) {
            // Floats with about half its body under the water (legs down, head and back out),
            // like Minecraft's animals; dropped items float on top. In water shallower than that
            // it just stands on the bottom.
            const float floatDepth = t.isItem ? t.height * 0.4f : std::min(t.height * 0.55f, 1.0f);
            const float target = std::clamp((submerged - floatDepth) * 4.0f, -2.0f, 2.5f);
            e.vel.y += (target - e.vel.y) * std::min(1.0f, 5.0f * dt);
        } else {
            e.vel.y -= 24.0f * dt;
            e.vel.y = std::max(e.vel.y, -50.0f);
        }
    }

    const float hw = t.width * 0.5f;
    auto boxMin = [&](const glm::vec3& p) { return glm::vec3(p.x - hw, p.y, p.z - hw); };
    auto boxMax = [&](const glm::vec3& p) { return glm::vec3(p.x + hw, p.y + t.height, p.z + hw); };

    if (!t.collides) {
        e.pos += e.vel * dt;
        e.onGround = false;
        return;
    }

    // In small pieces so nothing tunnels through a block
    glm::vec3 move = e.vel * dt;
    const int pieces = std::max(1, static_cast<int>(std::ceil(glm::length(move) / 0.4f)));
    move /= static_cast<float>(pieces);
    const bool wasOnGround = e.onGround;
    bool hitWall = false;
    e.onGround = false;
    for (int i = 0; i < pieces; ++i) {
        // Up/down first
        const float dy = sweep(world, boxMin(e.pos), boxMax(e.pos), 1, move.y);
        if (dy != move.y) {
            if (move.y < 0.0f) e.onGround = true;
            e.vel.y = 0.0f;
        }
        e.pos.y += dy;
        // Then across, each way; a low ledge is stepped up onto
        for (int axis : {0, 2}) {
            const float d = move[axis];
            if (d == 0.0f) continue;
            const float allowed = sweep(world, boxMin(e.pos), boxMax(e.pos), axis, d);
            if (allowed != d && (e.onGround || wasOnGround) && t.stepHeight > 0.0f) {
                glm::vec3 up = e.pos;
                up.y += t.stepHeight;
                if (boxFree(world, boxMin(up), boxMax(up))) {
                    const float a2 = sweep(world, boxMin(up), boxMax(up), axis, d);
                    if (std::fabs(a2) > std::fabs(allowed) + 1e-3f) {
                        up[axis] += a2;
                        // settle back down onto the ledge
                        up.y += sweep(world, boxMin(up), boxMax(up), 1, -t.stepHeight);
                        e.pos = up;
                        continue;
                    }
                }
            }
            if (allowed != d) {
                hitWall = true;
                e.vel[axis] = 0.0f;
            }
            e.pos[axis] += allowed;
        }
    }
    // Still on the ground? (standing still: the downward sweep above didn't run into anything
    // because nothing moved; check just under the feet)
    if (!e.onGround && e.vel.y <= 0.0f) {
        const float below = sweep(world, boxMin(e.pos), boxMax(e.pos), 1, -0.05f);
        if (below > -0.05f) e.onGround = true;
    }
    // Walking into a block too high to step: jump it - but only when that works: a single
    // block in the way, with room for the whole body on top of it and room to jump here.
    // (No jumping at a 1-block-high gap a 2-block mob can't fit through, or at a tree trunk.)
    e.blocked = false;
    if (hitWall && e.walking && !e.inWater) {
        glm::vec2 dir = e.walk;
        if (glm::length(dir) < 1e-4f) dir = glm::vec2(std::cos(glm::radians(e.yaw)), std::sin(glm::radians(e.yaw)));
        dir = glm::normalize(dir);
        const int ax = static_cast<int>(std::floor(e.pos.x + dir.x * (hw + 0.35f)));
        const int az = static_cast<int>(std::floor(e.pos.z + dir.y * (hw + 0.35f)));
        const int fy = static_cast<int>(std::floor(e.pos.y + 0.01f));
        const int h = bodyCells(t.height);
        const bool canJump = t.autoJump && e.onGround && !passable(world, ax, fy, az) &&
                             columnClear(world, ax, fy + 1, az, h) &&
                             columnClear(world, static_cast<int>(std::floor(e.pos.x)), fy + h, static_cast<int>(std::floor(e.pos.z)), 1);
        if (canJump) {
            e.vel.y = 8.2f;
            e.onGround = false;
        } else {
            e.blocked = true;
        }
    }
    if (hitWall && e.walking && e.inWater) e.vel.y = std::max(e.vel.y, 3.0f);   // climb out of water
}

void EntityManager::step(float dt, const World& world, const std::vector<glm::vec3>& players) {
    if (dt <= 0.0f) return;
    dt = std::min(dt, 0.1f);
    FluidRegistry::get().ensureFresh();
    std::vector<Entity*> active;     // moved this step (for pushing each other apart, below)
    for (auto& [id, e] : m_entities) {
        if (e.removed) continue;
        const EntityType* t = type(e.type);
        if (!t) continue;
        e.age += dt;
        if (e.hurtTimer > 0.0f) e.hurtTimer -= dt;
        if (e.pickupDelay > 0.0f) e.pickupDelay -= dt;

        float nearest = 1e9f;
        for (const auto& p : players) nearest = std::min(nearest, glm::distance(p, e.pos));

        // Despawning (mobs that don't stay, and items left lying around for 5 minutes)
        if (t->despawn) {
            if (nearest > 128.0f) { e.removed = true; continue; }
            if (nearest > 32.0f) {
                e.noPlayerTime += dt;
                if (e.noPlayerTime > 30.0f && random01() < dt / 20.0f) { e.removed = true; continue; }
            } else {
                e.noPlayerTime = 0.0f;
            }
        }
        if (t->isItem && e.age > 300.0f) { e.removed = true; continue; }
        if (e.pos.y < -64.0f) { e.removed = true; continue; }      // fell out of the world

        // Frozen when its chunk (or the one under it) isn't loaded, or nobody is near
        const glm::ivec3 cell(glm::floor(e.pos));
        if (nearest > kActiveRange || !world.isLoaded(cell)) continue;

        if (e.repathCooldown > 0.0f) e.repathCooldown -= dt;
        followPath(e, *t, dt, world);
        moveAndCollide(e, *t, dt, world);
        active.push_back(&e);
    }
    pushApart(dt, world, players, active);

    // Dropped items next to each other become one stack
    std::vector<Entity*> items;
    for (auto& [id, e] : m_entities) if (!e.removed && e.type == kItemType && e.age > 0.5f) items.push_back(&e);
    if (items.size() > 1 && items.size() < 400) {
        for (size_t i = 0; i < items.size(); ++i) {
            Entity* a = items[i];
            if (a->removed) continue;
            for (size_t j = i + 1; j < items.size(); ++j) {
                Entity* b = items[j];
                if (b->removed || b->item != a->item || a->itemCount + b->itemCount > 64) continue;
                if (glm::distance(a->pos, b->pos) > 0.8f) continue;
                a->itemCount += b->itemCount;
                a->age = std::min(a->age, b->age);
                a->touch();
                b->removed = true;
            }
        }
    }

    for (auto it = m_entities.begin(); it != m_entities.end();) {
        if (it->second.removed) {
            m_removedIds.push_back(it->first);
            it = m_entities.erase(it);
        } else {
            ++it;
        }
    }
}

// Minecraft's entity pushing: mobs don't walk through each other or through players. Each step,
// any two solid bodies whose boxes overlap are moved apart along the line between their centres
// (sideways only), a good part of the overlap at a time, so crowds shuffle and spread out smoothly
// instead of snapping. The heavier one (bigger box) moves less; players and entities that can't
// be pushed (knockback = false) don't move at all - the other one gets out of the way. The moves
// go through block collision, so nothing is pushed into a wall. Dropped items aren't solid.
void EntityManager::pushApart(float dt, const World& world, const std::vector<glm::vec3>& players,
                              const std::vector<Entity*>& active) {
    struct Body { Entity* e; const EntityType* t; glm::vec3 pos; float hw, h, weight; bool movable; glm::vec2 push{0.0f}; };
    std::vector<Body> bodies;
    bodies.reserve(active.size() + players.size());
    for (Entity* e : active) {
        const EntityType* t = type(e->type);
        if (!t || t->isItem || !t->solid || !t->collides || e->removed) continue;
        bodies.push_back(Body{e, t, e->pos, t->width * 0.5f, t->height, t->width * t->width * t->height, t->pushable});
    }
    if (bodies.empty()) return;
    for (const auto& p : players) bodies.push_back(Body{nullptr, nullptr, p, 0.3f, 1.8f, 1.0f, false});

    // Neighbours through a grid of 2-block cells (no n^2 over every entity)
    auto cellKey = [](int cx, int cz) { return (static_cast<int64_t>(cx) << 32) ^ static_cast<uint32_t>(cz); };
    std::unordered_map<int64_t, std::vector<size_t>> grid;
    for (size_t i = 0; i < bodies.size(); ++i) {
        grid[cellKey(static_cast<int>(std::floor(bodies[i].pos.x / 2.0f)), static_cast<int>(std::floor(bodies[i].pos.z / 2.0f)))].push_back(i);
    }
    const float rate = std::min(1.0f, 12.0f * dt);   // share of the overlap undone this step
    for (size_t i = 0; i < bodies.size(); ++i) {
        Body& a = bodies[i];
        const int cx = static_cast<int>(std::floor(a.pos.x / 2.0f)), cz = static_cast<int>(std::floor(a.pos.z / 2.0f));
        for (int dx = -1; dx <= 1; ++dx) for (int dz = -1; dz <= 1; ++dz) {
            auto it = grid.find(cellKey(cx + dx, cz + dz));
            if (it == grid.end()) continue;
            for (size_t j : it->second) {
                if (j <= i) continue;
                Body& b = bodies[j];
                if (!a.movable && !b.movable) continue;
                // overlapping in height?
                if (a.pos.y >= b.pos.y + b.h || b.pos.y >= a.pos.y + a.h) continue;
                // ...and across (boxes): how far they overlap on x and on z
                const float ox = a.hw + b.hw - std::fabs(b.pos.x - a.pos.x);
                const float oz = a.hw + b.hw - std::fabs(b.pos.z - a.pos.z);
                if (ox <= 0.0f || oz <= 0.0f) continue;
                glm::vec2 n(b.pos.x - a.pos.x, b.pos.z - a.pos.z);
                float len = glm::length(n);
                if (len < 1e-4f) {                        // exactly on top of each other: any way
                    const float ang = random01() * 6.2831853f;
                    n = glm::vec2(std::cos(ang), std::sin(ang));
                } else {
                    n /= len;
                }
                const float overlap = std::min(std::min(ox, oz), 0.6f) * rate;
                float shareA = 0.5f, shareB = 0.5f;
                if (!a.movable) { shareA = 0.0f; shareB = 1.0f; }
                else if (!b.movable) { shareA = 1.0f; shareB = 0.0f; }
                else { shareA = b.weight / (a.weight + b.weight); shareB = 1.0f - shareA; }
                a.push -= n * (overlap * shareA);
                b.push += n * (overlap * shareB);
            }
        }
    }
    for (Body& b : bodies) {
        if (!b.e || !b.movable || (b.push.x == 0.0f && b.push.y == 0.0f)) continue;
        Entity& e = *b.e;
        const glm::vec3 mn0(-b.hw, 0.0f, -b.hw), mx0(b.hw, b.h, b.hw);
        const float px = sweep(world, e.pos + mn0, e.pos + mx0, 0, b.push.x);
        e.pos.x += px;
        const float pz = sweep(world, e.pos + mn0, e.pos + mx0, 2, b.push.y);
        e.pos.z += pz;
        // ...and a little of it as speed, so walking into each other doesn't keep re-overlapping
        if (dt > 0.0f) { e.vel.x += px / dt * 0.15f; e.vel.z += pz / dt * 0.15f; }
    }
}

uint32_t EntityManager::raycast(const glm::vec3& o, const glm::vec3& d, float maxDist, float* dist) const {
    uint32_t best = 0;
    float bestT = maxDist;
    for (const auto& [id, e] : m_entities) {
        if (e.removed) continue;
        const EntityType* t = type(e.type);
        if (!t || t->isItem) continue;       // items can't be hit (walk over them)
        const float hw = t->width * 0.5f + 0.05f;
        const glm::vec3 mn(e.pos.x - hw, e.pos.y, e.pos.z - hw), mx(e.pos.x + hw, e.pos.y + t->height + 0.05f, e.pos.z + hw);
        float t0 = 0.0f, t1 = bestT;
        bool hit = true;
        for (int a = 0; a < 3 && hit; ++a) {
            if (std::fabs(d[a]) < 1e-8f) { if (o[a] < mn[a] || o[a] > mx[a]) hit = false; continue; }
            float ta = (mn[a] - o[a]) / d[a], tb = (mx[a] - o[a]) / d[a];
            if (ta > tb) std::swap(ta, tb);
            t0 = std::max(t0, ta);
            t1 = std::min(t1, tb);
            if (t0 > t1) hit = false;
        }
        if (hit && t0 < bestT) { bestT = t0; best = id; }
    }
    if (dist) *dist = bestT;
    return best;
}

bool EntityManager::standable(const World& world, const EntityType& t, int x, int y, int z) const {
    if (!columnClear(world, x, y, z, bodyCells(t.height))) return false;
    // Swimming: at the surface only (the top water block, with no water above it). The air
    // over the water isn't somewhere to stand - that was mobs walking on lakes.
    if (isWaterAt(world, x, y, z)) return !isWaterAt(world, x, y + 1, z);
    if (!world.isLoaded({x, y - 1, z})) return false;
    const uint8_t below = world.getBlockAt({x, y - 1, z});
    if (below == 0 || dangerousBlock(below) || FluidRegistry::get().forBlock(below)) return false;
    return BlockRegistry::getInstance().getBlock(below).isWalkable;
}

namespace {
struct PathNode { int x, y, z; float g, f; int64_t parent; };
}

std::vector<glm::vec3> EntityManager::findPath(const World& world, const EntityType& t, const glm::vec3& from,
                                               const glm::vec3& to, bool* reached, int maxNodes) const {
    if (reached) *reached = false;
    const int h = bodyCells(t.height);
    auto findStand = [&](const glm::vec3& p, int range) -> glm::ivec3 {
        const glm::ivec3 c(glm::floor(p + glm::vec3(0.0f, 0.01f, 0.0f)));
        if (standable(world, t, c.x, c.y, c.z)) return c;
        for (int d = 1; d <= range; ++d) {
            if (standable(world, t, c.x, c.y - d, c.z)) return {c.x, c.y - d, c.z};
            if (standable(world, t, c.x, c.y + d, c.z)) return {c.x, c.y + d, c.z};
        }
        return glm::ivec3(c.x, -9999, c.z);
    };
    glm::ivec3 start = findStand(from, 2);
    if (start.y == -9999) start = glm::ivec3(glm::floor(from + glm::vec3(0.0f, 0.01f, 0.0f)));   // in the air: from here
    const glm::ivec3 goal = findStand(to, 4);
    const glm::vec3 goalF = goal.y == -9999 ? glm::floor(to) : glm::vec3(goal);

    std::unordered_map<int64_t, PathNode> nodes;
    auto cmp = [](const std::pair<float, int64_t>& a, const std::pair<float, int64_t>& b) { return a.first > b.first; };
    std::priority_queue<std::pair<float, int64_t>, std::vector<std::pair<float, int64_t>>, decltype(cmp)> open(cmp);
    auto heur = [&](int x, int y, int z) { return glm::distance(glm::vec3(x, y, z), goalF); };
    const int64_t startKey = cellKey(start.x, start.y, start.z);
    nodes[startKey] = {start.x, start.y, start.z, 0.0f, heur(start.x, start.y, start.z), -1};
    open.push({nodes[startKey].f, startKey});
    std::unordered_map<int64_t, bool> closed;
    int64_t bestKey = startKey;
    float bestH = heur(start.x, start.y, start.z);
    int expanded = 0;
    bool found = false;

    static const int kDirs[8][2] = {{1,0},{-1,0},{0,1},{0,-1},{1,1},{1,-1},{-1,1},{-1,-1}};
    while (!open.empty() && expanded < maxNodes) {
        const int64_t key = open.top().second;
        open.pop();
        if (closed[key]) continue;
        closed[key] = true;
        ++expanded;
        const PathNode cur = nodes[key];
        const float hcur = heur(cur.x, cur.y, cur.z);
        if (hcur < bestH) { bestH = hcur; bestKey = key; }
        if (goal.y != -9999 && cur.x == goal.x && cur.y == goal.y && cur.z == goal.z) { found = true; bestKey = key; break; }
        const bool inWater = isWaterAt(world, cur.x, cur.y, cur.z);

        for (const auto& d : kDirs) {
            const int nx = cur.x + d[0], nz = cur.z + d[1];
            const bool diag = d[0] != 0 && d[1] != 0;
            // no cutting corners: both sides of a diagonal step must be open for the whole body
            if (diag && (!columnClear(world, cur.x + d[0], cur.y, cur.z, h) || !columnClear(world, cur.x, cur.y, cur.z + d[1], h))) continue;
            const float base = diag ? 1.414f : 1.0f;
            auto consider = [&](int y, float cost) {
                const int64_t k = cellKey(nx, y, nz);
                if (closed.count(k) && closed[k]) return;
                // swimming costs a lot more than walking: land routes win (round a lake, not across)
                const float g = cur.g + cost + (isWaterAt(world, nx, y, nz) ? 4.0f : 0.0f);
                auto it = nodes.find(k);
                if (it != nodes.end() && it->second.g <= g) return;
                nodes[k] = {nx, y, nz, g, g + heur(nx, y, nz), key};
                open.push({g + heur(nx, y, nz), k});
            };
            if (standable(world, t, nx, cur.y, nz)) { consider(cur.y, base); continue; }
            // one block up: a jump, so only straight (not diagonal) and with headroom here
            if (!diag && standable(world, t, nx, cur.y + 1, nz) &&
                columnClear(world, cur.x, cur.y + h, cur.z, 1) && (t.autoJump || t.stepHeight >= 1.0f || inWater)) {
                consider(cur.y + 1, base + 0.8f);
                continue;
            }
            // walking off an edge: the body fits over it, then the first floor within 3 blocks
            if (columnClear(world, nx, cur.y, nz, h)) {
                for (int drop = 1; drop <= 3; ++drop) {
                    if (standable(world, t, nx, cur.y - drop, nz)) { consider(cur.y - drop, base + 0.4f * drop); break; }
                    if (!passable(world, nx, cur.y - drop, nz)) break;
                }
            }
        }
    }
    if (reached) *reached = found;
    if (bestKey == startKey && !found) return {};

    std::vector<glm::vec3> cells;
    for (int64_t k = bestKey; k != -1; k = nodes[k].parent) {
        const PathNode& n = nodes[k];
        cells.push_back(glm::vec3(n.x + 0.5f, static_cast<float>(n.y), n.z + 0.5f));
    }
    std::reverse(cells.begin(), cells.end());

    // Smoothing: skip waypoints when the straight line between two on the same level is
    // walkable for the whole width of the body (no zig-zag along the grid)
    const float hw = t.width * 0.5f;
    auto straightOk = [&](const glm::vec3& a, const glm::vec3& b) {
        if (std::fabs(a.y - b.y) > 0.01f) return false;
        const int y = static_cast<int>(a.y);
        const float len = glm::distance(glm::vec2(a.x, a.z), glm::vec2(b.x, b.z));
        const int steps = std::max(1, static_cast<int>(len / 0.25f));
        for (int i = 0; i <= steps; ++i) {
            const glm::vec3 p = glm::mix(a, b, static_cast<float>(i) / steps);
            for (float ox : {-hw, hw}) for (float oz : {-hw, hw}) {
                if (!standable(world, t, static_cast<int>(std::floor(p.x + ox * 0.95f)), y,
                               static_cast<int>(std::floor(p.z + oz * 0.95f)))) return false;
            }
        }
        return true;
    };
    std::vector<glm::vec3> out;
    size_t i = 0;
    out.push_back(cells[0]);
    while (i + 1 < cells.size()) {
        size_t j = i + 1;
        for (size_t k = cells.size() - 1; k > i + 1; --k) {
            if (k - i > 12) continue;                 // (don't look too far: cheap enough)
            bool sameLevel = true;
            for (size_t m = i; m <= k && sameLevel; ++m) sameLevel = cells[m].y == cells[i].y;
            if (sameLevel && straightOk(cells[i], cells[k])) { j = k; break; }
        }
        out.push_back(cells[j]);
        i = j;
    }
    return out;
}

bool EntityManager::goTo(Entity& e, const World& world, const glm::vec3& goal, float speed) {
    const EntityType* t = type(e.type);
    if (!t) return false;
    e.pathSpeed = speed;
    // The same goal (within a block) while already on the way: keep going. Just got as near as
    // it can (or stuck): don't search again straight away.
    if (glm::distance(goal, e.pathGoal) < 1.0f) {
        if (e.pathing) return true;
        if (e.pathState == "arrived" && glm::distance(goal, e.pos) < 1.2f) return true;   // already there
        if (e.repathCooldown > 0.0f) return e.pathState != "none";
    }
    if (e.pathing && e.repathCooldown > 0.0f) return true;     // (chasing: not every tick)
    bool reached = false;
    std::vector<glm::vec3> path = findPath(world, *t, e.pos, goal, &reached);
    e.repathCooldown = 0.35f;
    if (path.empty()) { stopPath(e); e.walking = false; e.walk = glm::vec2(0.0f); return false; }
    e.path = std::move(path);
    e.pathIndex = 0;
    // the first waypoint is where it stands now: aim for the next
    if (e.path.size() > 1 && glm::distance(glm::vec2(e.path[0].x, e.path[0].z), glm::vec2(e.pos.x, e.pos.z)) < 0.9f) e.pathIndex = 1;
    e.pathGoal = goal;
    e.pathing = true;
    e.pathState = "following";
    e.pathReaches = reached;
    e.stuckTimer = 0.0f;
    e.progressPos = e.pos;
    e.progressIndex = e.pathIndex;
    e.repaths = 0;
    return true;
}

void EntityManager::followPath(Entity& e, const EntityType& t, float dt, const World& world) {
    if (!e.pathing) return;
    // Next waypoint (passing the ones already reached)
    while (e.pathIndex < e.path.size()) {
        const glm::vec3& w = e.path[e.pathIndex];
        const float d = glm::distance(glm::vec2(w.x, w.z), glm::vec2(e.pos.x, e.pos.z));
        // (swimming: only across counts - it bobs up and down in the water)
        if (d < (e.inWater ? 0.5f : 0.3f) && (e.inWater || std::fabs(w.y - e.pos.y) < 1.1f)) ++e.pathIndex;
        else break;
    }
    if (e.pathIndex >= e.path.size()) {
        e.pathing = false;
        e.walking = false;
        e.walk = glm::vec2(0.0f);
        e.pathState = e.pathReaches ? "arrived" : "partial";
        if (!e.pathReaches) e.repathCooldown = 1.0f;
        return;
    }
    const glm::vec3 w = e.path[e.pathIndex];
    glm::vec2 d(w.x - e.pos.x, w.z - e.pos.z);
    const float dist = glm::length(d);
    if (dist > 1e-4f) {
        d /= dist;
        // turn towards it smoothly (fast enough to follow a path round corners)
        const float want = glm::degrees(std::atan2(d.y, d.x));
        float diff = std::fmod(want - e.yaw + 540.0f, 360.0f) - 180.0f;
        const float maxTurn = 720.0f * dt;
        diff = std::clamp(diff, -maxTurn, maxTurn);
        e.yaw = std::fmod(e.yaw + diff + 360.0f, 360.0f);
        // slow down for the last waypoint so it doesn't overshoot
        const bool last = e.pathIndex + 1 == e.path.size();
        const float speed = last ? std::min(e.pathSpeed, std::max(0.6f, dist * 3.0f)) : e.pathSpeed;
        e.walk = d * speed;
        e.walking = true;
    }
    // The next waypoint is a block up: jump when close to it
    if (w.y > e.pos.y + 0.55f && e.onGround && !e.inWater && dist < 1.4f) {
        e.vel.y = 8.2f;
        e.onGround = false;
    }
    if (w.y > e.pos.y + 0.3f && e.inWater) e.vel.y = std::max(e.vel.y, 3.0f);
    // Stuck (pushed, or the world changed): look again, then give up
    e.stuckTimer += dt;
    if (e.stuckTimer > 1.2f) {
        // progress = getting closer to the waypoint (sliding along a wall doesn't count)
        const glm::vec2 wp(w.x, w.z);
        const float before = glm::distance(glm::vec2(e.progressPos.x, e.progressPos.z), wp);
        const bool moved = before - dist > 0.25f || e.pathIndex != e.progressIndex;
        e.stuckTimer = 0.0f;
        e.progressPos = e.pos;
        e.progressIndex = e.pathIndex;
        if (!moved) {
            if (e.repaths < 2) {
                ++e.repaths;
                const int saved = e.repaths;
                bool reached = false;
                std::vector<glm::vec3> path = findPath(world, t, e.pos, e.pathGoal, &reached);
                if (!path.empty()) {
                    e.path = std::move(path);
                    e.pathIndex = e.path.size() > 1 ? 1 : 0;
                    e.pathState = "following";
                    e.pathReaches = reached;
                    e.repaths = saved;
                    return;
                }
            }
            e.pathing = false;
            e.walking = false;
            e.walk = glm::vec2(0.0f);
            e.pathState = "stuck";
            e.repathCooldown = 1.0f;
        }
    }
}

bool EntityManager::findLand(const World& world, const Entity& e, float radius, glm::vec3& out) {
    const EntityType* t = type(e.type);
    if (!t) return false;
    const int x0 = static_cast<int>(std::floor(e.pos.x)), z0 = static_cast<int>(std::floor(e.pos.z));
    const int y0 = static_cast<int>(std::floor(e.pos.y + 0.01f));
    int searches = 0;
    const int maxR = std::clamp(static_cast<int>(radius), 2, 32);
    for (int r = 1; r <= maxR; ++r) {
        // the land cells on this ring, nearest first
        std::vector<std::pair<float, glm::ivec3>> ring;
        for (int dx = -r; dx <= r; ++dx) for (int dz = -r; dz <= r; ++dz) {
            if (std::max(std::abs(dx), std::abs(dz)) != r) continue;
            for (int dy : {1, 0, 2, -1, 3}) {
                const int x = x0 + dx, y = y0 + dy, z = z0 + dz;
                if (isWaterAt(world, x, y, z) || !standable(world, *t, x, y, z)) continue;
                ring.push_back({static_cast<float>(dx * dx + dz * dz), glm::ivec3(x, y, z)});
                break;
            }
        }
        std::sort(ring.begin(), ring.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        for (const auto& [d, c] : ring) {
            if (++searches > 8) return false;
            const glm::vec3 target(c.x + 0.5f, static_cast<float>(c.y), c.z + 0.5f);
            bool reached = false;
            if (!findPath(world, *t, e.pos, target, &reached, 900).empty() && reached) { out = target; return true; }
        }
    }
    return false;
}

bool EntityManager::randomDestination(const World& world, const Entity& e, float radius, glm::vec3& out) {
    const EntityType* t = type(e.type);
    if (!t) return false;
    if (e.inWater && findLand(world, e, std::max(radius, 16.0f), out)) return true;   // swimming: get out first
    for (int attempt = 0; attempt < 10; ++attempt) {
        const float a = random01() * 6.2831853f;
        const float r = 2.0f + random01() * std::max(0.0f, radius - 2.0f);
        const int x = static_cast<int>(std::floor(e.pos.x + std::cos(a) * r));
        const int z = static_cast<int>(std::floor(e.pos.z + std::sin(a) * r));
        const int y0 = static_cast<int>(std::floor(e.pos.y + 0.01f));
        for (int dy : {0, 1, -1, 2, -2, 3, -3}) {
            if (!standable(world, *t, x, y0 + dy, z)) continue;
            if (isWaterAt(world, x, y0 + dy, z)) break;           // land animals don't go for a swim
            bool reached = false;
            const glm::vec3 target(x + 0.5f, static_cast<float>(y0 + dy), z + 0.5f);
            if (!findPath(world, *t, e.pos, target, &reached, 400).empty() && reached) { out = target; return true; }
            break;
        }
    }
    return false;
}

// A ray through the blocks (Amanatides & Woo): anything solid stops it (leaves, glass and
// doors too); plants, water and air don't
bool EntityManager::lineOfSight(const World& world, const glm::vec3& a, const glm::vec3& b) {
    const glm::vec3 d = b - a;
    const float len = glm::length(d);
    if (len < 1e-4f) return true;
    if (len > 128.0f) return false;
    const glm::vec3 dir = d / len;
    glm::ivec3 cell(glm::floor(a));
    const glm::ivec3 end(glm::floor(b));
    const glm::ivec3 step(dir.x > 0 ? 1 : -1, dir.y > 0 ? 1 : -1, dir.z > 0 ? 1 : -1);
    glm::vec3 tMax, tDelta;
    for (int i = 0; i < 3; ++i) {
        if (std::fabs(dir[i]) < 1e-8f) { tMax[i] = 1e30f; tDelta[i] = 1e30f; continue; }
        const float next = dir[i] > 0 ? std::floor(a[i]) + 1.0f : std::floor(a[i]);
        tMax[i] = (next - a[i]) / dir[i];
        tDelta[i] = std::fabs(1.0f / dir[i]);
    }
    for (int n = 0; n < 512; ++n) {
        if (cell == end) return true;
        int axis = tMax.x < tMax.y ? (tMax.x < tMax.z ? 0 : 2) : (tMax.y < tMax.z ? 1 : 2);
        if (tMax[axis] > len) return true;
        cell[axis] += step[axis];
        tMax[axis] += tDelta[axis];
        if (cell.y < 0 || cell.y >= CHUNK_HEIGHT) continue;
        const uint8_t id = world.getBlockAt(cell);
        if (id == 0 || FluidRegistry::get().forBlock(id)) continue;
        if (BlockRegistry::getInstance().getBlock(id).isWalkable) return false;
    }
    return true;
}

std::vector<std::pair<uint32_t, size_t>> EntityManager::itemsInReach(const std::vector<glm::vec3>& players) const {
    std::vector<std::pair<uint32_t, size_t>> out;
    for (const auto& [id, e] : m_entities) {
        if (e.removed || e.type != kItemType || e.pickupDelay > 0.0f) continue;
        for (size_t i = 0; i < players.size(); ++i) {
            const glm::vec3& p = players[i];
            const float dx = e.pos.x - p.x, dz = e.pos.z - p.z;
            if (dx * dx + dz * dz < 1.5f * 1.5f && e.pos.y > p.y - 0.75f && e.pos.y < p.y + 2.0f) {
                out.push_back({id, i});
                break;
            }
        }
    }
    return out;
}

EntityView EntityManager::view(const Entity& e) const {
    EntityView v;
    v.id = e.id;
    v.type = e.type;
    v.pos = e.pos;
    v.yaw = e.yaw;
    v.anim = e.anim;
    v.animSpeed = e.animSpeed;
    v.animLoop = e.animLoop;
    v.nametag = e.nametag;
    v.item = e.item;
    v.itemCount = e.itemCount;
    v.hurtCount = e.hurtCount;
    v.health = e.health;
    v.version = e.version;
    if (const EntityType* t = type(e.type)) {
        v.width = t->width;
        v.height = t->height;
        v.maxHealth = t->maxHealth;
    }
    return v;
}

// entities.txt: a header line, then one entity per line, tab-separated:
//   type  x  y  z  yaw  health  nametag  item  count  saved
bool EntityManager::save(const std::string& worldDir, const std::function<void(Entity&)>& saveData) {
    if (worldDir.empty()) return false;
    const fs::path file = fs::path(worldDir) / "entities.txt";
    const fs::path tmp = fs::path(worldDir) / "entities.txt.tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f << "# BlockSmith entities 1\n";
        char buf[160];
        for (auto& [id, e] : m_entities) {
            if (e.removed) continue;
            const EntityType* t = type(e.type);
            if (t && t->despawn) continue;          // mobs that come and go aren't kept (Minecraft)
            if (saveData && t && !t->isItem) saveData(e);
            std::snprintf(buf, sizeof(buf), "%.3f\t%.3f\t%.3f\t%.1f\t%.2f", e.pos.x, e.pos.y, e.pos.z, e.yaw, e.health);
            f << escapeField(e.type) << '\t' << buf << '\t' << escapeField(e.nametag) << '\t'
              << escapeField(e.item) << '\t' << e.itemCount << '\t' << escapeField(e.saved) << '\n';
        }
        for (const auto& line : m_unknownLines) f << line << '\n';
        if (!f) return false;
    }
    std::error_code ec;
    fs::rename(tmp, file, ec);
    if (ec) {
        fs::remove(file, ec);
        fs::rename(tmp, file, ec);
    }
    return !ec;
}

int EntityManager::load(const std::string& worldDir) {
    clear();
    std::ifstream f(fs::path(worldDir) / "entities.txt", std::ios::binary);
    if (!f) return 0;
    std::string line;
    int n = 0;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        std::vector<std::string> parts;
        size_t start = 0;
        while (true) {
            const size_t tab = line.find('\t', start);
            parts.push_back(line.substr(start, tab == std::string::npos ? std::string::npos : tab - start));
            if (tab == std::string::npos) break;
            start = tab + 1;
        }
        if (parts.size() < 10) continue;
        const std::string typeName = unescapeField(parts[0]);
        if (!type(typeName)) { m_unknownLines.push_back(line); continue; }   // its mod isn't here today
        glm::vec3 pos;
        float yaw = 0.0f, health = 1.0f;
        try {
            pos = glm::vec3(std::stof(parts[1]), std::stof(parts[2]), std::stof(parts[3]));
            yaw = std::stof(parts[4]);
            health = std::stof(parts[5]);
        } catch (...) { continue; }
        const uint32_t id = spawn(typeName, pos);
        Entity* e = get(id);
        if (!e) continue;
        e->yaw = yaw;
        e->health = health;
        e->nametag = unescapeField(parts[6]);
        e->item = unescapeField(parts[7]);
        try { e->itemCount = std::stoi(parts[8]); } catch (...) { e->itemCount = 1; }
        e->saved = unescapeField(parts[9]);
        if (typeName == kItemType && (e->item.empty() || e->itemCount <= 0)) { m_entities.erase(id); continue; }
        ++n;
    }
    return n;
}

std::vector<uint32_t> EntityManager::trySpawning(float dt, const World& world, const std::vector<glm::vec3>& players,
                                                 const std::function<int(int, int, int)>& lightLevel, bool isDay,
                                                 bool enabled, const std::function<bool(int, int, int)>& skyVisible) {
    std::vector<uint32_t> spawned;
    if (!enabled || m_spawnRules.empty() || players.empty()) return spawned;
    m_spawnTimer -= dt;
    if (m_spawnTimer > 0.0f) return spawned;
    m_spawnTimer = 2.0f;
    auto& reg = BlockRegistry::getInstance();

    for (const glm::vec3& player : players) {
        // No more than 40 spawned mobs around any one player
        int around = 0;
        for (const auto& [id, e] : m_entities) {
            const EntityType* t = type(e.type);
            if (t && t->despawn && glm::distance(e.pos, player) < 64.0f) ++around;
        }
        if (around >= 40) continue;

        for (int attempt = 0; attempt < 3; ++attempt) {
            const SpawnRule& rule = m_spawnRules[static_cast<size_t>(random01() * m_spawnRules.size()) % m_spawnRules.size()];
            const EntityType* t = type(rule.type);
            if (!t) continue;
            if ((rule.when == "day" && !isDay) || (rule.when == "night" && isDay)) continue;
            if (random01() > rule.chance) continue;
            // A column 24..48 blocks away
            const float angle = random01() * 6.2831853f;
            const float dist = 24.0f + random01() * 24.0f;
            const int x = static_cast<int>(std::floor(player.x + std::cos(angle) * dist));
            const int z = static_cast<int>(std::floor(player.z + std::sin(angle) * dist));
            const int top = std::min(CHUNK_HEIGHT - 2, static_cast<int>(player.y) + 20);
            const int bottom = std::max(1, static_cast<int>(player.y) - 30);
            if (!world.isLoaded({x, top, z})) continue;
            // Every spot in the column that fits: solid below, room for the mob
            std::vector<int> spots;
            const int needH = std::max(1, static_cast<int>(std::ceil(t->height)));
            for (int y = top; y >= bottom; --y) {
                if (y < rule.minY || y > rule.maxY) continue;
                const uint8_t below = world.getBlockAt({x, y - 1, z});
                if (below == 0 || FluidRegistry::get().forBlock(below)) continue;
                const auto& bp = reg.getBlock(below);
                if (!bp.isWalkable) continue;
                if (!rule.onBlocks.empty() &&
                    std::find(rule.onBlocks.begin(), rule.onBlocks.end(), bp.name) == rule.onBlocks.end()) continue;
                bool room = true;
                for (int k = 0; k < needH && room; ++k) {
                    const uint8_t id = world.getBlockAt({x, y + k, z});
                    if (id != 0 && (FluidRegistry::get().forBlock(id) || reg.getBlock(id).isWalkable)) room = false;
                }
                if (!room) continue;
                const int light = lightLevel(x, y, z);
                if (light < rule.minLight || light > rule.maxLight) continue;
                if (rule.needsSky) {
                    const bool sky = skyVisible ? skyVisible(x, y, z) : light::sky(world.getLightAt({x, y, z})) >= 15;
                    if (!sky) continue;
                }
                spots.push_back(y);
            }
            if (spots.empty()) continue;
            const int y = spots[static_cast<size_t>(random01() * spots.size()) % spots.size()];
            const glm::vec3 at(x + 0.5f, static_cast<float>(y), z + 0.5f);
            bool tooClose = false;
            for (const auto& p : players) if (glm::distance(p, at) < 24.0f) tooClose = true;
            if (tooClose) continue;
            int same = 0;
            for (const auto& [id, e] : m_entities) if (e.type == rule.type && glm::distance(e.pos, at) < 48.0f) ++same;
            if (same >= rule.maxNearby) continue;
            const int group = rule.groupMin + static_cast<int>(random01() * (rule.groupMax - rule.groupMin + 1));
            for (int g = 0; g < std::max(1, group) && same + g < rule.maxNearby; ++g) {
                const glm::vec3 p = at + glm::vec3(g == 0 ? 0.0f : (random01() - 0.5f) * 3.0f, 0.0f,
                                                   g == 0 ? 0.0f : (random01() - 0.5f) * 3.0f);
                const float hw = t->width * 0.5f;
                if (g > 0 && !boxFree(world, glm::vec3(p.x - hw, p.y, p.z - hw), glm::vec3(p.x + hw, p.y + t->height, p.z + hw))) continue;
                const uint32_t id = spawn(rule.type, p);
                if (id) {
                    get(id)->yaw = random01() * 360.0f;
                    spawned.push_back(id);
                }
            }
        }
    }
    return spawned;
}
