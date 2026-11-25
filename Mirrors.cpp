#include "Mirrors.h"
#include "MirrorShape.h"
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <map>
#include <utility>

Mirrors g_Mirrors;

namespace {

const char* kVert = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec4 aUvLayer;   // u, v, the frame's layer, the joined sides
layout (location = 2) in vec4 aTintClarity;
layout (location = 3) in vec3 aGlass;
uniform mat4 u_ViewProj;
out vec3 UvLayer;
flat out int Joined;
flat out vec4 TintClarity;
flat out vec3 Glass;
void main() {
    TintClarity = aTintClarity;
    Glass = aGlass;
    UvLayer = aUvLayer.xyz;
    Joined = int(aUvLayer.w + 0.5);
    gl_Position = u_ViewProj * vec4(aPos, 1.0);
}
)";

// The glass: the reflection, where the frame's picture is see-through
const char* kFrag = R"(
#version 330 core
in vec3 UvLayer;
flat in int Joined;
flat in vec4 TintClarity;
flat in vec3 Glass;
out vec4 FragColor;
uniform sampler2DArray u_Frame;
uniform sampler2D u_Reflection;
uniform vec2 u_ScreenSize;
void main() {
    // (the frame: already drawn. Where another mirror carries on, the middle of the picture,
    // as the world shader does)
    vec2 uv = UvLayer.xy;
    if ((Joined & 1) != 0) uv.x = max(uv.x, 0.5);
    if ((Joined & 2) != 0) uv.x = min(uv.x, 0.5);
    if ((Joined & 4) != 0) uv.y = min(uv.y, 0.5);
    if ((Joined & 8) != 0) uv.y = max(uv.y, 0.5);
    if (UvLayer.z >= 0.0 && texture(u_Frame, vec3(uv, UvLayer.z)).a >= 0.5) discard;
    vec3 c = texture(u_Reflection, gl_FragCoord.xy / u_ScreenSize).rgb;
    // a misted mirror (clarity under 1): the reflection fades toward the glass's own colour
    c = mix(Glass * 0.75, c, TintClarity.a);
    // silvered glass gives back a little less light, very slightly cool; then the mirror's tint
    FragColor = vec4(c * vec3(0.92, 0.95, 0.96) * TintClarity.rgb, 1.0);
}
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
        std::cerr << "[Mirrors] shader: " << log << std::endl;
        glDeleteShader(s);
        return 0;
    }
    return s;
}

// The screen rectangle (pixels) a polygon (world points in order) covers; false when it's off
// screen (all of it beyond one edge of the view, or behind the camera).
bool screenRect(const glm::vec3* pts, int n, const glm::mat4& viewProj, int w, int h, glm::vec4& r) {
    glm::vec4 c[8];
    for (int i = 0; i < n; ++i) c[i] = viewProj * glm::vec4(pts[i], 1.0f);
    // outside one plane of the view entirely
    auto allOutside = [&](auto test) { for (int i = 0; i < n; ++i) if (!test(c[i])) return false; return true; };
    if (allOutside([](const glm::vec4& v) { return v.x < -v.w; }) || allOutside([](const glm::vec4& v) { return v.x > v.w; }) ||
        allOutside([](const glm::vec4& v) { return v.y < -v.w; }) || allOutside([](const glm::vec4& v) { return v.y > v.w; }) ||
        allOutside([](const glm::vec4& v) { return v.w <= 0.05f; }))
        return false;
    // the corners in front of the camera, and where the edges cross to behind it (the polygon cut
    // at the camera: a mirror beside you covers only its part of the screen)
    constexpr float kNear = 0.05f;
    glm::vec2 mn(1e9f), mx(-1e9f);
    auto add = [&](const glm::vec4& v) {
        const glm::vec2 ndc = glm::vec2(v) / v.w;
        mn = glm::min(mn, ndc);
        mx = glm::max(mx, ndc);
    };
    for (int i = 0; i < n; ++i) {
        const glm::vec4& a = c[i];
        const glm::vec4& b = c[(i + 1) % n];
        if (a.w > kNear) add(a);
        if ((a.w > kNear) != (b.w > kNear)) add(glm::mix(a, b, (kNear - a.w) / (b.w - a.w)));
    }
    mn = glm::clamp(mn, glm::vec2(-1.0f), glm::vec2(1.0f));
    mx = glm::clamp(mx, glm::vec2(-1.0f), glm::vec2(1.0f));
    r = glm::vec4((mn.x * 0.5f + 0.5f) * w, (mn.y * 0.5f + 0.5f) * h, (mx.x * 0.5f + 0.5f) * w, (mx.y * 0.5f + 0.5f) * h);
    return true;
}

} // namespace

