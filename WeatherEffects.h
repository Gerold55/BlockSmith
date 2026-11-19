#pragma once
// WeatherEffects: what weather LOOKS like. It never decides the weather - Lua does that
// (mods/weather): it calls sky.set_precipitation / set_sky_tint / set_light / flash, and
// this draws the result:
//   - precipitation: rain streaks, snowflakes or any picture (ash, leaves, sand), falling
//     around the camera, blown by wind, stopping on the first block below (no rain indoors)
//   - a sky tint drawn over the sky (grey for rain, dark for storms)
//   - a world light level (the block and model shaders multiply sunlight by it)
//   - lightning flashes over the whole screen

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <cstdint>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

class World;

struct PrecipitationSettings {
    std::string type = "none";       // "rain", "snow" or a mod's own name (just a label)
    float intensity = 0.0f;          // 0..1: share of maxParticles falling
    bool  streaks = true;            // true: lines along the fall (rain); false: flakes
    float fallSpeed = 14.0f;         // blocks per second
    float size = 0.03f;              // streak width / flake size, in blocks
    float length = 0.8f;             // streak length, in blocks
    float sway = 0.0f;               // side-to-side drift (snow), blocks per second
    glm::vec2 wind{0.0f};            // x, z drift, blocks per second
    glm::vec4 color{0.62f, 0.68f, 0.8f, 0.5f};
    GLuint texture = 0;              // optional picture (0 = drawn shape)
    float radius = 22.0f;            // area around the camera that gets particles
    float height = 20.0f;            // how far above the camera they start
    int maxParticles = 6000;
};

class WeatherEffects {
public:
    bool init();   // after the OpenGL context exists

    // Presets for a type ("rain", "snow"); other names keep the current shape settings.
    static PrecipitationSettings preset(const std::string& type);
    void setPrecipitation(const PrecipitationSettings& s);
    const PrecipitationSettings& precipitation() const { return m_precip; }

    void setSkyTint(const glm::vec4& rgba) { m_skyTint = rgba; }
    const glm::vec4& skyTint() const { return m_skyTint; }
    void setLight(float level) { m_light = glm::clamp(level, 0.0f, 1.5f); }
    float light() const { return m_light; }
    // Light including any lightning flash: what the shaders use
    float worldLight() const { return m_light + m_flash * 0.9f; }

    void flash(float strength, float seconds);
    // A lightning bolt from `top` (the cloud base) down to `ground`: a jagged bright line with a
    // few branches, flickering for `seconds`. The sky flash is separate (flash()).
    void strike(const glm::vec3& top, const glm::vec3& ground, float seconds = 0.45f);
    // The bolts, after the world (additive glow; hills in front hide them)
    void drawBolts(const glm::mat4& view, const glm::mat4& proj, const glm::vec3& camera);

    // Each frame. `daylight` 0..1 darkens precipitation at night.
    void update(float dt, const glm::vec3& camera, const World& world);
    void drawSkyTint();                                        // right after the sky
    void drawPrecipitation(const glm::mat4& view, const glm::mat4& proj,
                           const glm::vec3& camera, float daylight);
    void drawFlash();                                          // after the world, before the HUD

private:
    struct Particle { glm::vec3 pos; float phase; };
    struct Bolt { std::vector<glm::vec3> points; std::vector<std::pair<int, int>> segments; std::vector<float> width;
                  float age = 0.0f, life = 0.45f; };
    std::vector<Bolt> m_bolts;
    void respawn(Particle& p, const glm::vec3& camera, bool anywhereInHeight);
    int surfaceY(int x, int z, const glm::vec3& camera, const World& world);   // first block below the sky

    PrecipitationSettings m_precip;
    std::vector<Particle> m_particles;
    std::unordered_map<int64_t, int> m_surface;   // column -> y above its highest block
    float m_surfaceAge = 0.0f;
    float m_time = 0.0f;
    std::mt19937 m_rng{12345};

    glm::vec4 m_skyTint{0.0f};
    float m_light = 1.0f;
    float m_flash = 0.0f, m_flashDecay = 0.0f;

    GLuint m_particleProgram = 0, m_overlayProgram = 0;
    GLuint m_vao = 0, m_vbo = 0, m_emptyVao = 0;
    std::vector<float> m_verts;
};
