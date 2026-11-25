#include "Spotlights.h"
#include "VoxelVolume.h"
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <cmath>
#include <iostream>

glm::vec3 SpotLight::direction() const {
    const float p = glm::radians(pan), t = glm::radians(tilt);
    return glm::normalize(glm::vec3(std::sin(p) * std::cos(t), std::sin(t), -std::cos(p) * std::cos(t)));
}

int Spotlights::add(const SpotLight& s) {
    m_spots.push_back(s);
    m_spots.back().active = true;
    return static_cast<int>(m_spots.size());
}
SpotLight* Spotlights::get(int id) {
    if (id < 1 || id > static_cast<int>(m_spots.size()) || !m_spots[id - 1].active) return nullptr;
    return &m_spots[id - 1];
}
bool Spotlights::remove(int id) {
    SpotLight* s = get(id);
    if (!s) return false;
    s->active = false;
    return true;
}
void Spotlights::clear() {
    m_spots.clear();
    m_drawn.clear();
    haze = 0.35f;
    hazeColor = glm::vec3(1.0f);
}
std::vector<int> Spotlights::ids() const {
    std::vector<int> out;
    for (size_t i = 0; i < m_spots.size(); ++i) if (m_spots[i].active) out.push_back(static_cast<int>(i + 1));
    return out;
}

float Spotlights::strobeFactor(const SpotLight& s, float time) const {
    if (s.strobe <= 0.0f) return 1.0f;
    const float phase = time * s.strobe - std::floor(time * s.strobe);
    return phase < 0.25f ? 1.0f : 0.0f;     // short flashes
}

void Spotlights::upload(GLuint program, const glm::vec3& camera, float time) {
    // The brightest near the camera, up to kMaxSpots
    struct Pick { int id; float score; };
    std::vector<Pick> picks;
    for (size_t i = 0; i < m_spots.size(); ++i) {
        const SpotLight& s = m_spots[i];
        if (!s.active || s.dimmer <= 0.0f || s.intensity <= 0.0f || s.range <= 0.0f) continue;
        const float d = glm::distance(camera, s.position);
        if (d > s.range + 96.0f) continue;
        picks.push_back({ static_cast<int>(i + 1), s.dimmer * s.intensity * s.range / (1.0f + d) });
    }
    std::sort(picks.begin(), picks.end(), [](const Pick& a, const Pick& b) { return a.score > b.score; });
    if (picks.size() > static_cast<size_t>(kMaxSpots)) picks.resize(kMaxSpots);
    m_drawn.clear();

    glm::vec3 pos[kMaxSpots], dir[kMaxSpots], col[kMaxSpots];
    glm::vec4 cone[kMaxSpots], gobo[kMaxSpots];
    int n = 0;
    for (const Pick& p : picks) {
        const SpotLight& s = m_spots[p.id - 1];
        const float half = glm::radians(std::clamp(s.angle, 1.0f, 170.0f) * 0.5f);
        const float inner = half * (1.0f - std::clamp(s.softness, 0.0f, 1.0f));
        pos[n] = s.position;
        dir[n] = s.direction();
        col[n] = s.color * std::clamp(s.dimmer, 0.0f, 1.0f) * s.intensity * strobeFactor(s, time);
        cone[n] = glm::vec4(std::cos(half), std::cos(inner), s.range, s.shadows ? 1.0f : 0.0f);
        gobo[n] = glm::vec4(static_cast<float>(s.gobo), glm::radians(s.goboRotation + s.goboSpin * time),
                            std::tan(half), s.beam);
        m_drawn.push_back(p.id);
        ++n;
    }
    glUniform1i(glGetUniformLocation(program, "u_NumSpots"), n);
    if (n == 0) return;
    glUniform3fv(glGetUniformLocation(program, "u_SpotPos"), n, glm::value_ptr(pos[0]));
    glUniform3fv(glGetUniformLocation(program, "u_SpotDir"), n, glm::value_ptr(dir[0]));
    glUniform3fv(glGetUniformLocation(program, "u_SpotColor"), n, glm::value_ptr(col[0]));
    glUniform4fv(glGetUniformLocation(program, "u_SpotCone"), n, glm::value_ptr(cone[0]));
    glUniform4fv(glGetUniformLocation(program, "u_SpotGobo"), n, glm::value_ptr(gobo[0]));
}