bool Mirrors::choose(const std::vector<Cell>& cells, const glm::mat4& viewProj, const glm::vec3& eye, int fbw, int fbh) {
    m_chosen.clear();
    if (cells.empty() || fbw <= 0 || fbh <= 0 || m_failed) return false;
    // one surface: the way it faces, and how far along that its glass is (in 64ths)
    struct Surface {
        glm::vec4 plane;
        float nearest = 1e9f;
        glm::vec4 rect{ 1e9f, 1e9f, -1e9f, -1e9f };
        std::vector<Cell> cells;
    };
    std::map<std::pair<int, int>, Surface> surfaces;
    for (const Cell& c : cells) {
        const glm::vec4 pl = mirror::plane(c.pos, c.meta);
        const float side = glm::dot(glm::vec3(pl), eye) + pl.w;
        if (side < 0.02f) continue;                       // (behind it, or edge on)
        glm::vec3 corners[4];
        mirror::frontCorners(c.pos, c.meta, corners);
        glm::vec4 r;
        if (!screenRect(corners, 4, viewProj, fbw, fbh, r)) continue;
        const glm::vec3 centre = (corners[0] + corners[2]) * 0.5f;
        const float dist = glm::length(centre - eye);
        if (dist > kRange) continue;
        Surface& s = surfaces[{ c.meta & 3, static_cast<int>(std::lround(pl.w * 64.0f)) }];
        s.plane = pl;
        s.nearest = std::min(s.nearest, dist);
        s.rect = glm::vec4(std::min(s.rect.x, r.x), std::min(s.rect.y, r.y), std::max(s.rect.z, r.z), std::max(s.rect.w, r.w));
        s.cells.push_back(c);
    }
    // The one being looked at: under the middle of the screen first, then the one covering most of
    // the screen (a mirror at the edge of the view loses to the one in front of you)
    const Surface* best = nullptr;
    float bestScore = -1.0f;
    const float cx = fbw * 0.5f, cy = fbh * 0.5f;
    for (const auto& [key, s] : surfaces) {
        const float area = std::max(0.0f, s.rect.z - s.rect.x) * std::max(0.0f, s.rect.w - s.rect.y) / (static_cast<float>(fbw) * fbh);
        const bool centre = cx >= s.rect.x && cx <= s.rect.z && cy >= s.rect.y && cy <= s.rect.w;
        const float score = area + (centre ? 1.0f : 0.0f) + 0.001f / (1.0f + s.nearest);
        if (score > bestScore) { bestScore = score; best = &s; }
    }
    if (!best) return false;
    if (!ensureTarget(fbw, fbh) || !ensureProgram()) return false;
    m_plane = best->plane;
    m_chosen = best->cells;
    const int x0 = std::max(0, static_cast<int>(std::floor(best->rect.x)) - 2);
    const int y0 = std::max(0, static_cast<int>(std::floor(best->rect.y)) - 2);
    const int x1 = std::min(fbw, static_cast<int>(std::ceil(best->rect.z)) + 2);
    const int y1 = std::min(fbh, static_cast<int>(std::ceil(best->rect.w)) + 2);
    if (x1 <= x0 || y1 <= y0) { m_chosen.clear(); return false; }
    m_rect[0] = x0; m_rect[1] = y0; m_rect[2] = x1 - x0; m_rect[3] = y1 - y0;
    return true;
}

