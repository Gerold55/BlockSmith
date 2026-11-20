#include "WeatherEffects.h"
#include "World.h"
#include "Types.h"
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <cmath>
#include <iostream>

namespace {

const char* kParticleVS = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec2 aUV;
uniform mat4 u_ViewProj;
out vec2 vUV;
void main() {
    vUV = aUV;
    gl_Position = u_ViewProj * vec4(aPos, 1.0);
}
)";

const char* kParticleFS = R"(
#version 330 core
in vec2 vUV;
out vec4 FragColor;
uniform vec4 u_Color;
uniform int u_HasTexture;
uniform int u_Streaks;
uniform float u_Daylight;
uniform sampler2D u_Texture;
void main() {
    vec4 c = u_Color;
    if (u_HasTexture == 1) {
        c *= texture(u_Texture, vUV);
    } else if (u_Streaks == 2) {
        // a lightning bolt: a white-hot core with a soft blue glow, full strength day or night
        float side = 1.0 - abs(vUV.x * 2.0 - 1.0);
        c.a *= side * side;
        FragColor = vec4(mix(c.rgb, vec3(1.0), side * side * side), c.a);
        return;
    } else if (u_Streaks == 1) {
        // a thin line, soft at the sides and ends
        float side = 1.0 - abs(vUV.x * 2.0 - 1.0);
        float ends = smoothstep(0.0, 0.25, vUV.y) * smoothstep(1.0, 0.6, vUV.y);
        c.a *= side * ends;
    } else {
        // a soft round flake
        float d = length(vUV - 0.5) * 2.0;
        c.a *= smoothstep(1.0, 0.45, d);
    }
    if (c.a < 0.01) discard;
    FragColor = vec4(c.rgb * mix(0.3, 1.0, u_Daylight), c.a);
}
)";

// A triangle covering the whole screen, from gl_VertexID (no vertex buffer needed)
const char* kOverlayVS = R"(
#version 330 core
void main() {
    vec2 p = vec2((gl_VertexID == 1) ? 3.0 : -1.0, (gl_VertexID == 2) ? 3.0 : -1.0);
    gl_Position = vec4(p, 0.0, 1.0);
}
)";

const char* kOverlayFS = R"(
#version 330 core
out vec4 FragColor;
uniform vec4 u_Color;
void main() { FragColor = u_Color; }
)";

GLuint compile(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        std::cerr << "[Weather] Shader error: " << log << std::endl;
    }
    return s;
}

GLuint link(const char* vs, const char* fs) {
    GLuint p = glCreateProgram();
    GLuint v = compile(GL_VERTEX_SHADER, vs), f = compile(GL_FRAGMENT_SHADER, fs);
    glAttachShader(p, v);
    glAttachShader(p, f);
    glLinkProgram(p);
    glDeleteShader(v);
    glDeleteShader(f);
    return p;
}

int64_t columnKey(int x, int z) { return (static_cast<int64_t>(x) << 32) ^ static_cast<uint32_t>(z); }

} // namespace

bool WeatherEffects::init() {
    m_particleProgram = link(kParticleVS, kParticleFS);
    m_overlayProgram = link(kOverlayVS, kOverlayFS);
    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);
    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), reinterpret_cast<void*>(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glGenVertexArrays(1, &m_emptyVao);
    glBindVertexArray(0);
    return m_particleProgram != 0 && m_overlayProgram != 0;
}

PrecipitationSettings WeatherEffects::preset(const std::string& type) {
    PrecipitationSettings s;
    s.type = type;
    if (type == "snow") {
        s.streaks = false;
        s.fallSpeed = 2.2f;
        s.size = 0.09f;
        s.sway = 0.6f;
        s.color = glm::vec4(1.0f, 1.0f, 1.0f, 0.85f);
        s.radius = 18.0f;
        s.height = 14.0f;
    } else if (type == "none") {
        s.intensity = 0.0f;
    }
    // "rain" (and anything unknown) uses the defaults in PrecipitationSettings
    return s;
}

void WeatherEffects::setPrecipitation(const PrecipitationSettings& s) {
    m_precip = s;
    m_precip.intensity = glm::clamp(m_precip.intensity, 0.0f, 1.0f);
    m_precip.maxParticles = std::clamp(m_precip.maxParticles, 0, 20000);
    m_precip.radius = std::clamp(m_precip.radius, 4.0f, 64.0f);
    m_precip.height = std::clamp(m_precip.height, 4.0f, 64.0f);
}

