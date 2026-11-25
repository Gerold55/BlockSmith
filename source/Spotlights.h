#pragma once
// Spotlights: stage lights for mods (Lua: lights.*). Each has a position, the way it points,
// a colour and dimmer, a beam angle with a soft edge, a range, a strobe and a gobo (a picture
// the light shines through). They light blocks, with shadows traced through the blocks around
// the camera (VoxelVolume.h), and show as beams in the air - brighter in haze - that stop at
// walls. Up to kMaxSpots are drawn at once (the brightest nearest the camera).

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <string>
#include <vector>

class VoxelVolume;

struct SpotLight {
    glm::vec3 position{ 0.0f };
    float pan = 0.0f;          // degrees: 0 = north (-Z), 90 = east (+X)
    float tilt = -90.0f;       // degrees: 0 = level, -90 = straight down, 90 = up
    glm::vec3 color{ 1.0f };   // 0..1 each
    float dimmer = 1.0f;       // 0..1
    float intensity = 1.0f;    // brightness multiplier (> 1 for strong lights)
    float angle = 30.0f;       // the beam's full width, degrees (1..170)
    float softness = 0.25f;    // 0 = a hard edge, 1 = fades from the centre
    float range = 32.0f;       // blocks
    float beam = 1.0f;         // how strongly the beam shows in the air (times the haze)
    float strobe = 0.0f;       // flashes a second (0 = off)
    int gobo = -1;             // texture layer of a gobo picture, or -1
    float goboRotation = 0.0f; // degrees
    float goboSpin = 0.0f;     // degrees a second
    bool shadows = true;
    bool active = false;

    glm::vec3 direction() const;
};

class Spotlights {
public:
    static constexpr int kMaxSpots = 16;

    int add(const SpotLight& s);             // -> id (never reused while the game runs)
    SpotLight* get(int id);
    bool remove(int id);
    void clear();
    std::vector<int> ids() const;

    // Haze in the air: how much beams show (0 = none) and its colour tint
    float haze = 0.35f;
    // How much work the beams do (the lighting quality setting): steps along each view ray, and
    // steps of each step's shadow ray (0 = beams aren't shadowed); tracing off: beams don't stop at blocks
    int beamSamples = 40, beamTraceSteps = 48;
    bool tracing = true;
    glm::vec3 hazeColor{ 1.0f };

    // Sets the world shader's u_Spot* uniforms (the brightest kMaxSpots near the camera)
    void upload(GLuint program, const glm::vec3& camera, float time);
    // Draws the beams (after everything else, added on top)
    void drawBeams(const glm::mat4& viewProj, const glm::vec3& camera, const VoxelVolume& voxels, float time,
                   GLuint textureArray);

private:
    std::vector<SpotLight> m_spots;          // index = id - 1
    std::vector<int> m_drawn;                // ids uploaded this frame (beams use the same)
    GLuint m_beamProgram = 0, m_vao = 0, m_vbo = 0;
    int m_coneVerts = 0;
    bool initBeams();
    float strobeFactor(const SpotLight& s, float time) const;
};

// GLSL shared by the world shader and the beams (needs BLOCKSMITH_VOXEL_TRACE_GLSL and a
// `textureArray` sampler2DArray before it). spotLightAt(p, n, i): the light spot i gives a
// surface at p facing n (n = vec3(0) for a point in the air, as in a beam).
#define BLOCKSMITH_SPOT_GLSL R"(
uniform int  u_NumSpots;
uniform vec3 u_SpotPos[16];
uniform vec3 u_SpotDir[16];
uniform vec3 u_SpotColor[16];   // colour x dimmer x intensity x strobe
uniform vec4 u_SpotCone[16];    // cos(outer), cos(inner), range, shadows (0/1)
uniform vec4 u_SpotGobo[16];    // texture layer (-1 none), rotation (radians), tan(outer), beam strength
vec3 spotLightAt(vec3 p, vec3 n, int i, int traceSteps) {
    vec3 toL = u_SpotPos[i] - p;
    float d = length(toL);
    vec4 cone = u_SpotCone[i];
    if (d > cone.z || d < 0.001) return vec3(0.0);
    vec3 L = toL / d;
    vec3 dir = u_SpotDir[i];
    float cd = dot(-L, dir);
    if (cd <= cone.x) return vec3(0.0);
    float edge = smoothstep(cone.x, max(cone.y, cone.x + 0.0001), cd);
    float fall = 1.0 - d / cone.z;
    fall = fall * fall / (1.0 + d * 0.05);
    float facing = 1.0;
    if (dot(n, n) > 0.0) {
        facing = dot(n, L);
        if (facing <= 0.0) return vec3(0.0);
    }
    float gobo = 1.0;
    vec4 g = u_SpotGobo[i];
    if (g.x >= 0.0) {
        vec3 refUp = abs(dir.y) > 0.95 ? vec3(0.0, 0.0, 1.0) : vec3(0.0, 1.0, 0.0);
        vec3 right = normalize(cross(dir, refUp));
        vec3 up = cross(right, dir);
        vec3 v = -L / cd;                                  // on the plane 1 block ahead of the lamp
        vec2 q = vec2(dot(v, right), dot(v, up)) / max(g.z, 0.0001);
        float c = cos(g.y), s = sin(g.y);
        q = vec2(c * q.x - s * q.y, s * q.x + c * q.y);
        vec4 t = texture(textureArray, vec3(q * 0.5 + 0.5, g.x));
        gobo = dot(t.rgb, vec3(0.333)) * t.a;
    }
    float shadow = 1.0;
    if (cone.w > 0.5) shadow = voxTransmittance(p + n * 0.02, u_SpotPos[i], traceSteps);
    return u_SpotColor[i] * edge * fall * facing * gobo * shadow;
}
)"