// ---------------------------------------------------------------------------
// Beams: each drawn spot's cone, its far side; the fragment marches back along the view ray
// through the cone, adding the light scattered by the haze at each step (shadowed, stopped by
// blocks in the way)
// ---------------------------------------------------------------------------
namespace {
const char* kBeamVS = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
uniform mat4 u_Model;
uniform mat4 u_ViewProj;
out vec3 WorldPos;
void main() {
    vec4 w = u_Model * vec4(aPos, 1.0);
    WorldPos = w.xyz;
    gl_Position = u_ViewProj * w;
}
)";
const char* kBeamFSHead = R"(
#version 330 core
in vec3 WorldPos;
out vec4 FragColor;
uniform sampler2DArray textureArray;
uniform vec3 u_CameraPos;
uniform int u_Spot;            // which of the uploaded spots
uniform float u_Haze;
uniform vec3 u_HazeColor;
uniform int u_BeamSamples;     // steps along the view ray
uniform int u_BeamTrace;       // steps of each step's shadow ray (0 = no shadows in the beam)
)";
const char* kBeamFSMain = R"(
void main() {
    if (gl_FrontFacing) discard;              // the far side only: one march per pixel
    vec3 ray = WorldPos - u_CameraPos;
    float far = length(ray);
    if (far < 0.001) discard;
    ray /= far;
    int kSteps = max(u_BeamSamples, 4);
    float stepLen = far / float(kSteps);
    // a little noise in where the steps fall hides the banding
    float jitter = fract(sin(dot(gl_FragCoord.xy, vec2(12.9898, 78.233))) * 43758.5453);
    vec3 acc = vec3(0.0);
    for (int k = 0; k < kSteps; ++k) {
        vec3 p = u_CameraPos + ray * ((float(k) + jitter) * stepLen);
        if (voxOpacity(ivec3(floor(p))) > 0.9) break;   // a block in the way: nothing past it shows
        acc += spotLightAt(p, vec3(0.0), u_Spot, u_BeamTrace);
    }
    float strength = u_Haze * u_SpotGobo[u_Spot].w * 0.06;
    vec3 c = acc * stepLen * strength * u_HazeColor;
    if (max(c.r, max(c.g, c.b)) < 0.002) discard;
    FragColor = vec4(c, 1.0);
}
)";

GLuint compileShader(GLenum type, const std::string& src) {
    GLuint s = glCreateShader(type);
    const char* p = src.c_str();
    glShaderSource(s, 1, &p, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        std::cerr << "[Spotlights] beam shader: " << log << std::endl;
        glDeleteShader(s);
        return 0;
    }
    return s;
}
} // namespace

bool Spotlights::initBeams() {
    if (m_beamProgram) return true;
    const GLuint vs = compileShader(GL_VERTEX_SHADER, kBeamVS);
    const GLuint fs = compileShader(GL_FRAGMENT_SHADER, std::string(kBeamFSHead) + BLOCKSMITH_VOXEL_TRACE_GLSL +
                                                         BLOCKSMITH_SPOT_GLSL + kBeamFSMain);
    if (!vs || !fs) return false;
    m_beamProgram = glCreateProgram();
    glAttachShader(m_beamProgram, vs);
    glAttachShader(m_beamProgram, fs);
    glLinkProgram(m_beamProgram);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint ok = 0;
    glGetProgramiv(m_beamProgram, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetProgramInfoLog(m_beamProgram, sizeof(log), nullptr, log);
        std::cerr << "[Spotlights] beam shader: " << log << std::endl;
        glDeleteProgram(m_beamProgram);
        m_beamProgram = 0;
        return false;
    }
    // (each texture it reads on its own unit from the start: strict drivers refuse draws where
    // two kinds of texture share one)
    glUseProgram(m_beamProgram);
    glUniform1i(glGetUniformLocation(m_beamProgram, "textureArray"), 0);
    glUniform1i(glGetUniformLocation(m_beamProgram, "u_Voxels"), 3);
    glUseProgram(0);
    // A cone: the tip at the origin, opening along +Z to a circle of radius 1 at z = 1, closed
    // at the wide end. Wound so its outside faces out.
    std::vector<float> v;
    constexpr int kSeg = 32;
    for (int i = 0; i < kSeg; ++i) {
        const float a0 = 6.2831853f * i / kSeg, a1 = 6.2831853f * (i + 1) / kSeg;
        const glm::vec3 b0(std::cos(a0), std::sin(a0), 1.0f), b1(std::cos(a1), std::sin(a1), 1.0f);
        for (const glm::vec3& p : { glm::vec3(0.0f), b1, b0 }) v.insert(v.end(), { p.x, p.y, p.z });
        for (const glm::vec3& p : { glm::vec3(0.0f, 0.0f, 1.0f), b0, b1 }) v.insert(v.end(), { p.x, p.y, p.z });
    }
    m_coneVerts = static_cast<int>(v.size() / 3);
    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);
    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(v.size() * sizeof(float)), v.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);
    return true;
}

