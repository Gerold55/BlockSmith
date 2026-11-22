#include "ParticleSystem.h"
#include "Block.h"
#include "ChunkMesh.h"
#include "Light.h"
#include "Types.h"
#include "World.h"
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <cmath>

namespace {
constexpr float kPieceSize = 0.25f;   // block bits show a quarter-width piece of the picture

// Particles stop against anything you can't walk through (plants, water and air let them by)
bool solidAt(const World& world, const glm::vec3& p) {
    const glm::ivec3 c(static_cast<int>(std::floor(p.x)), static_cast<int>(std::floor(p.y)), static_cast<int>(std::floor(p.z)));
    const uint8_t id = world.getBlockAt(c);
    if (id == 0) return false;
    const BlockProperty& b = BlockRegistry::getInstance().getBlock(id);
    return b.isWalkable && b.pattern != "plantlike";
}

glm::vec3 blockTint(uint8_t blockId, float texLayer) {
    if (blockId == 0) return glm::vec3(1.0f);
    const auto& block = BlockRegistry::getInstance().getBlock(blockId);
    for (const auto& f : block.faces) {
        if (static_cast<float>(f.baseLayer) == texLayer && f.tintBase) return block.defaultTint;
    }
    return glm::vec3(1.0f);
}
}

ParticleSystem::ParticleSystem() = default;

ParticleSystem::~ParticleSystem() {
    if (m_vao) glDeleteVertexArrays(1, &m_vao);
    if (m_vbo) glDeleteBuffers(1, &m_vbo);
}

void ParticleSystem::spawn(const ParticleSpec& p) {
    if (m_particles.size() >= kMaxParticles || p.frames.empty() || !(p.life > 0.0f)) return;
    Particle q;
    q.s = p;
    q.s.life = std::min(p.life, 60.0f);
    if (q.s.sizeEnd < 0.0f) q.s.sizeEnd = q.s.size;
    if (q.s.colorEnd.x < 0.0f) q.s.colorEnd = q.s.color;
    q.u0 = random01() * (1.0f - kPieceSize);
    q.v0 = random01() * (1.0f - kPieceSize);
    m_particles.push_back(std::move(q));
}

void ParticleSystem::burst(const ParticleSpec& p, int count, const glm::vec3& spread, const glm::vec3& velSpread) {
    count = std::clamp(count, 0, 512);
    for (int i = 0; i < count; ++i) {
        ParticleSpec q = p;
        q.pos += glm::vec3(randomRange(-spread.x, spread.x), randomRange(-spread.y, spread.y), randomRange(-spread.z, spread.z));
        q.vel += glm::vec3(randomRange(-velSpread.x, velSpread.x), randomRange(-velSpread.y, velSpread.y),
                           randomRange(-velSpread.z, velSpread.z));
        const float k = randomRange(0.75f, 1.25f);
        q.life *= k;
        q.size *= randomRange(0.75f, 1.25f);
        if (q.sizeEnd >= 0.0f) q.sizeEnd *= q.size / std::max(p.size, 1e-4f);
        spawn(q);
    }
}

void ParticleSystem::spawnBreakParticles(const glm::vec3& pos, uint8_t blockId, float texLayer) {
    if (texLayer < 0.0f) return;   // block has no texture
    ParticleSpec s;
    s.frames = { static_cast<int>(texLayer) };
    s.piece = true;
    s.color = blockTint(blockId, texLayer);
    s.acc = glm::vec3(0.0f, -16.0f, 0.0f);
    s.drag = 0.6f;
    // Minecraft: a 4 x 4 x 4 grid of bits, each flying out from the middle of the block
    for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j) for (int k = 0; k < 4; ++k) {
        ParticleSpec q = s;
        const glm::vec3 local((i + 0.5f) / 4.0f, (j + 0.5f) / 4.0f, (k + 0.5f) / 4.0f);
        q.pos = pos + local;
        q.vel = (local - glm::vec3(0.5f)) * randomRange(2.0f, 5.0f) + glm::vec3(0.0f, randomRange(0.5f, 2.5f), 0.0f);
        q.life = randomRange(0.35f, 1.1f);
        q.size = randomRange(0.07f, 0.14f);
        spawn(q);
    }
}

void ParticleSystem::spawnDigParticles(const glm::vec3& blockPos, const glm::vec3& face, uint8_t blockId, float texLayer) {
    if (texLayer < 0.0f) return;
    ParticleSpec s;
    s.frames = { static_cast<int>(texLayer) };
    s.piece = true;
    s.color = blockTint(blockId, texLayer);
    s.acc = glm::vec3(0.0f, -16.0f, 0.0f);
    s.life = randomRange(0.3f, 0.7f);
    s.size = randomRange(0.06f, 0.1f);
    // just outside the face being hit, somewhere on it
    glm::vec3 p = blockPos + glm::vec3(0.5f) + face * 0.56f;
    for (int a = 0; a < 3; ++a) if (std::fabs(face[a]) < 0.5f) p[a] += randomRange(-0.45f, 0.45f);
    s.pos = p;
    s.vel = face * randomRange(0.5f, 1.5f) + glm::vec3(randomRange(-0.6f, 0.6f), randomRange(0.5f, 1.6f), randomRange(-0.6f, 0.6f));
    spawn(s);
}