glm::mat4 Mirrors::reflection() const {
    const glm::vec3 n(m_plane);
    const float d = m_plane.w;
    glm::mat4 r(1.0f);
    for (int c = 0; c < 3; ++c)
        for (int row = 0; row < 3; ++row) r[c][row] -= 2.0f * n[c] * n[row];
    r[3] = glm::vec4(-2.0f * d * n, 1.0f);
    return r;
}

glm::vec3 Mirrors::reflect(const glm::vec3& p) const {
    const glm::vec3 n(m_plane);
    return p - 2.0f * (glm::dot(n, p) + m_plane.w) * n;
}

glm::mat4 Mirrors::clippedProjection(const glm::mat4& proj, const glm::mat4& mirroredView) const {
    // Lengyel's oblique near plane: the near plane becomes the mirror's surface (moved a hair
    // into the room, so the glass itself isn't in its own reflection)
    glm::vec4 worldPlane = m_plane;
    worldPlane.w -= 0.004f;
    const glm::vec4 c = glm::transpose(glm::inverse(mirroredView)) * worldPlane;
    glm::mat4 p = proj;
    auto sgn = [](float v) { return v > 0.0f ? 1.0f : v < 0.0f ? -1.0f : 0.0f; };
    glm::vec4 q;
    q.x = (sgn(c.x) + p[2][0]) / p[0][0];
    q.y = (sgn(c.y) + p[2][1]) / p[1][1];
    q.z = -1.0f;
    q.w = (1.0f + p[2][2]) / p[3][2];
    const float cq = glm::dot(c, q);
    if (std::fabs(cq) < 1e-8f) return proj;
    const glm::vec4 m = c * (2.0f / cq);
    p[0][2] = m.x;
    p[1][2] = m.y;
    p[2][2] = m.z + 1.0f;
    p[3][2] = m.w;
    return p;
}

bool Mirrors::ensureTarget(int w, int h) {
    if (m_fbo && w == m_w && h == m_h) return true;
    m_w = w; m_h = h;
    if (!m_fbo) glGenFramebuffers(1, &m_fbo);
    if (!m_color) glGenTextures(1, &m_color);
    if (!m_depth) glGenRenderbuffers(1, &m_depth);
    glBindTexture(GL_TEXTURE_2D, m_color);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindRenderbuffer(GL_RENDERBUFFER, m_depth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);
    GLint prev = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev);
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_color, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, m_depth);
    const bool ok = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prev));
    if (!ok) {
        std::cerr << "[Mirrors] this graphics card can't draw into a picture: mirrors show plain glass" << std::endl;
        m_failed = true;
        release();
    }
    return ok;
}

bool Mirrors::ensureProgram() {
    if (m_program) return true;
    const GLuint vs = compile(GL_VERTEX_SHADER, kVert), fs = compile(GL_FRAGMENT_SHADER, kFrag);
    if (!vs || !fs) { m_failed = true; return false; }
    m_program = glCreateProgram();
    glAttachShader(m_program, vs);
    glAttachShader(m_program, fs);
    glLinkProgram(m_program);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint ok = 0;
    glGetProgramiv(m_program, GL_LINK_STATUS, &ok);
    if (!ok) {
        std::cerr << "[Mirrors] shader didn't link" << std::endl;
        glDeleteProgram(m_program);
        m_program = 0;
        m_failed = true;
        return false;
    }
    // (each sampler type on its own unit, set once: strict drivers refuse two types on one unit)
    glUseProgram(m_program);
    glUniform1i(glGetUniformLocation(m_program, "u_Frame"), 0);
    glUniform1i(glGetUniformLocation(m_program, "u_Reflection"), 7);
    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);
    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    const GLsizei stride = 14 * sizeof(float);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, stride, (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, stride, (void*)(7 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, stride, (void*)(11 * sizeof(float)));
    glEnableVertexAttribArray(3);
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    return true;
}

