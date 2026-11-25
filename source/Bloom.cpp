#include "Bloom.h"
#include <algorithm>
#include <iostream>

namespace {
const char* kVS = R"(
#version 330 core
const vec2 P[3] = vec2[3](vec2(-1.0, -1.0), vec2(3.0, -1.0), vec2(-1.0, 3.0));
out vec2 uv;
void main() { uv = P[gl_VertexID] * 0.5 + 0.5; gl_Position = vec4(P[gl_VertexID], 0.0, 1.0); }
)";
// The bright parts (a soft knee above the threshold), four samples averaged down to half size
const char* kBright = R"(
#version 330 core
in vec2 uv;
out vec4 color;
uniform sampler2D src;
uniform sampler2D depth;
uniform int useDepth;
uniform vec2 texel;
uniform float threshold;
uniform vec2 fade;        // distance where the glow starts fading, and where it's gone
uniform vec2 clipZ;       // near, far
void main() {
    float near_ = 1.0;
    if (useDepth == 1) {
        float d = texture(depth, uv).r;
        if (d >= 0.99999) { color = vec4(0.0); return; }           // the sky
        float z = 2.0 * clipZ.x * clipZ.y / (clipZ.y + clipZ.x - (d * 2.0 - 1.0) * (clipZ.y - clipZ.x));
        near_ = 1.0 - smoothstep(fade.x, max(fade.y, fade.x + 1.0), z);
        if (near_ <= 0.0) { color = vec4(0.0); return; }
    }
    vec3 c = (texture(src, uv + texel * vec2(-0.5, -0.5)).rgb + texture(src, uv + texel * vec2(0.5, -0.5)).rgb +
              texture(src, uv + texel * vec2(-0.5, 0.5)).rgb + texture(src, uv + texel * vec2(0.5, 0.5)).rgb) * 0.25;
    // (brightness between the brightest channel and the luminance: a yellow sand face isn't
    // treated as a lamp)
    float b = mix(dot(c, vec3(0.2126, 0.7152, 0.0722)), max(c.r, max(c.g, c.b)), 0.5);
    float knee = clamp((b - threshold) / max(1.0 - threshold, 0.01), 0.0, 1.0);
    color = vec4(c * knee * knee * near_, 1.0);
}
)";
// Down one level: the centre and four diagonal taps (dual-filter blur: smooth, no blockiness)
const char* kDown = R"(
#version 330 core
in vec2 uv;
out vec4 color;
uniform sampler2D src;
uniform vec2 texel;        // of the source
void main() {
    vec3 c = texture(src, uv).rgb * 4.0;
    c += texture(src, uv + texel * vec2(-1.0, -1.0)).rgb;
    c += texture(src, uv + texel * vec2( 1.0, -1.0)).rgb;
    c += texture(src, uv + texel * vec2(-1.0,  1.0)).rgb;
    c += texture(src, uv + texel * vec2( 1.0,  1.0)).rgb;
    color = vec4(c / 8.0, 1.0);
}
)";
// Up one level (added onto the bigger level): a tent of eight taps
const char* kUp = R"(
#version 330 core
in vec2 uv;
out vec4 color;
uniform sampler2D src;
uniform vec2 texel;        // of the source
uniform float weight;
void main() {
    vec3 c = texture(src, uv + texel * vec2(-2.0, 0.0)).rgb + texture(src, uv + texel * vec2(2.0, 0.0)).rgb +
             texture(src, uv + texel * vec2(0.0, -2.0)).rgb + texture(src, uv + texel * vec2(0.0, 2.0)).rgb;
    c += (texture(src, uv + texel * vec2(-1.0, -1.0)).rgb + texture(src, uv + texel * vec2(1.0, -1.0)).rgb +
          texture(src, uv + texel * vec2(-1.0, 1.0)).rgb + texture(src, uv + texel * vec2(1.0, 1.0)).rgb) * 2.0;
    color = vec4(c / 12.0 * weight, 1.0);
}
)";
// Onto the frame (screen blend: GL_ONE, GL_ONE_MINUS_SRC_COLOR), a touch warm
const char* kComp = R"(
#version 330 core
in vec2 uv;
out vec4 color;
uniform sampler2D src;
uniform float strength;
void main() {
    vec3 g = texture(src, uv).rgb * strength;
    g *= vec3(1.0, 0.97, 0.92);
    color = vec4(g / (1.0 + g), 1.0);       // (soft roll-off: very bright glow never clips)
}
)";

GLuint program(const char* fsSrc) {
    auto compile = [](GLenum type, const char* src) {
        GLuint s = glCreateShader(type);
        glShaderSource(s, 1, &src, nullptr);
        glCompileShader(s);
        GLint ok = 0;
        glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char log[1024];
            glGetShaderInfoLog(s, sizeof(log), nullptr, log);
            std::cerr << "[Bloom] shader: " << log << std::endl;
            glDeleteShader(s);
            return 0u;
        }
        return s;
    };
    const GLuint vs = compile(GL_VERTEX_SHADER, kVS), fs = compile(GL_FRAGMENT_SHADER, fsSrc);
    if (!vs || !fs) return 0;
    GLuint p = glCreateProgram();
    glAttachShader(p, vs);
    glAttachShader(p, fs);
    glLinkProgram(p);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) { glDeleteProgram(p); return 0; }
    return p;
}

void makeTex(GLuint& t, int w, int h, GLenum internal) {
    if (!t) glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexImage2D(GL_TEXTURE_2D, 0, internal, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}
} // namespace