void WeatherEffects::flash(float strength, float seconds) {
    m_flash = std::max(m_flash, glm::clamp(strength, 0.0f, 1.0f));
    m_flashDecay = m_flash / std::max(seconds, 0.05f);
}

int WeatherEffects::surfaceY(int x, int z, const glm::vec3& camera, const World& world) {
    const int64_t key = columnKey(x, z);
    const auto it = m_surface.find(key);
    if (it != m_surface.end()) return it->second;
    const int top = std::min(CHUNK_HEIGHT - 1, static_cast<int>(camera.y + m_precip.height) + 2);
    const int bottom = std::max(0, static_cast<int>(camera.y) - 48);
    int y = -1000;   // nothing below: falls out of view
    for (int by = top; by >= bottom; --by) {
        if (world.getBlockAt(glm::ivec3(x, by, z)) != 0) { y = by + 1; break; }
    }
    m_surface[key] = y;
    return y;
}

void WeatherEffects::respawn(Particle& p, const glm::vec3& camera, bool anywhereInHeight) {
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    const float angle = unit(m_rng) * 6.2831853f;
    const float dist = std::sqrt(unit(m_rng)) * m_precip.radius;
    // A little upwind, so wind doesn't leave the area around the camera empty
    const glm::vec2 lead = m_precip.wind * (m_precip.height / std::max(m_precip.fallSpeed, 0.5f)) * 0.5f;
    p.pos.x = camera.x + std::cos(angle) * dist - lead.x;
    p.pos.z = camera.z + std::sin(angle) * dist - lead.y;
    p.pos.y = anywhereInHeight ? camera.y - 6.0f + unit(m_rng) * (m_precip.height + 6.0f)
                               : camera.y + m_precip.height * (0.6f + 0.4f * unit(m_rng));
    p.phase = unit(m_rng) * 6.2831853f;
}

void WeatherEffects::update(float dt, const glm::vec3& camera, const World& world) {
    m_time += dt;
    if (m_flash > 0.0f) m_flash = std::max(0.0f, m_flash - m_flashDecay * dt);
    for (auto& b : m_bolts) b.age += dt;
    m_bolts.erase(std::remove_if(m_bolts.begin(), m_bolts.end(), [](const Bolt& b) { return b.age >= b.life; }),
                  m_bolts.end());

    // Columns' surfaces are re-read twice a second (blocks change, the camera moves)
    m_surfaceAge += dt;
    if (m_surfaceAge > 0.5f) { m_surface.clear(); m_surfaceAge = 0.0f; }

    const size_t want = static_cast<size_t>(m_precip.intensity * static_cast<float>(m_precip.maxParticles));
    if (m_particles.size() > want) m_particles.resize(want);
    while (m_particles.size() < want) {
        Particle p{};
        respawn(p, camera, true);
        m_particles.push_back(p);
    }

    const float r2 = m_precip.radius * m_precip.radius * 1.3f;
    for (auto& p : m_particles) {
        p.pos.y -= m_precip.fallSpeed * dt;
        p.pos.x += (m_precip.wind.x + std::sin(m_time * 1.3f + p.phase) * m_precip.sway) * dt;
        p.pos.z += (m_precip.wind.y + std::cos(m_time * 1.1f + p.phase) * m_precip.sway) * dt;
        const float dx = p.pos.x - camera.x, dz = p.pos.z - camera.z;
        const int bx = static_cast<int>(std::floor(p.pos.x)), bz = static_cast<int>(std::floor(p.pos.z));
        if (p.pos.y < camera.y - 24.0f || dx * dx + dz * dz > r2 ||
            p.pos.y < static_cast<float>(surfaceY(bx, bz, camera, world))) {
            respawn(p, camera, false);
        }
    }
}

