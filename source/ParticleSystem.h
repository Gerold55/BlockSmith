#ifndef PARTICLESYSTEM_H
#define PARTICLESYSTEM_H
// Particles, Minecraft style: little pictures that fly, fall, drift, shrink or animate and
// vanish - block bits when digging, dust, smoke and flames, splashes and bubbles, sparks when a
// mob is hit, the "poof" when one dies, and anything mods make (Lua: particles.spawn /
// particles.add_spawner / particles.register_ambient, in main.cpp).
//
// They're drawn with the world's own shader and pictures (the block texture array), lit by the
// light where each one is (block light, sunlight, coloured light) unless they glow, and get the
// world's fog and underwater tint. They only exist in the game that draws them - nothing is sent
// over the network; on a server every player's game makes its own.

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <cstdint>
#include <random>
#include <vector>

class World;

// One kind of particle: what a spawn, or each particle of a spawner, looks like
struct ParticleSpec {
    glm::vec3 pos{0.0f};
    glm::vec3 vel{0.0f};                 // blocks per second
    glm::vec3 acc{0.0f, -12.0f, 0.0f};   // blocks per second, per second (gravity)
    float drag = 0.0f;                   // velocity lost per second (air: 0..5)
    float life = 1.0f;                   // seconds
    float size = 0.1f;                   // blocks across
    float sizeEnd = -1.0f;               // at the end of its life (< 0: same as size)
    glm::vec3 color{1.0f};               // tint
    glm::vec3 colorEnd{-1.0f};           // at the end (x < 0: same as color)
    std::vector<int> frames;             // texture layers, played over its life (1 = still)
    bool piece = false;                  // a random quarter of the picture (block bits)
    bool glow = false;                   // full bright, whatever the light (flames, sparks)
    bool collide = true;                 // stops on blocks (false: passes through)
    bool bounce = false;                 // bounces off the ground instead of stopping
};

// Keeps making particles: `rate` a second for `time` seconds (0 = until removed), each one
// `spec` with its position, velocity, life and size picked between the min and max
struct ParticleSpawner {
    ParticleSpec spec;
    glm::vec3 minPos{0.0f}, maxPos{0.0f};    // around spec.pos (or the player, if follow)
    glm::vec3 minVel{0.0f}, maxVel{0.0f};    // added to spec.vel
    float minLife = 1.0f, maxLife = 1.0f;
    float minSize = 0.1f, maxSize = 0.1f;
    float rate = 10.0f;
    float time = 0.0f;
    bool followPlayer = false;
};

class ParticleSystem {
public:
    static constexpr size_t kMaxParticles = 6000;
    static constexpr size_t kMaxSpawners = 256;

    ParticleSystem();
    ~ParticleSystem();

    void spawn(const ParticleSpec& p);
    // `count` of them, each spread around (a random point in pos +- spread) with a random
    // extra velocity in +- velSpread, life and size varied by +- 25%
    void burst(const ParticleSpec& p, int count, const glm::vec3& spread, const glm::vec3& velSpread);

    // Minecraft's block-breaking bits: a 4x4x4 grid of pieces of the block's picture
    void spawnBreakParticles(const glm::vec3& pos, uint8_t blockId, float texLayer);
    // A few bits off the face being dug (`face` = the outward normal)
    void spawnDigParticles(const glm::vec3& blockPos, const glm::vec3& face, uint8_t blockId, float texLayer);
    // Bits of the block under someone's feet (sprinting, landing): `count`, kicked up at `speed`
    void spawnFootParticles(const glm::vec3& feet, uint8_t blockId, float texLayer, int count, float speed);

    int addSpawner(const ParticleSpawner& s);    // id, or -1 if there are too many
    void removeSpawner(int id);

    void update(float dt, const World* world, const glm::vec3& player);
    void render(const glm::mat4& view, const glm::mat4& proj, GLuint shaderProgram, GLuint textureArrayID,
                const World* world);
    size_t count() const { return m_particles.size(); }

    float random01() { return std::uniform_real_distribution<float>(0.0f, 1.0f)(m_rng); }
    float randomRange(float a, float b) { return a + (b - a) * random01(); }

private:
    struct Particle {
        ParticleSpec s;
        float age = 0.0f;
        float u0 = 0.0f, v0 = 0.0f;      // which piece of the picture (piece = true)
        bool resting = false;            // lying on the ground
    };
    struct ActiveSpawner {
        ParticleSpawner s;
        float age = 0.0f;
        float owed = 0.0f;               // particles due but not made yet (fractions)
        bool used = false;
    };
    void spawnFrom(ActiveSpawner& a, const glm::vec3& player);

    GLuint m_vao = 0, m_vbo = 0;
    std::vector<Particle> m_particles;
    std::vector<ActiveSpawner> m_spawners;
    std::mt19937 m_rng{0xC0FFEEu};
};

#endif // PARTICLESYSTEM_H
