#include "SunShafts.h"
#include "VoxelVolume.h"
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <iostream>
#include <string>

namespace {
const char* kVS = R"(
#version 330 core
const vec2 P[3] = vec2[3](vec2(-1.0, -1.0), vec2(3.0, -1.0), vec2(-1.0, 3.0));
out vec2 uv;
void main() { uv = P[gl_VertexID] * 0.5 + 0.5; gl_Position = vec4(P[gl_VertexID], 0.0, 1.0); }
)";
const char* kMarchHead = R"(
#version 330 core
in vec2 uv;
out vec4 color;
uniform sampler2D depthTex;
uniform int useDepth;
uniform mat4 invViewProj;
uniform vec3 cameraPos;
uniform vec3 sunDir;
uniform vec3 sunColor;
uniform float strength;
)";
const char* kMarchMain = R"(
void main() {
    // where this pixel's view ray ends: the scene there, or (sky) the far end of the march
    float d = useDepth == 1 ? texture(depthTex, uv).r : 1.0;
    vec4 w = invViewProj * vec4(uv * 2.0 - 1.0, d * 2.0 - 1.0, 1.0);
    vec3 end = w.xyz / w.w;
    vec3 ray = end - cameraPos;
    float dist = length(ray);
    ray /= max(dist, 0.0001);
    bool sky = d >= 0.99999;
    bool camWet = voxIsWater(ivec3(floor(cameraPos)));
    float maxDist = camWet ? 24.0 : 48.0;                  // (under water you can't see far)
    if (sky) dist = maxDist;
    dist = min(dist, maxDist);
    const int kSteps = 20;
    float stepLen = dist / float(kSteps);
    // interleaved gradient noise: an even spread of start points from pixel to pixel, which the
    // soft upscale below smooths into clean beams (no blotches)
    float jitter = fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));
    float c = dot(ray, sunDir);
    // how light scatters toward the eye: mostly forward (toward the sun), very little sideways,
    // so open air stays clear and the beams show where there's shade around them.
    // Water scatters far more, and more evenly: its beams show whichever way you look.
    float g = 0.6;
    float phaseAir = (1.0 - g * g) / pow(1.0 + g * g - 2.0 * g * c, 1.5) * 0.08 + 0.004;
    float gw = 0.35;
    float phaseWater = (1.0 - gw * gw) / pow(1.0 + gw * gw - 2.0 * gw * c, 1.5) * 0.08 + 0.03;
    vec3 sum = vec3(0.0);
    float travelledWet = 0.0;
    for (int k = 0; k < kSteps; ++k) {
        vec3 p = cameraPos + ray * ((float(k) + jitter) * stepLen);
        ivec3 cell = ivec3(floor(p));
        float o = voxOpacity(cell);
        if (o > 0.9) break;                                 // inside a block: nothing past it
        float T = voxTransmittance(p, p + sunDir * 80.0, 32);
        if (abs(o - 0.094118) < 0.002) {
            // in water: blue-green beams, dimmer the deeper they are and the more water
            // between them and the eye
            travelledWet += stepLen;
            float depth = voxWaterDepth(p);
            vec3 fade = exp(-(depth * 0.4 + travelledWet * 0.8) * vec3(0.16, 0.06, 0.045));
            sum += T * phaseWater * 16.0 * fade * vec3(0.55, 0.9, 1.0);
        } else {
            sum += vec3(T * phaseAir);
        }
    }
    // (open sky: only the shafts' own air, not a wall of glow around the sun)
    color = vec4(sunColor * sum * stepLen * strength * (sky ? 0.012 : 0.03), 1.0);
}
)";
// Added over the frame: a soft 9-tap upscale (the half-size glow without its noise)
const char* kComp = R"(
#version 330 core
in vec2 uv;
out vec4 color;
uniform sampler2D src;
uniform vec2 texel;        // of the half-size glow
void main() {
    vec3 c = texture(src, uv).rgb * 4.0;
    c += (texture(src, uv + texel * vec2(1.5, 0.0)).rgb + texture(src, uv - texel * vec2(1.5, 0.0)).rgb +
          texture(src, uv + texel * vec2(0.0, 1.5)).rgb + texture(src, uv - texel * vec2(0.0, 1.5)).rgb) * 2.0;
    c += texture(src, uv + texel * vec2(1.5, 1.5)).rgb + texture(src, uv - texel * vec2(1.5, 1.5)).rgb +
         texture(src, uv + texel * vec2(1.5, -1.5)).rgb + texture(src, uv - texel * vec2(1.5, -1.5)).rgb;
    color = vec4(c / 16.0, 1.0);
}
)";