void WeatherEffects::drawPrecipitation(const glm::mat4& view, const glm::mat4& proj,
                                       const glm::vec3& camera, float daylight) {
    if (m_particles.empty() || m_particleProgram == 0) return;

    const glm::vec3 camRight(view[0][0], view[1][0], view[2][0]);
    const glm::vec3 camUp(view[0][1], view[1][1], view[2][1]);
    const glm::vec3 fall = glm::normalize(glm::vec3(m_precip.wind.x, -m_precip.fallSpeed, m_precip.wind.y) + glm::vec3(0, -0.001f, 0));

    m_verts.clear();
    m_verts.reserve(m_particles.size() * 30);
    auto vtx = [this](const glm::vec3& p, float u, float v) {
        m_verts.insert(m_verts.end(), { p.x, p.y, p.z, u, v });
    };
    for (const auto& p : m_particles) {
        glm::vec3 a, b;   // half-extents: a across, b along
        if (m_precip.streaks) {
            const glm::vec3 toCam = glm::normalize(camera - p.pos + glm::vec3(0.0001f));
            glm::vec3 side = glm::cross(fall, toCam);
            const float len = glm::length(side);
            side = len > 0.0001f ? side / len : camRight;
            a = side * (m_precip.size * 0.5f);
            b = fall * (m_precip.length * 0.5f);
        } else {
            a = camRight * (m_precip.size * 0.5f);
            b = -camUp * (m_precip.size * 0.5f);
        }
        const glm::vec3 p00 = p.pos - a - b, p10 = p.pos + a - b, p11 = p.pos + a + b, p01 = p.pos - a + b;
        vtx(p00, 0, 0); vtx(p10, 1, 0); vtx(p11, 1, 1);
        vtx(p00, 0, 0); vtx(p11, 1, 1); vtx(p01, 0, 1);
    }

    glUseProgram(m_particleProgram);
    const glm::mat4 vp = proj * view;
    glUniformMatrix4fv(glGetUniformLocation(m_particleProgram, "u_ViewProj"), 1, GL_FALSE, glm::value_ptr(vp));
    glUniform4fv(glGetUniformLocation(m_particleProgram, "u_Color"), 1, glm::value_ptr(m_precip.color));
    glUniform1i(glGetUniformLocation(m_particleProgram, "u_HasTexture"), m_precip.texture != 0 ? 1 : 0);
    glUniform1i(glGetUniformLocation(m_particleProgram, "u_Streaks"), m_precip.streaks ? 1 : 0);
    glUniform1f(glGetUniformLocation(m_particleProgram, "u_Daylight"), glm::clamp(daylight, 0.0f, 1.0f));
    if (m_precip.texture != 0) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_precip.texture);
        glUniform1i(glGetUniformLocation(m_particleProgram, "u_Texture"), 0);
    }

    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_CULL_FACE);

    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(m_verts.size() * sizeof(float)), m_verts.data(), GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(m_verts.size() / 5));
    glBindVertexArray(0);
    glDepthMask(GL_TRUE);
}