void Mirrors::begin(const glm::vec3& clearColor) {
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &m_prevFbo);
    glGetIntegerv(GL_VIEWPORT, m_prevViewport);
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    glViewport(0, 0, m_w, m_h);
    glEnable(GL_SCISSOR_TEST);
    glScissor(m_rect[0], m_rect[1], m_rect[2], m_rect[3]);
    glClearColor(clearColor.r, clearColor.g, clearColor.b, 1.0f);
    glDepthMask(GL_TRUE);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    // (a reflection turns every triangle the other way round)
    glFrontFace(GL_CW);
}

void Mirrors::end() {
    glFrontFace(GL_CCW);
    glDisable(GL_SCISSOR_TEST);
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(m_prevFbo));
    glViewport(m_prevViewport[0], m_prevViewport[1], m_prevViewport[2], m_prevViewport[3]);
}

void Mirrors::composite(const glm::mat4& viewProj, GLuint textureArray) {
    if (m_chosen.empty() || !m_program) return;
    std::vector<float> v;
    v.reserve(m_chosen.size() * 6 * 14);
    const float uv[4][2] = { { 0, 1 }, { 1, 1 }, { 1, 0 }, { 0, 0 } };
    for (const Cell& c : m_chosen) {
        glm::vec3 p[4];
        mirror::frontCorners(c.pos, c.meta, p);
        for (int i : { 0, 1, 2, 0, 2, 3 }) {
            v.insert(v.end(), { p[i].x, p[i].y, p[i].z, uv[i][0], uv[i][1], static_cast<float>(c.layer),
                                static_cast<float>(c.joined), c.tint.r, c.tint.g, c.tint.b, c.clarity,
                                c.glass.r, c.glass.g, c.glass.b });
        }
    }
    const GLboolean blendWas = glIsEnabled(GL_BLEND), cullWas = glIsEnabled(GL_CULL_FACE);
    GLint depthFunc = GL_LESS;
    glGetIntegerv(GL_DEPTH_FUNC, &depthFunc);
    glUseProgram(m_program);
    glUniformMatrix4fv(glGetUniformLocation(m_program, "u_ViewProj"), 1, GL_FALSE, glm::value_ptr(viewProj));
    glUniform2f(glGetUniformLocation(m_program, "u_ScreenSize"), static_cast<float>(m_w), static_cast<float>(m_h));
    glActiveTexture(GL_TEXTURE7);
    glBindTexture(GL_TEXTURE_2D, m_color);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D_ARRAY, textureArray);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_FALSE);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(-1.0f, -2.0f);          // (on the very same spot as the glass already drawn)
    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(v.size() * sizeof(float)), v.data(), GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(v.size() / 14));
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glDisable(GL_POLYGON_OFFSET_FILL);
    glDepthMask(GL_TRUE);
    glDepthFunc(static_cast<GLenum>(depthFunc));
    if (blendWas) glEnable(GL_BLEND);
    if (cullWas) glEnable(GL_CULL_FACE);
    glActiveTexture(GL_TEXTURE7);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
}

void Mirrors::release() {
    if (m_fbo) glDeleteFramebuffers(1, &m_fbo);
    if (m_color) glDeleteTextures(1, &m_color);
    if (m_depth) glDeleteRenderbuffers(1, &m_depth);
    if (m_program) glDeleteProgram(m_program);
    if (m_vao) glDeleteVertexArrays(1, &m_vao);
    if (m_vbo) glDeleteBuffers(1, &m_vbo);
    m_fbo = m_color = m_depth = m_program = m_vao = m_vbo = 0;
    m_w = m_h = 0;
    m_chosen.clear();
}