GLuint build(const std::string& fs) {
    auto compile = [](GLenum type, const char* src) {
        GLuint s = glCreateShader(type);
        glShaderSource(s, 1, &src, nullptr);
        glCompileShader(s);
        GLint ok = 0;
        glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char log[1024];
            glGetShaderInfoLog(s, sizeof(log), nullptr, log);
            std::cerr << "[SunShafts] shader: " << log << std::endl;
            glDeleteShader(s);
            return 0u;
        }
        return s;
    };
    const GLuint vs = compile(GL_VERTEX_SHADER, kVS), f = compile(GL_FRAGMENT_SHADER, fs.c_str());
    if (!vs || !f) return 0;
    GLuint p = glCreateProgram();
    glAttachShader(p, vs);
    glAttachShader(p, f);
    glLinkProgram(p);
    glDeleteShader(vs);
    glDeleteShader(f);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) { glDeleteProgram(p); return 0; }
    return p;
}
} // namespace

bool SunShafts::init() {
    if (m_march) return true;
    if (m_failed) return false;
    m_march = build(std::string(kMarchHead) + BLOCKSMITH_VOXEL_TRACE_GLSL + kMarchMain);
    m_comp = build(kComp);
    if (!m_march || !m_comp) { m_failed = true; return false; }
    glGenVertexArrays(1, &m_vao);
    glGenFramebuffers(1, &m_fbo);
    return true;
}

void SunShafts::resize(int w, int h) {
    if (w == m_w && h == m_h) return;
    m_w = w; m_h = h;
    if (!m_depth) glGenTextures(1, &m_depth);
    glBindTexture(GL_TEXTURE_2D, m_depth);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, w, h, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_NONE);
    if (!m_half) glGenTextures(1, &m_half);
    glBindTexture(GL_TEXTURE_2D, m_half);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, std::max(1, w / 2), std::max(1, h / 2), 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_half, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void SunShafts::apply(int width, int height, const glm::mat4& invViewProj, const glm::vec3& camera,
                      const glm::vec3& sunDir, const glm::vec3& sunColor, float strength, const VoxelVolume& voxels) {
    if (width <= 0 || height <= 0 || strength <= 0.0f || !init()) return;
    resize(width, height);
    GLint prevProg = 0, vp[4];
    glGetIntegerv(GL_CURRENT_PROGRAM, &prevProg);
    glGetIntegerv(GL_VIEWPORT, vp);
    const GLboolean depthWas = glIsEnabled(GL_DEPTH_TEST), blendWas = glIsEnabled(GL_BLEND), cullWas = glIsEnabled(GL_CULL_FACE);

    // how far away each pixel is
    if (m_depthOk) {
        while (glGetError() != GL_NO_ERROR) {}
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_depth);
        glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, width, height);
        if (glGetError() != GL_NO_ERROR) {
            m_depthOk = false;
            std::cerr << "[SunShafts] can't copy the depth buffer here: sun shafts are off" << std::endl;
        }
    }
    if (!m_depthOk) return;

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glDepthMask(GL_FALSE);
    glBindVertexArray(m_vao);

    // the glow, at half size
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    glViewport(0, 0, std::max(1, width / 2), std::max(1, height / 2));
    glUseProgram(m_march);
    voxels.bind(m_march, 3, true);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_depth);
    glUniform1i(glGetUniformLocation(m_march, "depthTex"), 0);
    glUniform1i(glGetUniformLocation(m_march, "useDepth"), 1);
    glUniformMatrix4fv(glGetUniformLocation(m_march, "invViewProj"), 1, GL_FALSE, glm::value_ptr(invViewProj));
    glUniform3f(glGetUniformLocation(m_march, "cameraPos"), camera.x, camera.y, camera.z);
    const glm::vec3 sd = glm::normalize(sunDir);
    glUniform3f(glGetUniformLocation(m_march, "sunDir"), sd.x, sd.y, sd.z);
    glUniform3f(glGetUniformLocation(m_march, "sunColor"), sunColor.r, sunColor.g, sunColor.b);
    glUniform1f(glGetUniformLocation(m_march, "strength"), strength);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    // added over the frame
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(vp[0], vp[1], vp[2], vp[3]);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);
    glUseProgram(m_comp);
    glBindTexture(GL_TEXTURE_2D, m_half);
    glUniform1i(glGetUniformLocation(m_comp, "src"), 0);
    glUniform2f(glGetUniformLocation(m_comp, "texel"), 2.0f / width, 2.0f / height);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    glBindVertexArray(0);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    if (!blendWas) glDisable(GL_BLEND);
    if (depthWas) glEnable(GL_DEPTH_TEST);
    if (cullWas) glEnable(GL_CULL_FACE);
    glDepthMask(GL_TRUE);
    glUseProgram(static_cast<GLuint>(prevProg));
}