void WeatherEffects::drawSkyTint() {
    if (m_skyTint.a <= 0.001f || m_overlayProgram == 0) return;
    glUseProgram(m_overlayProgram);
    glUniform4fv(glGetUniformLocation(m_overlayProgram, "u_Color"), 1, glm::value_ptr(m_skyTint));
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glBindVertexArray(m_emptyVao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
}

void WeatherEffects::strike(const glm::vec3& top, const glm::vec3& ground, float seconds) {
    if (top.y <= ground.y + 1.0f) return;
    Bolt b;
    b.life = glm::clamp(seconds, 0.1f, 2.0f);
    std::uniform_real_distribution<float> u(-1.0f, 1.0f);
    // The main channel: zig-zags down, drifting sideways a little at each kink
    const int steps = glm::clamp(static_cast<int>((top.y - ground.y) / 4.0f), 8, 60);
    glm::vec3 p = top;
    b.points.push_back(p);
    std::vector<int> mainIdx{0};
    for (int i = 1; i <= steps; ++i) {
        const float t = static_cast<float>(i) / steps;
        glm::vec3 next = glm::mix(top, ground, t);
        if (i < steps) next += glm::vec3(u(m_rng) * 2.2f, u(m_rng) * 0.6f, u(m_rng) * 2.2f);
        b.points.push_back(next);
        const int idx = static_cast<int>(b.points.size()) - 1;
        b.segments.push_back({mainIdx.back(), idx});
        b.width.push_back(0.45f);
        mainIdx.push_back(idx);
        p = next;
    }
    // Branches from the upper two thirds, thinner, fading out sideways and down
    const int branches = 2 + static_cast<int>((u(m_rng) + 1.0f) * 1.5f);
    for (int k = 0; k < branches; ++k) {
        const int from = mainIdx[1 + static_cast<size_t>((u(m_rng) + 1.0f) * 0.5f * (steps * 2 / 3))];
        glm::vec3 q = b.points[static_cast<size_t>(from)];
        const glm::vec3 dir = glm::normalize(glm::vec3(u(m_rng), -1.2f, u(m_rng)));
        int last = from;
        const int len = 3 + static_cast<int>((u(m_rng) + 1.0f) * 2.0f);
        for (int i = 0; i < len; ++i) {
            q += dir * 3.5f + glm::vec3(u(m_rng) * 1.5f, 0.0f, u(m_rng) * 1.5f);
            if (q.y <= ground.y) break;
            b.points.push_back(q);
            const int idx = static_cast<int>(b.points.size()) - 1;
            b.segments.push_back({last, idx});
            b.width.push_back(0.22f * (1.0f - static_cast<float>(i) / len) + 0.06f);
            last = idx;
        }
    }
    m_bolts.push_back(std::move(b));
    if (m_bolts.size() > 8) m_bolts.erase(m_bolts.begin());
}

void WeatherEffects::drawBolts(const glm::mat4& view, const glm::mat4& proj, const glm::vec3& camera) {
    if (m_bolts.empty() || m_particleProgram == 0) return;
    m_verts.clear();
    auto vtx = [this](const glm::vec3& p, float u, float v) { m_verts.insert(m_verts.end(), { p.x, p.y, p.z, u, v }); };
    float brightest = 0.0f;
    for (const auto& b : m_bolts) {
        // Flickers: bright, dimmer, bright again, then fades out
        const float t = b.age / b.life;
        const float flicker = (t < 0.15f) ? 1.0f : (t < 0.3f) ? 0.45f : (t < 0.5f) ? 0.9f : (1.0f - t) * 1.6f;
        brightest = std::max(brightest, flicker);
        for (size_t i = 0; i < b.segments.size(); ++i) {
            const glm::vec3 a = b.points[static_cast<size_t>(b.segments[i].first)];
            const glm::vec3 c = b.points[static_cast<size_t>(b.segments[i].second)];
            const glm::vec3 along = c - a;
            glm::vec3 side = glm::cross(along, camera - (a + c) * 0.5f);
            const float len = glm::length(side);
            if (len < 1e-4f) continue;
            // a glow several times wider than the white core (the shader shapes it)
            side = side / len * (b.width[i] * 3.0f * glm::clamp(flicker, 0.3f, 1.0f));
            vtx(a - side, 0, 0); vtx(a + side, 1, 0); vtx(c + side, 1, 1);
            vtx(a - side, 0, 0); vtx(c + side, 1, 1); vtx(c - side, 0, 1);
        }
    }
    if (m_verts.empty()) return;
    glUseProgram(m_particleProgram);
    const glm::mat4 vp = proj * view;
    glUniformMatrix4fv(glGetUniformLocation(m_particleProgram, "u_ViewProj"), 1, GL_FALSE, glm::value_ptr(vp));
    const glm::vec4 color(0.62f, 0.72f, 1.0f, glm::clamp(brightest, 0.0f, 1.0f));
    glUniform4fv(glGetUniformLocation(m_particleProgram, "u_Color"), 1, glm::value_ptr(color));
    glUniform1i(glGetUniformLocation(m_particleProgram, "u_HasTexture"), 0);
    glUniform1i(glGetUniformLocation(m_particleProgram, "u_Streaks"), 2);
    glUniform1f(glGetUniformLocation(m_particleProgram, "u_Daylight"), 1.0f);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);         // light adds up
    glDisable(GL_CULL_FACE);
    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(m_verts.size() * sizeof(float)), m_verts.data(), GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(m_verts.size() / 5));
    glBindVertexArray(0);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_TRUE);
}

void WeatherEffects::drawFlash() {
    if (m_flash <= 0.001f || m_overlayProgram == 0) return;
    glUseProgram(m_overlayProgram);
    const glm::vec4 c(0.85f, 0.9f, 1.0f, m_flash * 0.55f);
    glUniform4fv(glGetUniformLocation(m_overlayProgram, "u_Color"), 1, glm::value_ptr(c));
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);   // adds light
    glBindVertexArray(m_emptyVao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_DEPTH_TEST);
}