bool Bloom::init() {
    if (m_bright) return true;
    if (m_failed) return false;
    m_bright = program(kBright);
    m_down = program(kDown);
    m_up = program(kUp);
    m_comp = program(kComp);
    if (!m_bright || !m_down || !m_up || !m_comp) { m_failed = true; return false; }
    glGenVertexArrays(1, &m_vao);
    glGenFramebuffers(kLevels, m_fbo);
    return true;
}

void Bloom::resize(int w, int h) {
    if (w == m_w && h == m_h) return;
    m_w = w; m_h = h;
    makeTex(m_scene, w, h, GL_RGBA8);
    if (!m_depth) glGenTextures(1, &m_depth);
    glBindTexture(GL_TEXTURE_2D, m_depth);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, w, h, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_NONE);
    for (int i = 0; i < kLevels; ++i) {
        m_lw[i] = std::max(1, w >> (i + 1));
        m_lh[i] = std::max(1, h >> (i + 1));
        makeTex(m_level[i], m_lw[i], m_lh[i], GL_RGBA16F);   // (smooth glow: no banding in the faint edges)
        glBindFramebuffer(GL_FRAMEBUFFER, m_fbo[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_level[i], 0);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Bloom::apply(int width, int height, float strength, float threshold, float fadeStart, float fadeEnd,
                  float nearZ, float farZ) {
    if (width <= 0 || height <= 0 || strength <= 0.0f || !init()) return;
    resize(width, height);
    GLint prevProg = 0, vp[4];
    glGetIntegerv(GL_CURRENT_PROGRAM, &prevProg);
    glGetIntegerv(GL_VIEWPORT, vp);
    const GLboolean depthWas = glIsEnabled(GL_DEPTH_TEST), blendWas = glIsEnabled(GL_BLEND), cullWas = glIsEnabled(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glDepthMask(GL_FALSE);
    glBindVertexArray(m_vao);
    glActiveTexture(GL_TEXTURE0);

    // 1. the frame so far
    glBindTexture(GL_TEXTURE_2D, m_scene);
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, width, height);
    // and how far away each pixel is (if this driver can copy the depth buffer; otherwise the
    // glow just isn't faded with distance)
    if (m_depthOk) {
        while (glGetError() != GL_NO_ERROR) {}
        glBindTexture(GL_TEXTURE_2D, m_depth);
        glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, width, height);
        if (glGetError() != GL_NO_ERROR) {
            m_depthOk = false;
            std::cerr << "[Bloom] can't copy the depth buffer here: the glow won't fade with distance" << std::endl;
        }
    }
    // 2. its bright parts, at half size
    glViewport(0, 0, m_lw[0], m_lh[0]);
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo[0]);
    glUseProgram(m_bright);
    glBindTexture(GL_TEXTURE_2D, m_scene);
    glUniform1i(glGetUniformLocation(m_bright, "src"), 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, m_depth);
    glActiveTexture(GL_TEXTURE0);
    glUniform1i(glGetUniformLocation(m_bright, "depth"), 1);
    glUniform1i(glGetUniformLocation(m_bright, "useDepth"), m_depthOk ? 1 : 0);
    glUniform2f(glGetUniformLocation(m_bright, "fade"), fadeStart, fadeEnd);
    glUniform2f(glGetUniformLocation(m_bright, "clipZ"), nearZ, farZ);
    glUniform2f(glGetUniformLocation(m_bright, "texel"), 1.0f / width, 1.0f / height);
    glUniform1f(glGetUniformLocation(m_bright, "threshold"), std::clamp(threshold, 0.0f, 0.99f));
    glDrawArrays(GL_TRIANGLES, 0, 3);
    // 3. down the chain: each level a blurred, half-size copy of the one before
    glUseProgram(m_down);
    glUniform1i(glGetUniformLocation(m_down, "src"), 0);
    for (int i = 1; i < kLevels; ++i) {
        glBindFramebuffer(GL_FRAMEBUFFER, m_fbo[i]);
        glViewport(0, 0, m_lw[i], m_lh[i]);
        glBindTexture(GL_TEXTURE_2D, m_level[i - 1]);
        glUniform2f(glGetUniformLocation(m_down, "texel"), 1.0f / m_lw[i - 1], 1.0f / m_lh[i - 1]);
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }
    // 4. back up: each smaller level spread out and added onto the next bigger one, so the
    //    biggest ends up with all of them - the wide halo a little fainter than the core
    glUseProgram(m_up);
    glUniform1i(glGetUniformLocation(m_up, "src"), 0);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);
    for (int i = kLevels - 1; i > 0; --i) {
        glBindFramebuffer(GL_FRAMEBUFFER, m_fbo[i - 1]);
        glViewport(0, 0, m_lw[i - 1], m_lh[i - 1]);
        glBindTexture(GL_TEXTURE_2D, m_level[i]);
        glUniform2f(glGetUniformLocation(m_up, "texel"), 0.5f / m_lw[i], 0.5f / m_lh[i]);
        glUniform1f(glGetUniformLocation(m_up, "weight"), 0.8f);
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }
    // 5. onto the frame, screen-blended: brightens without washing out
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(vp[0], vp[1], vp[2], vp[3]);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_COLOR);
    glUseProgram(m_comp);
    glBindTexture(GL_TEXTURE_2D, m_level[0]);
    glUniform1i(glGetUniformLocation(m_comp, "src"), 0);
    // (the chain adds about three levels' worth of light: scaled so the strength setting means
    // about what it did)
    glUniform1f(glGetUniformLocation(m_comp, "strength"), strength * 0.55f);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    glBindVertexArray(0);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    if (!blendWas) glDisable(GL_BLEND);
    if (depthWas) glEnable(GL_DEPTH_TEST);
    if (cullWas) glEnable(GL_CULL_FACE);
    glDepthMask(GL_TRUE);
    glUseProgram(static_cast<GLuint>(prevProg));
}
