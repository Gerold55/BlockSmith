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

bool screenPolygon(const glm::vec3* pts, int n, const glm::mat4& viewProj, int w, int h,
                   std::vector<glm::vec2>& polygon) {
    constexpr float kNear = 0.05f;
    std::vector<glm::vec4> clip;
    clip.reserve(n + 2);
    for (int i = 0; i < n; ++i) clip.push_back(viewProj * glm::vec4(pts[i], 1.0f));

    std::vector<glm::vec4> nearClipped;
    for (size_t i = 0; i < clip.size(); ++i) {
        const glm::vec4& a = clip[i];
        const glm::vec4& b = clip[(i + 1) % clip.size()];
        const bool aInside = a.w > kNear, bInside = b.w > kNear;
        if (aInside) nearClipped.push_back(a);
        if (aInside != bInside) nearClipped.push_back(glm::mix(a, b, (kNear - a.w) / (b.w - a.w)));
    }
    if (nearClipped.size() < 3) return false;

    polygon.clear();
    polygon.reserve(nearClipped.size() + 4);
    for (const glm::vec4& p : nearClipped) {
        const glm::vec2 ndc = glm::vec2(p) / p.w;
        polygon.emplace_back((ndc.x * 0.5f + 0.5f) * w, (ndc.y * 0.5f + 0.5f) * h);
    }
    for (int edge = 0; edge < 4; ++edge) {
        const std::vector<glm::vec2> input = polygon;
        polygon.clear();
        if (input.empty()) return false;
        auto inside = [&](const glm::vec2& p) {
            if (edge == 0) return p.x >= 0.0f;
            if (edge == 1) return p.x <= static_cast<float>(w);
            if (edge == 2) return p.y >= 0.0f;
            return p.y <= static_cast<float>(h);
        };
        auto intersect = [&](const glm::vec2& a, const glm::vec2& b) {
            if (edge < 2) {
                const float x = edge == 0 ? 0.0f : static_cast<float>(w);
                return glm::mix(a, b, (x - a.x) / (b.x - a.x));
            }
            const float y = edge == 2 ? 0.0f : static_cast<float>(h);
            return glm::mix(a, b, (y - a.y) / (b.y - a.y));
        };
        for (size_t i = 0; i < input.size(); ++i) {
            const glm::vec2& a = input[i];
            const glm::vec2& b = input[(i + 1) % input.size()];
            const bool aInside = inside(a), bInside = inside(b);
            if (aInside) polygon.push_back(a);
            if (aInside != bInside) polygon.push_back(intersect(a, b));
        }
    }
    return polygon.size() >= 3;
}

float polygonArea(const std::vector<glm::vec2>& polygon) {
    float twiceArea = 0.0f;
    for (size_t i = 0; i < polygon.size(); ++i) {
        const glm::vec2& a = polygon[i];
        const glm::vec2& b = polygon[(i + 1) % polygon.size()];
        twiceArea += a.x * b.y - a.y * b.x;
    }
    return std::abs(twiceArea) * 0.5f;
}

bool polygonContains(const std::vector<glm::vec2>& polygon, const glm::vec2& p) {
    bool inside = false;
    for (size_t i = 0; i < polygon.size(); ++i) {
        const glm::vec2& a = polygon[i];
        const glm::vec2& b = polygon[(i + 1) % polygon.size()];
        const glm::vec2 edge = b - a, toPoint = p - a;
        const float cross = edge.x * toPoint.y - edge.y * toPoint.x;
        if (std::abs(cross) < 0.001f && glm::dot(toPoint, p - b) <= 0.001f) return true;
        if ((a.y > p.y) != (b.y > p.y) && p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x)
            inside = !inside;
    }
    return inside;
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
        float area = 0.0f;
        bool coversCentre = false;
        std::vector<Cell> cells;
    };
    std::map<std::pair<int, int>, Surface> surfaces;
    const glm::vec2 screenCentre(fbw * 0.5f, fbh * 0.5f);
    for (const Cell& c : cells) {
        const glm::vec4 pl = mirror::plane(c.pos, c.meta);
        const float side = glm::dot(glm::vec3(pl), eye) + pl.w;
        if (side < 0.02f) continue;                       // (behind it, or edge on)
        glm::vec3 corners[4];
        mirror::frontCorners(c.pos, c.meta, corners);
        glm::vec4 r;
        if (!screenRect(corners, 4, viewProj, fbw, fbh, r)) continue;
        std::vector<glm::vec2> polygon;
        if (!screenPolygon(corners, 4, viewProj, fbw, fbh, polygon)) continue;
        const glm::vec3 centre = (corners[0] + corners[2]) * 0.5f;
        const float dist = glm::length(centre - eye);
        if (dist > kRange) continue;
        Surface& s = surfaces[{ c.meta & 3, static_cast<int>(std::lround(pl.w * 64.0f)) }];
        s.plane = pl;
        s.nearest = std::min(s.nearest, dist);
        s.rect = glm::vec4(std::min(s.rect.x, r.x), std::min(s.rect.y, r.y), std::max(s.rect.z, r.z), std::max(s.rect.w, r.w));
        s.area += polygonArea(polygon);
        s.coversCentre = s.coversCentre || polygonContains(polygon, screenCentre);
        s.cells.push_back(c);
    }
    // The one being looked at: under the middle of the screen first, then the one covering most of
    // the screen (a mirror at the edge of the view loses to the one in front of you)
    const Surface* best = nullptr;
    std::pair<int, int> bestKey{ 0, 0 };
    float bestScore = -1.0f;
    for (const auto& [key, s] : surfaces) {
        const float area = s.area / (static_cast<float>(fbw) * fbh);
        float score = area + (s.coversCentre ? 1.0f : 0.0f) + 0.001f / (1.0f + s.nearest);
        // (the mirror that was live stays live unless another clearly wins: no flicking between
        // two that score about the same while you move)
        if (m_hasLast && key == m_lastKey) score += 0.2f;
        if (score > bestScore) { bestScore = score; best = &s; bestKey = key; }
    }
    if (!best) { m_hasLast = false; return false; }
    m_lastKey = bestKey;
    m_hasLast = true;
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

glm::vec4 Mirrors::clipPlane() const {
    // (a hair into the room, so the glass itself isn't in its own reflection)
    glm::vec4 p = m_plane;
    p.w -= 0.004f;
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
        // (laid a hair in front of the glass: drawn on the very same spot, the depth test could go
        // either way as you move - the reflection flickered, and up close it lost to the glass)
        const glm::vec3 lift = glm::vec3(mirror::normalOf(c.meta & 3)) * 0.003f;
        for (glm::vec3& q : p) q += lift;
        for (int i : { 0, 1, 2, 0, 2, 3 }) {
            v.insert(v.end(), { p[i].x, p[i].y, p[i].z, uv[i][0], uv[i][1], static_cast<float>(c.layer),
                                static_cast<float>(c.joined), c.tint.r, c.tint.g, c.tint.b, c.clarity,
                                c.glass.r, c.glass.g, c.glass.b });
        }
    }
    const GLboolean blendWas = glIsEnabled(GL_BLEND), cullWas = glIsEnabled(GL_CULL_FACE);
    GLboolean depthMaskWas = GL_TRUE;
    glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMaskWas);
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
    glDepthMask(GL_TRUE);
    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(v.size() * sizeof(float)), v.data(), GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(v.size() / 14));
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glDepthMask(depthMaskWas);
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