void ParticleSystem::spawnFootParticles(const glm::vec3& feet, uint8_t blockId, float texLayer, int count, float speed) {
    if (texLayer < 0.0f) return;
    ParticleSpec s;
    s.frames = { static_cast<int>(texLayer) };
    s.piece = true;
    s.color = blockTint(blockId, texLayer);
    s.acc = glm::vec3(0.0f, -14.0f, 0.0f);
    s.pos = feet + glm::vec3(0.0f, 0.05f, 0.0f);
    s.life = 0.6f;
    s.size = 0.08f;
    burst(s, count, glm::vec3(0.3f, 0.0f, 0.3f), glm::vec3(speed, 0.0f, speed));
    // (upwards: a little kick for each)
    for (size_t i = m_particles.size() - std::min(m_particles.size(), static_cast<size_t>(count)); i < m_particles.size(); ++i) {
        m_particles[i].s.vel.y += randomRange(0.8f, 2.0f) * std::max(0.5f, speed);
    }
}

int ParticleSystem::addSpawner(const ParticleSpawner& s) {
    for (size_t i = 0; i < m_spawners.size(); ++i) {
        if (!m_spawners[i].used) { m_spawners[i] = ActiveSpawner{s, 0.0f, 0.0f, true}; return static_cast<int>(i); }
    }
    if (m_spawners.size() >= kMaxSpawners) return -1;
    m_spawners.push_back(ActiveSpawner{s, 0.0f, 0.0f, true});
    return static_cast<int>(m_spawners.size() - 1);
}

void ParticleSystem::removeSpawner(int id) {
    if (id >= 0 && id < static_cast<int>(m_spawners.size())) m_spawners[static_cast<size_t>(id)].used = false;
}

void ParticleSystem::spawnFrom(ActiveSpawner& a, const glm::vec3& player) {
    const ParticleSpawner& s = a.s;
    ParticleSpec p = s.spec;
    const glm::vec3 base = s.followPlayer ? player + s.spec.pos : s.spec.pos;
    p.pos = base + glm::vec3(randomRange(s.minPos.x, s.maxPos.x), randomRange(s.minPos.y, s.maxPos.y), randomRange(s.minPos.z, s.maxPos.z));
    p.vel = s.spec.vel + glm::vec3(randomRange(s.minVel.x, s.maxVel.x), randomRange(s.minVel.y, s.maxVel.y), randomRange(s.minVel.z, s.maxVel.z));
    p.life = randomRange(s.minLife, s.maxLife);
    const float size = randomRange(s.minSize, s.maxSize);
    if (s.spec.sizeEnd >= 0.0f) p.sizeEnd = s.spec.sizeEnd * size / std::max(s.spec.size, 1e-4f);
    p.size = size;
    spawn(p);
}

void ParticleSystem::update(float dt, const World* world, const glm::vec3& player) {
    if (dt <= 0.0f) return;
    dt = std::min(dt, 0.1f);
    // Spawners first
    for (auto& a : m_spawners) {
        if (!a.used) continue;
        a.age += dt;
        a.owed += a.s.rate * dt;
        int n = static_cast<int>(a.owed);
        a.owed -= static_cast<float>(n);
        n = std::min(n, 200);
        for (int i = 0; i < n; ++i) spawnFrom(a, player);
        if (a.s.time > 0.0f && a.age >= a.s.time) a.used = false;
    }

    for (size_t i = 0; i < m_particles.size();) {
        Particle& p = m_particles[i];
        p.age += dt;
        if (p.age >= p.s.life) {
            m_particles[i] = std::move(m_particles.back());
            m_particles.pop_back();
            continue;
        }
        if (!p.resting) {
            p.s.vel += p.s.acc * dt;
            if (p.s.drag > 0.0f) p.s.vel *= std::exp(-p.s.drag * dt);
            const glm::vec3 move = p.s.vel * dt;
            if (p.s.collide && world) {
                // one axis at a time, so a bit sliding along a wall keeps sliding
                for (int a = 0; a < 3; ++a) {
                    glm::vec3 next = p.s.pos;
                    next[a] += move[a];
                    if (solidAt(*world, next)) {
                        if (a == 1 && p.s.vel.y < 0.0f) {
                            if (p.s.bounce && p.s.vel.y < -1.5f) {
                                p.s.vel.y = -p.s.vel.y * 0.45f;
                            } else {
                                p.s.vel = glm::vec3(0.0f);
                                p.resting = true;           // on the ground: stays put
                            }
                        } else {
                            p.s.vel[a] = 0.0f;
                        }
                    } else {
                        p.s.pos[a] = next[a];
                    }
                }
            } else {
                p.s.pos += move;
            }
        }
        ++i;
    }
}