void Spotlights::drawBeams(const glm::mat4& viewProj, const glm::vec3& camera, const VoxelVolume& voxels, float time,
                           GLuint textureArray) {
    if (m_drawn.empty() || haze <= 0.0f) return;
    if (!initBeams()) return;
    glUseProgram(m_beamProgram);
    // the same spot uniforms as the world shader
    upload(m_beamProgram, camera, time);
    voxels.bind(m_beamProgram, 3, tracing);
    glUniform1i(glGetUniformLocation(m_beamProgram, "u_BeamSamples"), beamSamples);
    glUniform1i(glGetUniformLocation(m_beamProgram, "u_BeamTrace"), beamTraceSteps);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D_ARRAY, textureArray);
    glUniform1i(glGetUniformLocation(m_beamProgram, "textureArray"), 0);
    glUniformMatrix4fv(glGetUniformLocation(m_beamProgram, "u_ViewProj"), 1, GL_FALSE, glm::value_ptr(viewProj));
    glUniform3f(glGetUniformLocation(m_beamProgram, "u_CameraPos"), camera.x, camera.y, camera.z);
    glUniform1f(glGetUniformLocation(m_beamProgram, "u_Haze"), haze);
    glUniform3f(glGetUniformLocation(m_beamProgram, "u_HazeColor"), hazeColor.r, hazeColor.g, hazeColor.b);
    const GLint modelLoc = glGetUniformLocation(m_beamProgram, "u_Model");
    const GLint spotLoc = glGetUniformLocation(m_beamProgram, "u_Spot");

    const GLboolean depthWas = glIsEnabled(GL_DEPTH_TEST), cullWas = glIsEnabled(GL_CULL_FACE), blendWas = glIsEnabled(GL_BLEND);
    glDisable(GL_DEPTH_TEST);       // (blocks in the way stop the march instead)
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);    // light adds up
    glDepthMask(GL_FALSE);
    glBindVertexArray(m_vao);
    for (size_t i = 0; i < m_drawn.size(); ++i) {
        const SpotLight& s = m_spots[m_drawn[i] - 1];
        if (s.beam <= 0.0f) continue;
        const float half = glm::radians(std::clamp(s.angle, 1.0f, 170.0f) * 0.5f);
        // (very wide beams: the cone is capped at a reasonable width)
        const float r = s.range * std::min(std::tan(half), 4.0f);
        const glm::vec3 f = s.direction();
        const glm::vec3 refUp = std::fabs(f.y) > 0.95f ? glm::vec3(0, 0, 1) : glm::vec3(0, 1, 0);
        const glm::vec3 right = glm::normalize(glm::cross(f, refUp));
        const glm::vec3 up = glm::cross(f, right);       // (right, up, forward): a rotation, not a mirror
        glm::mat4 m(1.0f);
        m[0] = glm::vec4(right * r, 0.0f);
        m[1] = glm::vec4(up * r, 0.0f);
        m[2] = glm::vec4(f * s.range, 0.0f);
        m[3] = glm::vec4(s.position, 1.0f);
        glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(m));
        glUniform1i(spotLoc, static_cast<int>(i));
        glDrawArrays(GL_TRIANGLES, 0, m_coneVerts);
    }
    glBindVertexArray(0);
    glDepthMask(GL_TRUE);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    if (depthWas) glEnable(GL_DEPTH_TEST);
    if (cullWas) glEnable(GL_CULL_FACE);
    if (!blendWas) glDisable(GL_BLEND);
}