void ParticleSystem::render(const glm::mat4& view, const glm::mat4& proj, GLuint shaderProgram, GLuint textureArrayID,
                            const World* world) {
    if (m_particles.empty()) return;

    glUseProgram(shaderProgram);
    glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "view"), 1, GL_FALSE, glm::value_ptr(view));
    glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "projection"), 1, GL_FALSE, glm::value_ptr(proj));
    // Particles are already in world space. Reset the per-chunk uniforms left over from drawing
    // the world (otherwise particles get the last chunk's offset/fade).
    const glm::mat4 identity(1.0f);
    glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "model"), 1, GL_FALSE, glm::value_ptr(identity));
    glUniform1f(glGetUniformLocation(shaderProgram, "u_ChunkFade"), 1.0f);
    glUniform1i(glGetUniformLocation(shaderProgram, "u_IsGUI"), 0);
    glUniform1i(glGetUniformLocation(shaderProgram, "u_Unlit"), 0);
    glUniform1i(glGetUniformLocation(shaderProgram, "u_VoxelLight"), 1);   // each particle's own light

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D_ARRAY, textureArrayID);
    glUniform1i(glGetUniformLocation(shaderProgram, "textureArray"), 0);

    // Camera axes from the view matrix (same for every particle)
    const glm::vec3 camRight(view[0][0], view[1][0], view[2][0]);
    const glm::vec3 camUp(view[0][1], view[1][1], view[2][1]);

    static std::vector<Vertex> verts;
    verts.clear();
    verts.reserve(m_particles.size() * 6);
    for (const Particle& p : m_particles) {
        const float t = std::clamp(p.age / p.s.life, 0.0f, 1.0f);
        const float hs = 0.5f * (p.s.size + (p.s.sizeEnd - p.s.size) * t);
        if (hs <= 0.0005f) continue;
        const glm::vec3 color = p.s.color + (p.s.colorEnd - p.s.color) * t;
        const size_t frameCount = p.s.frames.size();
        const int layer = p.s.frames[std::min(frameCount - 1, static_cast<size_t>(t * static_cast<float>(frameCount)))];
        if (layer < 0) continue;

        float l[4] = { 0.0f, 1.0f, 1.0f, 1.0f };   // glowing: full block light
        if (!p.s.glow) {
            const uint16_t v = world ? world->getLightAt(glm::ivec3(glm::floor(p.s.pos))) : ChunkData::kFullSky;
            l[0] = light::sky(v) / 15.0f; l[1] = light::red(v) / 15.0f; l[2] = light::green(v) / 15.0f; l[3] = light::blue(v) / 15.0f;
        }
        const float u0 = p.s.piece ? p.u0 : 0.0f, v0 = p.s.piece ? p.v0 : 0.0f;
        const float u1 = p.s.piece ? u0 + kPieceSize : 1.0f, v1 = p.s.piece ? v0 + kPieceSize : 1.0f;

        auto vert = [&](const glm::vec3& at, float u, float vv) {
            Vertex x{};
            x.posX = at.x; x.posY = at.y; x.posZ = at.z;
            x.tintR = color.r; x.tintG = color.g; x.tintB = color.b;
            x.texCoordX = u; x.texCoordY = vv;
            x.baseLayer = static_cast<float>(layer);
            x.overlayLayer = -1.0f;
            x.tintBase = 1.0f;          // the colour multiplies the picture (white = as it is)
            x.tintOverlay = 0.0f;
            x.faceShading = 1.0f;
            x.lightSky = l[0]; x.lightR = l[1]; x.lightG = l[2]; x.lightB = l[3];
            verts.push_back(x);
        };
        const glm::vec3 c0 = p.s.pos + (-camRight - camUp) * hs;   // bottom-left
        const glm::vec3 c1 = p.s.pos + ( camRight - camUp) * hs;   // bottom-right
        const glm::vec3 c2 = p.s.pos + ( camRight + camUp) * hs;   // top-right
        const glm::vec3 c3 = p.s.pos + (-camRight + camUp) * hs;   // top-left
        // (pictures are stored top row first, so v grows downwards)
        vert(c0, u0, v1); vert(c1, u1, v1); vert(c2, u1, v0);
        vert(c0, u0, v1); vert(c2, u1, v0); vert(c3, u0, v0);
    }
    if (verts.empty()) {
        glUniform1i(glGetUniformLocation(shaderProgram, "u_VoxelLight"), 0);
        return;
    }
    ChunkMesh::uploadVertices(m_vao, m_vbo, verts);

    glDisable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glBindVertexArray(m_vao);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(verts.size()));
    glBindVertexArray(0);

    glDepthMask(GL_TRUE);
    glUniform1i(glGetUniformLocation(shaderProgram, "u_VoxelLight"), 0);
}
