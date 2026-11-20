#include "Inventory.h"
#include "Block.h"
#include "Hotbar.h"
#include "FontRenderer.h"
#include "UiDraw.h"
#include <iostream>
#include <algorithm>
#include <cmath>
#include <tuple>
#include <vector>

extern Hotbar* g_Hotbar;
extern FontRenderer* g_FontRenderer;
extern GLuint g_TextureArrayID;

namespace {

// If text doesn't appear in the inventory, set this to true.
constexpr bool kTextUsesTexturedProgram = false;

// ===========================================================================
// Shared state between C++ and the Lua bindings
// ===========================================================================

struct DrawContext {
    bool   active = false;     // drawing is only allowed while a Lua screen is being rendered
    GLuint uiProgram = 0;
    GLuint texturedProgram = 0;
    float  W = 0.0f, H = 0.0f;
};

DrawContext  s_ctx;
sol::state*  s_lua = nullptr;
GLFWwindow*  s_window = nullptr;
std::string  s_lastLuaError;
bool         s_wasOpen = false;

// ===========================================================================
// GL state guard
// ===========================================================================

struct GLStateGuard {
    GLint program = 0, vao = 0, arrayBuffer = 0, activeTex = 0, tex2DArray = 0, tex2D = 0;
    GLint depthFunc = GL_LESS;
    GLint blendSrcRGB = 0, blendDstRGB = 0, blendSrcA = 0, blendDstA = 0;
    GLboolean depth = GL_FALSE, cull = GL_FALSE, blend = GL_FALSE, scissor = GL_FALSE;
    GLboolean depthMask = GL_TRUE;

    GLStateGuard() {
        glGetIntegerv(GL_CURRENT_PROGRAM, &program);
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao);
        glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &arrayBuffer);
        glGetIntegerv(GL_ACTIVE_TEXTURE, &activeTex);
        glActiveTexture(GL_TEXTURE0);
        glGetIntegerv(GL_TEXTURE_BINDING_2D_ARRAY, &tex2DArray);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &tex2D);
        glGetIntegerv(GL_BLEND_SRC_RGB, &blendSrcRGB);
        glGetIntegerv(GL_BLEND_DST_RGB, &blendDstRGB);
        glGetIntegerv(GL_BLEND_SRC_ALPHA, &blendSrcA);
        glGetIntegerv(GL_BLEND_DST_ALPHA, &blendDstA);
        depth   = glIsEnabled(GL_DEPTH_TEST);
        cull    = glIsEnabled(GL_CULL_FACE);
        blend   = glIsEnabled(GL_BLEND);
        scissor = glIsEnabled(GL_SCISSOR_TEST);
        glGetIntegerv(GL_DEPTH_FUNC, &depthFunc);
        glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMask);
    }

    ~GLStateGuard() {
        glUseProgram(program);
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, arrayBuffer);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D_ARRAY, tex2DArray);
        glBindTexture(GL_TEXTURE_2D, tex2D);
        glActiveTexture(activeTex);
        glBlendFuncSeparate(blendSrcRGB, blendDstRGB, blendSrcA, blendDstA);
        glDepthFunc(depthFunc);
        glDepthMask(depthMask);
        depth   ? glEnable(GL_DEPTH_TEST)   : glDisable(GL_DEPTH_TEST);
        cull    ? glEnable(GL_CULL_FACE)    : glDisable(GL_CULL_FACE);
        blend   ? glEnable(GL_BLEND)        : glDisable(GL_BLEND);
        scissor ? glEnable(GL_SCISSOR_TEST) : glDisable(GL_SCISSOR_TEST);
    }
};

// ===========================================================================
// Low-level drawing
// ===========================================================================

struct QuadCorner { float x, y, u, v; };

const FaceDefinition* getIconFace(const BlockProperty& block) {
    const int order[] = { 4, 2, 0, 1, 5, 3 };
    for (int f : order) {
        if (block.faces[f].baseLayer >= 0) return &block.faces[f];
    }
    return nullptr;
}

GLuint compileShader(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        std::cerr << "[Inventory] Icon shader compile error: " << log << std::endl;
    }
    return s;
}

GLuint getIconProgram() {
    static GLuint program = 0;
    if (program) return program;

    const char* vs = R"(#version 330 core
        layout(location = 0) in vec2 aPos;
        layout(location = 1) in vec3 aUVL;
        out vec3 vUVL;
        void main() { vUVL = aUVL; gl_Position = vec4(aPos, 0.0, 1.0); })";

    const char* fs = R"(#version 330 core
        in vec3 vUVL;
        uniform sampler2DArray uTex;
        uniform vec4 uTint;
        out vec4 FragColor;
        void main() {
            vec4 c = texture(uTex, vUVL) * uTint;
            if (c.a < 0.1) discard;
            FragColor = c;
        })";

    GLuint v = compileShader(GL_VERTEX_SHADER, vs);
    GLuint f = compileShader(GL_FRAGMENT_SHADER, fs);
    program = glCreateProgram();
    glAttachShader(program, v);
    glAttachShader(program, f);
    glLinkProgram(program);
    glDeleteShader(v);
    glDeleteShader(f);

    GLint ok = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetProgramInfoLog(program, sizeof(log), nullptr, log);
        std::cerr << "[Inventory] Icon shader link error: " << log << std::endl;
    }
    return program;
}

void resetUIState() {
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    uidraw::applyClip();          // the scissor test follows ui.push_clip (off when there's none)
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

// ---- Batched coloured triangles (rectangles and every shape) ----
// Vertices: x, y (clip space), r, g, b, a. Drawn together by flushBatch(): one upload and one
// draw call for a whole screen of rectangles instead of one per rectangle.
std::vector<float> s_batch;

void flushBatch() {
    if (s_batch.empty()) return;
    static GLuint vao = 0, vbo = 0;
    if (!vao) {
        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(2 * sizeof(float)));
        glEnableVertexAttribArray(1);
    }
    resetUIState();
    glUseProgram(s_ctx.uiProgram);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(s_batch.size() * sizeof(float)), s_batch.data(), GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(s_batch.size() / 6));
    glBindVertexArray(0);
    s_batch.clear();
}

// One vertex in window pixels
inline void vert(float x, float y, float r, float g, float b, float a) {
    const float sw = s_ctx.W, sh = s_ctx.H;
    s_batch.push_back((x / sw) * 2.0f - 1.0f);
    s_batch.push_back(1.0f - (y / sh) * 2.0f);
    s_batch.push_back(r); s_batch.push_back(g); s_batch.push_back(b); s_batch.push_back(a);
}
inline bool canDraw() { return s_ctx.W > 0 && s_ctx.H > 0 && s_ctx.uiProgram != 0; }
inline void afterAdd() {
    if (!s_ctx.active) flushBatch();                       // (C++ callers outside a Lua screen)
    else if (s_batch.size() > 6 * 60000) flushBatch();      // keep one upload reasonable
}

void triangle(float x1, float y1, float x2, float y2, float x3, float y3,
              float r, float g, float b, float a) {
    if (!canDraw()) return;
    vert(x1, y1, r, g, b, a); vert(x2, y2, r, g, b, a); vert(x3, y3, r, g, b, a);
    afterAdd();
}

void drawRect(float x, float y, float w, float h, float r, float g, float b, float a) {
    if (!canDraw() || w <= 0 || h <= 0) return;
    vert(x, y + h, r, g, b, a); vert(x + w, y + h, r, g, b, a); vert(x + w, y, r, g, b, a);
    vert(x, y + h, r, g, b, a); vert(x + w, y, r, g, b, a);     vert(x, y, r, g, b, a);
    afterAdd();
}

// A rectangle with a colour at each end (horizontal: left -> right, else top -> bottom)
void drawGradient(float x, float y, float w, float h, const float c1[4], const float c2[4], bool horizontal) {
    if (!canDraw() || w <= 0 || h <= 0) return;
    const float* tl = c1; const float* tr = horizontal ? c2 : c1;
    const float* bl = horizontal ? c1 : c2; const float* br = c2;
    vert(x, y + h, bl[0], bl[1], bl[2], bl[3]); vert(x + w, y + h, br[0], br[1], br[2], br[3]); vert(x + w, y, tr[0], tr[1], tr[2], tr[3]);
    vert(x, y + h, bl[0], bl[1], bl[2], bl[3]); vert(x + w, y, tr[0], tr[1], tr[2], tr[3]);     vert(x, y, tl[0], tl[1], tl[2], tl[3]);
    afterAdd();
}

void drawLine(float x1, float y1, float x2, float y2, float t, float r, float g, float b, float a) {
    const float dx = x2 - x1, dy = y2 - y1, len = std::sqrt(dx * dx + dy * dy);
    if (len <= 0.0001f || t <= 0) return;
    const float nx = -dy / len * t * 0.5f, ny = dx / len * t * 0.5f;
    triangle(x1 + nx, y1 + ny, x2 + nx, y2 + ny, x2 - nx, y2 - ny, r, g, b, a);
    triangle(x1 + nx, y1 + ny, x2 - nx, y2 - ny, x1 - nx, y1 - ny, r, g, b, a);
}

int circleSegments(float radius) { return std::clamp(static_cast<int>(radius * 0.8f), 12, 96); }

// A pie slice / full circle as a triangle fan
void drawArc(float cx, float cy, float radius, float from, float to, float r, float g, float b, float a, int segs = 0) {
    if (radius <= 0) return;
    if (segs <= 0) segs = std::max(3, static_cast<int>(circleSegments(radius) * std::fabs(to - from) / 6.2831853f));
    for (int i = 0; i < segs; ++i) {
        const float a0 = from + (to - from) * i / segs, a1 = from + (to - from) * (i + 1) / segs;
        triangle(cx, cy, cx + std::cos(a0) * radius, cy + std::sin(a0) * radius,
                 cx + std::cos(a1) * radius, cy + std::sin(a1) * radius, r, g, b, a);
    }
}

void drawRing(float cx, float cy, float radius, float t, float r, float g, float b, float a) {
    if (radius <= 0 || t <= 0) return;
    const float inner = std::max(0.0f, radius - t);
    const int segs = circleSegments(radius);
    for (int i = 0; i < segs; ++i) {
        const float a0 = 6.2831853f * i / segs, a1 = 6.2831853f * (i + 1) / segs;
        const float ox0 = std::cos(a0) * radius, oy0 = std::sin(a0) * radius, ox1 = std::cos(a1) * radius, oy1 = std::sin(a1) * radius;
        const float ix0 = std::cos(a0) * inner, iy0 = std::sin(a0) * inner, ix1 = std::cos(a1) * inner, iy1 = std::sin(a1) * inner;
        triangle(cx + ix0, cy + iy0, cx + ox0, cy + oy0, cx + ox1, cy + oy1, r, g, b, a);
        triangle(cx + ix0, cy + iy0, cx + ox1, cy + oy1, cx + ix1, cy + iy1, r, g, b, a);
    }
}

void drawRoundedRect(float x, float y, float w, float h, float rad, float r, float g, float b, float a) {
    if (w <= 0 || h <= 0) return;
    rad = std::clamp(rad, 0.0f, std::min(w, h) * 0.5f);
    if (rad < 0.5f) { drawRect(x, y, w, h, r, g, b, a); return; }
    drawRect(x + rad, y, w - 2 * rad, h, r, g, b, a);                 // middle column
    drawRect(x, y + rad, rad, h - 2 * rad, r, g, b, a);               // left and right strips
    drawRect(x + w - rad, y + rad, rad, h - 2 * rad, r, g, b, a);
    const float pi = 3.14159265f;
    drawArc(x + rad, y + rad, rad, pi, 1.5f * pi, r, g, b, a);             // corners
    drawArc(x + w - rad, y + rad, rad, 1.5f * pi, 2.0f * pi, r, g, b, a);
    drawArc(x + w - rad, y + h - rad, rad, 0.0f, 0.5f * pi, r, g, b, a);
    drawArc(x + rad, y + h - rad, rad, 0.5f * pi, pi, r, g, b, a);
}

void drawRectOutline(float x, float y, float w, float h, float t, float r, float g, float b, float a) {
    if (w <= 0 || h <= 0 || t <= 0) return;
    t = std::min(t, std::min(w, h) * 0.5f);
    drawRect(x, y, w, t, r, g, b, a);
    drawRect(x, y + h - t, w, t, r, g, b, a);
    drawRect(x, y + t, t, h - 2 * t, r, g, b, a);
    drawRect(x + w - t, y + t, t, h - 2 * t, r, g, b, a);
}

void drawTexturedQuad(const QuadCorner c[4], int layer, float r, float g, float b) {
    flushBatch();                 // rectangles drawn before stay underneath
    const float sw = s_ctx.W, sh = s_ctx.H;
    if (sw <= 0 || sh <= 0 || layer < 0 || g_TextureArrayID == 0) return;

    float L = static_cast<float>(layer);
    float v[4][5];
    for (int i = 0; i < 4; ++i) {
        v[i][0] = (c[i].x / sw) * 2.0f - 1.0f;
        v[i][1] = 1.0f - (c[i].y / sh) * 2.0f;
        v[i][2] = c[i].u;
        v[i][3] = c[i].v;
        v[i][4] = L;
    }
    const int idx[6] = { 0, 1, 2, 0, 2, 3 };
    float vertices[30];
    for (int i = 0; i < 6; ++i)
        for (int k = 0; k < 5; ++k)
            vertices[i * 5 + k] = v[idx[i]][k];

    static GLuint vao = 0, vbo = 0;
    if (!vao) {
        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(2 * sizeof(float)));
        glEnableVertexAttribArray(1);
    }

    GLuint prog = getIconProgram();
    resetUIState();
    glUseProgram(prog);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D_ARRAY, g_TextureArrayID);
    glUniform1i(glGetUniformLocation(prog, "uTex"), 0);
    glUniform4f(glGetUniformLocation(prog, "uTint"), r, g, b, 1.0f);

    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_DYNAMIC_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
}

void drawFace(const FaceDefinition& face, const glm::vec3& tint, const QuadCorner c[4], float shade) {
    if (face.baseLayer < 0) return;

    glm::vec3 base = face.tintBase ? tint : glm::vec3(1.0f);
    drawTexturedQuad(c, face.baseLayer, base.r * shade, base.g * shade, base.b * shade);

    if (face.overlayLayer >= 0) {
        glm::vec3 ov = face.tintOverlay ? tint : glm::vec3(1.0f);
        drawTexturedQuad(c, face.overlayLayer, ov.r * shade, ov.g * shade, ov.b * shade);
    }
}

void drawFlatIcon(const BlockProperty& block, float x, float y, float s) {
    const FaceDefinition* face = getIconFace(block);
    if (!face) return;
    QuadCorner c[4] = {
        { x,     y,     0.0f, 0.0f },
        { x + s, y,     1.0f, 0.0f },
        { x + s, y + s, 1.0f, 1.0f },
        { x,     y + s, 0.0f, 1.0f },
    };
    drawFace(*face, block.defaultTint, c, 1.0f);
}

// True isometric cube (matches the hotbar's 35.264 degree rotation)
void drawCubeIcon(const BlockProperty& block, float x, float y, float s) {
    const FaceDefinition* fallback = getIconFace(block);
    if (!fallback) return;

    auto pick = [&](int f) -> const FaceDefinition& {
        return block.faces[f].baseLayer >= 0 ? block.faces[f] : *fallback;
    };

    const float w    = s * 0.8660f;
    const float edge = w * 0.5774f;
    const float d    = w * 0.2887f;

    const float x0 = x + (s - w) * 0.5f;
    const float x1 = x0 + w;
    const float cx = x0 + w * 0.5f;

    const float yTop = y;
    const float yQ1  = y + d;
    const float yMid = y + 2.0f * d;
    const float yQ3  = yQ1 + edge;
    const float yBot = yMid + edge;

    QuadCorner top[4] = {
        { cx, yTop, 0.0f, 0.0f }, { x1, yQ1, 1.0f, 0.0f },
        { cx, yMid, 1.0f, 1.0f }, { x0, yQ1, 0.0f, 1.0f },
    };
    QuadCorner left[4] = {
        { x0, yQ1, 0.0f, 0.0f }, { cx, yMid, 1.0f, 0.0f },
        { cx, yBot, 1.0f, 1.0f }, { x0, yQ3, 0.0f, 1.0f },
    };
    QuadCorner right[4] = {
        { cx, yMid, 0.0f, 0.0f }, { x1, yQ1, 1.0f, 0.0f },
        { x1, yQ3, 1.0f, 1.0f }, { cx, yBot, 0.0f, 1.0f },
    };

    drawFace(pick(2), block.defaultTint, top,   1.0f);
    drawFace(pick(4), block.defaultTint, left,  0.8f);
    drawFace(pick(0), block.defaultTint, right, 0.6f);
}

// Purple/black checkerboard for blocks without a texture
void drawMissing(float x, float y, float s) {
    const float h = s * 0.5f;
    drawRect(x,     y,     h, h, 1, 0, 1, 1);
    drawRect(x + h, y,     h, h, 0, 0, 0, 1);
    drawRect(x,     y + h, h, h, 0, 0, 0, 1);
    drawRect(x + h, y + h, h, h, 1, 0, 1, 1);
}

void drawItemIcon(uint8_t id, float x, float y, float s) {
    if (id == 0) return;
    const auto& block = BlockRegistry::getInstance().getBlock(id);

    // inventory_image: flat picture, drawn as-is (no tint)
    if (block.inventoryLayer >= 0) {
        QuadCorner c[4] = {
            { x,     y,     0.0f, 0.0f },
            { x + s, y,     1.0f, 0.0f },
            { x + s, y + s, 1.0f, 1.0f },
            { x,     y + s, 0.0f, 1.0f },
        };
        drawTexturedQuad(c, block.inventoryLayer, 1.0f, 1.0f, 1.0f);
        return;
    }

    if (!getIconFace(block)) {
        const float inset = s * 0.15f;
        drawMissing(x + inset, y + inset, s - 2 * inset);
        return;
    }
    if (block.pattern == "cube") drawCubeIcon(block, x, y, s);
    else                         drawFlatIcon(block, x, y, s);
}

// ===========================================================================
// Lua helpers
// ===========================================================================

void reportLuaError(const char* fn, const std::string& msg) {
    // Only print when the error changes, so a broken script doesn't spam every frame
    std::string full = std::string(fn) + ": " + msg;
    if (full != s_lastLuaError) {
        std::cerr << "[Lua UI] " << full << std::endl;
        s_lastLuaError = full;
    }
}

// Calls a global Lua function if it exists. Returns the first return value (or nil).
template <typename... Args>
sol::object callLua(const char* fn, Args&&... args) {
    if (!s_lua) return sol::lua_nil;
    sol::protected_function f = (*s_lua)[fn];
    if (!f.valid()) return sol::make_object(s_lua->lua_state(), sol::lua_nil);

    sol::protected_function_result res = f(std::forward<Args>(args)...);
    if (!res.valid()) {
        sol::error err = res;
        reportLuaError(fn, err.what());
        return sol::make_object(s_lua->lua_state(), sol::lua_nil);
    }
    if (res.return_count() == 0) return sol::make_object(s_lua->lua_state(), sol::lua_nil);
    return res.get<sol::object>();
}

bool luaTruthy(const sol::object& o) {
    return o.valid() && o.get_type() != sol::type::lua_nil &&
           !(o.get_type() == sol::type::boolean && !o.as<bool>());
}

} // namespace

// ===========================================================================
// UiDraw (UiDraw.h): clipping, batching, per-frame input, cursor, clipboard
// ===========================================================================
namespace uidraw {
namespace {
struct ClipRect { float x, y, w, h; };
std::vector<ClipRect> s_clips;
std::vector<MouseEvent> s_mouse;
std::vector<KeyEvent> s_keys;
std::string s_typed;
double s_scrollX = 0.0, s_scrollY = 0.0;
std::string s_cursor;
double s_copyAt = -100.0, s_pasteAt = -100.0;
struct LastPress { double t = -10.0, x = 0, y = 0; int clicks = 0; } s_lastPress[8];
} // namespace

void applyClip() {
    if (s_clips.empty()) { glDisable(GL_SCISSOR_TEST); return; }
    GLint vp[4];
    glGetIntegerv(GL_VIEWPORT, vp);
    const ClipRect& c = s_clips.back();
    const int x = static_cast<int>(std::floor(c.x)), y = static_cast<int>(std::floor(c.y));
    const int w = std::max(0, static_cast<int>(std::ceil(c.x + c.w)) - x);
    const int h = std::max(0, static_cast<int>(std::ceil(c.y + c.h)) - y);
    glEnable(GL_SCISSOR_TEST);
    glScissor(x, vp[3] - (y + h), w, h);          // GL counts from the bottom
}
void pushClip(float x, float y, float w, float h) {
    flushBatch();
    ClipRect c{ x, y, std::max(0.0f, w), std::max(0.0f, h) };
    if (!s_clips.empty()) {                       // inside the current clip only
        const ClipRect& p = s_clips.back();
        const float x0 = std::max(c.x, p.x), y0 = std::max(c.y, p.y);
        const float x1 = std::min(c.x + c.w, p.x + p.w), y1 = std::min(c.y + c.h, p.y + p.h);
        c = { x0, y0, std::max(0.0f, x1 - x0), std::max(0.0f, y1 - y0) };
    }
    s_clips.push_back(c);
    applyClip();
}
void popClip() {
    flushBatch();
    if (!s_clips.empty()) s_clips.pop_back();
    applyClip();
}
void clearClips() { s_clips.clear(); applyClip(); }
void flush() { flushBatch(); }

void beginFrame() { s_mouse.clear(); s_keys.clear(); s_typed.clear(); s_scrollX = s_scrollY = 0.0; }
void addMouse(const MouseEvent& e0) {
    MouseEvent e = e0;
    if (e.pressed && e.button >= 0 && e.button < 8) {
        LastPress& lp = s_lastPress[e.button];
        const bool isNear = std::fabs(e.x - lp.x) <= 6.0 && std::fabs(e.y - lp.y) <= 6.0;
        const double now = glfwGetTime();
        e.clicks = (isNear && now - lp.t <= 0.5) ? std::min(lp.clicks + 1, 3) : 1;
        lp = { now, e.x, e.y, e.clicks };
    }
    if (s_mouse.size() < 256) s_mouse.push_back(e);
}
void addScroll(double dx, double dy) { s_scrollX += dx; s_scrollY += dy; }
void addChar(unsigned cp) {
    if (s_typed.size() > 4096 || cp < 32 || cp == 127 || cp > 0x10FFFF) return;
    if (cp < 0x80) s_typed += static_cast<char>(cp);
    else if (cp < 0x800) { s_typed += static_cast<char>(0xC0 | (cp >> 6)); s_typed += static_cast<char>(0x80 | (cp & 0x3F)); }
    else if (cp < 0x10000) { s_typed += static_cast<char>(0xE0 | (cp >> 12)); s_typed += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); s_typed += static_cast<char>(0x80 | (cp & 0x3F)); }
    else { s_typed += static_cast<char>(0xF0 | (cp >> 18)); s_typed += static_cast<char>(0x80 | ((cp >> 12) & 0x3F)); s_typed += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); s_typed += static_cast<char>(0x80 | (cp & 0x3F)); }
}
void addKey(int key, int action, int mods) { if (s_keys.size() < 256) s_keys.push_back({ key, action, mods }); }
const std::vector<MouseEvent>& mouseEvents() { return s_mouse; }
double scrollX() { return s_scrollX; }
double scrollY() { return s_scrollY; }
const std::string& typedText() { return s_typed; }
const std::vector<KeyEvent>& keyEvents() { return s_keys; }

void requestCursor(const std::string& name) { s_cursor = name; }
std::string takeCursorRequest() { std::string c; c.swap(s_cursor); return c; }

void noteShortcut(int key, int mods, double now) {
    const bool ctrl = (mods & (GLFW_MOD_CONTROL | GLFW_MOD_SUPER)) != 0, shift = (mods & GLFW_MOD_SHIFT) != 0;
    if ((ctrl && (key == GLFW_KEY_C || key == GLFW_KEY_X)) || (ctrl && key == GLFW_KEY_INSERT)) s_copyAt = now;
    if ((ctrl && key == GLFW_KEY_V) || (shift && key == GLFW_KEY_INSERT)) s_pasteAt = now;
}
bool mayReadClipboard(double now) { return now - s_pasteAt <= 1.0; }
bool mayWriteClipboard(double now) { return now - s_copyAt <= 1.0; }
} // namespace uidraw

// ===========================================================================
// InventorySystem
// ===========================================================================

void InventorySystem::toggleCreative() {
    creativeOpen = !creativeOpen;
}

void InventorySystem::setCreativeOpen(bool open) {
    creativeOpen = open;
}

bool InventorySystem::isCreativeOpen() const {
    if (g_Hotbar && g_Hotbar->isCreativeOpen) return true;
    return creativeOpen;
}

// Every creative-visible block name.
std::vector<std::string> InventorySystem::getCurrentItems() {
    std::vector<std::string> out;
    auto& reg = BlockRegistry::getInstance();
    for (int id = 1; id < 255; ++id) {
        const auto& b = reg.getBlock(static_cast<uint8_t>(id));
        if (!b.name.empty() && b.name != "air" && b.in_creative) out.push_back(b.name);
    }
    return out;
}

std::string blockModOf(const std::string& name);   // main.cpp

void InventorySystem::registerLuaBindings(sol::state& lua) {
    s_lua = &lua;

    // ---------------- ui.* ----------------
    sol::table ui = lua["ui"].get_or_create<sol::table>();

    // ui.draw_rect(x, y, w, h, r, g, b [, a])
    ui.set_function("draw_rect", [](float x, float y, float w, float h,
                                     float r, float g, float b, sol::optional<float> a) {
        if (!s_ctx.active) return;
        drawRect(x, y, w, h, r, g, b, a.value_or(1.0f));
    });

    // ---- Shapes (all batched; colours 0..1, alpha optional) ----
    // ui.draw_rect_outline(x, y, w, h, thickness, r, g, b [, a])
    ui.set_function("draw_rect_outline", [](float x, float y, float w, float h, float t, float r, float g, float b,
                                            sol::optional<float> a) {
        if (s_ctx.active) drawRectOutline(x, y, w, h, t, r, g, b, a.value_or(1.0f));
    });
    // ui.draw_rounded_rect(x, y, w, h, radius, r, g, b [, a])
    ui.set_function("draw_rounded_rect", [](float x, float y, float w, float h, float rad, float r, float g, float b,
                                            sol::optional<float> a) {
        if (s_ctx.active) drawRoundedRect(x, y, w, h, rad, r, g, b, a.value_or(1.0f));
    });
    // ui.draw_line(x1, y1, x2, y2, thickness, r, g, b [, a])
    ui.set_function("draw_line", [](float x1, float y1, float x2, float y2, float t, float r, float g, float b,
                                    sol::optional<float> a) {
        if (s_ctx.active) drawLine(x1, y1, x2, y2, t, r, g, b, a.value_or(1.0f));
    });
    // ui.draw_circle(cx, cy, radius, r, g, b [, a])
    ui.set_function("draw_circle", [](float cx, float cy, float rad, float r, float g, float b, sol::optional<float> a) {
        if (s_ctx.active) drawArc(cx, cy, rad, 0.0f, 6.2831853f, r, g, b, a.value_or(1.0f));
    });
    // ui.draw_ring(cx, cy, radius, thickness, r, g, b [, a]) - a circle's outline
    ui.set_function("draw_ring", [](float cx, float cy, float rad, float t, float r, float g, float b, sol::optional<float> a) {
        if (s_ctx.active) drawRing(cx, cy, rad, t, r, g, b, a.value_or(1.0f));
    });
    // ui.draw_pie(cx, cy, radius, from_degrees, to_degrees, r, g, b [, a]) - 0 = right, clockwise
    ui.set_function("draw_pie", [](float cx, float cy, float rad, float from, float to, float r, float g, float b,
                                   sol::optional<float> a) {
        if (s_ctx.active) drawArc(cx, cy, rad, from * 0.01745329f, to * 0.01745329f, r, g, b, a.value_or(1.0f));
    });
    // ui.draw_triangle(x1, y1, x2, y2, x3, y3, r, g, b [, a])
    ui.set_function("draw_triangle", [](float x1, float y1, float x2, float y2, float x3, float y3,
                                        float r, float g, float b, sol::optional<float> a) {
        if (s_ctx.active) triangle(x1, y1, x2, y2, x3, y3, r, g, b, a.value_or(1.0f));
    });
    // ui.draw_polygon({ x1, y1, x2, y2, ... }, r, g, b [, a]) - a convex shape (fan from the first point)
    ui.set_function("draw_polygon", [](sol::table pts, float r, float g, float b, sol::optional<float> a) {
        if (!s_ctx.active) return;
        std::vector<float> p;
        for (size_t i = 1; i <= pts.size() && p.size() < 2048; ++i) p.push_back(pts.get_or(static_cast<int>(i), 0.0f));
        for (size_t i = 2; i + 3 < p.size(); i += 2) {
            triangle(p[0], p[1], p[i], p[i + 1], p[i + 2], p[i + 3], r, g, b, a.value_or(1.0f));
        }
    });
    // ui.draw_gradient(x, y, w, h, { r, g, b [, a] }, { r, g, b [, a] } [, "horizontal"]) - top to bottom by default
    ui.set_function("draw_gradient", [](float x, float y, float w, float h, sol::table c1t, sol::table c2t,
                                        sol::optional<std::string> dir) {
        if (!s_ctx.active) return;
        const float c1[4] = { c1t.get_or(1, 1.0f), c1t.get_or(2, 1.0f), c1t.get_or(3, 1.0f), c1t.get_or(4, 1.0f) };
        const float c2[4] = { c2t.get_or(1, 1.0f), c2t.get_or(2, 1.0f), c2t.get_or(3, 1.0f), c2t.get_or(4, 1.0f) };
        drawGradient(x, y, w, h, c1, c2, dir && *dir == "horizontal");
    });

    // ---- Clipping: only draw inside a rectangle (nested clips intersect) ----
    ui.set_function("push_clip", [](float x, float y, float w, float h) { if (s_ctx.active) uidraw::pushClip(x, y, w, h); });
    ui.set_function("pop_clip", []() { if (s_ctx.active) uidraw::popClip(); });

    // ---- Input of this frame (works in every screen: menus, HUD, in-game screens) ----
    // ui.get_mouse_events() -> { { button = "left"|"right"|"middle", pressed, released, x, y,
    //                              clicks (2 = double-click), shift, ctrl, alt }, ... }
    ui.set_function("get_mouse_events", [](sol::this_state ts) {
        sol::state_view lv(ts);
        sol::table out = lv.create_table();
        int i = 1;
        for (const auto& e : uidraw::mouseEvents()) {
            sol::table t = lv.create_table();
            t["button"] = e.button == 0 ? "left" : e.button == 1 ? "right" : e.button == 2 ? "middle" : "other";
            t["pressed"] = e.pressed;
            t["released"] = !e.pressed;
            t["x"] = e.x; t["y"] = e.y;
            t["clicks"] = e.clicks;
            t["shift"] = (e.mods & GLFW_MOD_SHIFT) != 0;
            t["ctrl"] = (e.mods & (GLFW_MOD_CONTROL | GLFW_MOD_SUPER)) != 0;
            t["alt"] = (e.mods & GLFW_MOD_ALT) != 0;
            out[i++] = t;
        }
        return out;
    });
    // ui.get_scroll() -> dx, dy: the mouse wheel this frame (dy > 0 = up)
    ui.set_function("get_scroll", []() { return std::make_tuple(uidraw::scrollX(), uidraw::scrollY()); });
    // ui.get_typed_text() -> what was typed this frame (UTF-8; keyboard layouts and accents handled)
    ui.set_function("get_typed_text", []() { return uidraw::typedText(); });
    // ui.get_key_events() -> { { key, action = "press"|"release"|"repeat", shift, ctrl, alt }, ... }
    ui.set_function("get_key_events", [](sol::this_state ts) {
        sol::state_view lv(ts);
        sol::table out = lv.create_table();
        int i = 1;
        for (const auto& e : uidraw::keyEvents()) {
            sol::table t = lv.create_table();
            t["key"] = e.key;
            t["action"] = e.action == GLFW_PRESS ? "press" : e.action == GLFW_RELEASE ? "release" : "repeat";
            t["shift"] = (e.mods & GLFW_MOD_SHIFT) != 0;
            t["ctrl"] = (e.mods & (GLFW_MOD_CONTROL | GLFW_MOD_SUPER)) != 0;
            t["alt"] = (e.mods & GLFW_MOD_ALT) != 0;
            out[i++] = t;
        }
        return out;
    });
    // ui.is_key_down(key), ui.is_mouse_down("left"|"right"|"middle")
    ui.set_function("is_key_down", [](int key) {
        GLFWwindow* w = glfwGetCurrentContext();
        return w && key > 0 && key <= GLFW_KEY_LAST && glfwGetKey(w, key) == GLFW_PRESS;
    });
    ui.set_function("is_mouse_down", [](sol::object which) {
        GLFWwindow* w = glfwGetCurrentContext();
        if (!w) return false;
        int b = 0;
        if (which.is<std::string>()) { const auto n = which.as<std::string>(); b = n == "right" ? 1 : n == "middle" ? 2 : 0; }
        else if (which.is<int>()) b = which.as<int>();
        return b >= 0 && b <= GLFW_MOUSE_BUTTON_LAST && glfwGetMouseButton(w, b) == GLFW_PRESS;
    });
    // ui.set_cursor("arrow"|"hand"|"text"|"crosshair"|"move"|"resize_h"|"resize_v"|"resize_nwse"|"resize_nesw"|"not_allowed")
    //   for this frame (ask again every frame you want it)
    ui.set_function("set_cursor", [](const std::string& name) { uidraw::requestCursor(name); });
    // ui.get_clipboard() -> text, or nil + why: only right after the player pressed Ctrl+V
    ui.set_function("get_clipboard", [](sol::this_state ts) {
        sol::state_view lv(ts);
        GLFWwindow* w = glfwGetCurrentContext();
        if (!w || !uidraw::mayReadClipboard(glfwGetTime())) {
            return std::make_tuple(sol::make_object(lv, sol::lua_nil),
                                   sol::make_object(lv, "the clipboard can only be read right after the player presses Ctrl+V"));
        }
        const char* text = glfwGetClipboardString(w);
        std::string out = text ? text : "";
        if (out.size() > 65536) out.resize(65536);
        return std::make_tuple(sol::make_object(lv, out), sol::make_object(lv, sol::lua_nil));
    });
    // ui.set_clipboard(text) -> true, or false + why: only right after the player pressed Ctrl+C / Ctrl+X
    ui.set_function("set_clipboard", [](const std::string& text, sol::this_state ts) {
        sol::state_view lv(ts);
        GLFWwindow* w = glfwGetCurrentContext();
        if (!w || !uidraw::mayWriteClipboard(glfwGetTime())) {
            return std::make_tuple(false, sol::make_object(lv, "the clipboard can only be set right after the player presses Ctrl+C or Ctrl+X"));
        }
        std::string t = text.substr(0, 65536);
        t.erase(std::remove(t.begin(), t.end(), '\0'), t.end());
        glfwSetClipboardString(w, t.c_str());
        return std::make_tuple(true, sol::make_object(lv, sol::lua_nil));
    });

    // ui.draw_text(text, x, y, scale, r, g, b [, a])
    // NOTE: main.cpp replaces this with a version that uses the real font shader.
    ui.set_function("draw_text", [](const std::string& text, float x, float y, float scale,
                                     float r, float g, float b, sol::optional<float> a) {
        if (!s_ctx.active || !g_FontRenderer) return;
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
        Dimensions2D winSize{ static_cast<int>(s_ctx.W), static_cast<int>(s_ctx.H) };
        GLuint prog = kTextUsesTexturedProgram ? s_ctx.texturedProgram : s_ctx.uiProgram;
        g_FontRenderer->renderText(prog, text, x, y, scale, scale,
                                   glm::vec4(r, g, b, a.value_or(1.0f)), winSize);
    });

    // ui.draw_block_icon(name, x, y, size) - 3D cube or flat sprite, picked automatically
    ui.set_function("draw_block_icon", [](const std::string& name, float x, float y, float size) {
        if (!s_ctx.active || name.empty()) return;
        drawItemIcon(BlockRegistry::getInstance().getBlockId(name), x, y, size);
    });

    // ui.get_all_blocks() -> { "stone", "dirt", ... }  (only blocks with in_creative = true)
    ui.set_function("get_all_blocks", [this]() {
        return sol::as_table(getCurrentItems());
    });

    // ui.get_block_info(name) -> { id, name, pattern, has_texture, in_creative, inventory_image,
    //                              item, tool_type, tool, light, walkable, transparent } or nil
    //   item = has an inventory picture (never placed); tool_type = what it is as a tool;
    //   tool = what digs it; light = brightest colour it gives off (0..15, if set)
    ui.set_function("get_block_info", [](const std::string& name, sol::this_state ts) -> sol::object {
        sol::state_view lv(ts);
        auto& reg = BlockRegistry::getInstance();
        uint8_t id = reg.getBlockId(name);
        if (id == 0) return sol::make_object(lv, sol::lua_nil);
        const auto& b = reg.getBlock(id);
        if (b.name.empty()) return sol::make_object(lv, sol::lua_nil);

        sol::table t = lv.create_table();
        t["id"]          = static_cast<int>(id);
        t["name"]        = b.name;
        t["pattern"]     = b.pattern;
        t["has_texture"] = (getIconFace(b) != nullptr) || b.inventoryLayer >= 0;
        t["inventory_image"] = b.inventoryImage;
        t["in_creative"] = b.in_creative;
        t["item"]        = !b.inventoryImage.empty();
        t["tool_type"]   = b.toolType;
        t["tool"]        = b.tool;
        t["light"]       = static_cast<int>(std::max(b.lightEmit[0], std::max(b.lightEmit[1], b.lightEmit[2])));
        t["walkable"]    = b.isWalkable;
        t["transparent"] = b.isTransparent;
        t["mod"]         = blockModOf(b.name);    // the mod that registered it ("" = the engine)
        return t;
    });

    // ui.get_mouse() -> x, y, left_down, right_down
    // NOTE: main.cpp replaces this with a version that scales to framebuffer pixels.
    ui.set_function("get_mouse", []() {
        double mx = -1.0, my = -1.0;
        bool left = false, right = false;
        if (s_window) {
            glfwGetCursorPos(s_window, &mx, &my);
            left  = glfwGetMouseButton(s_window, GLFW_MOUSE_BUTTON_LEFT)  == GLFW_PRESS;
            right = glfwGetMouseButton(s_window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
        }
        return std::make_tuple(mx, my, left, right);
    });

    // ui.get_time() -> seconds since start (for blinking cursors, animations)
    ui.set_function("get_time", []() { return glfwGetTime(); });

    // Key / action constants for handle_creative_key
    sol::table keys = lua.create_table();
    keys["BACKSPACE"] = GLFW_KEY_BACKSPACE;
    keys["ESCAPE"]    = GLFW_KEY_ESCAPE;
    keys["ENTER"]     = GLFW_KEY_ENTER;
    keys["KP_ENTER"]  = GLFW_KEY_KP_ENTER;
    keys["DELETE"]    = GLFW_KEY_DELETE;
    keys["UP"]        = GLFW_KEY_UP;
    keys["DOWN"]      = GLFW_KEY_DOWN;
    keys["PAGE_UP"]   = GLFW_KEY_PAGE_UP;
    keys["PAGE_DOWN"] = GLFW_KEY_PAGE_DOWN;
    keys["LEFT"]      = GLFW_KEY_LEFT;
    keys["RIGHT"]     = GLFW_KEY_RIGHT;
    keys["SPACE"]     = GLFW_KEY_SPACE;
    keys["TAB"]       = GLFW_KEY_TAB;
    keys["HOME"]      = GLFW_KEY_HOME;
    keys["END"]       = GLFW_KEY_END;
    for (char c = 'A'; c <= 'Z'; ++c) keys[std::string(1, c)] = static_cast<int>(c);       // ui.keys.M ...
    for (char c = '0'; c <= '9'; ++c) keys[std::string(1, c)] = static_cast<int>(c);       // ui.keys["1"] ...
    for (int i = 1; i <= 12; ++i) keys["F" + std::to_string(i)] = GLFW_KEY_F1 + i - 1;      // ui.keys.F1 ...
    // Everything else on a keyboard
    const std::pair<const char*, int> more[] = {
        {"INSERT", GLFW_KEY_INSERT}, {"CAPS_LOCK", GLFW_KEY_CAPS_LOCK}, {"NUM_LOCK", GLFW_KEY_NUM_LOCK},
        {"SCROLL_LOCK", GLFW_KEY_SCROLL_LOCK}, {"PRINT_SCREEN", GLFW_KEY_PRINT_SCREEN}, {"PAUSE", GLFW_KEY_PAUSE},
        {"MENU", GLFW_KEY_MENU}, {"APOSTROPHE", GLFW_KEY_APOSTROPHE}, {"COMMA", GLFW_KEY_COMMA}, {"MINUS", GLFW_KEY_MINUS},
        {"PERIOD", GLFW_KEY_PERIOD}, {"SLASH", GLFW_KEY_SLASH}, {"SEMICOLON", GLFW_KEY_SEMICOLON}, {"EQUAL", GLFW_KEY_EQUAL},
        {"LEFT_BRACKET", GLFW_KEY_LEFT_BRACKET}, {"BACKSLASH", GLFW_KEY_BACKSLASH}, {"RIGHT_BRACKET", GLFW_KEY_RIGHT_BRACKET},
        {"GRAVE_ACCENT", GLFW_KEY_GRAVE_ACCENT}, {"LEFT_SHIFT", GLFW_KEY_LEFT_SHIFT}, {"RIGHT_SHIFT", GLFW_KEY_RIGHT_SHIFT},
        {"LEFT_CONTROL", GLFW_KEY_LEFT_CONTROL}, {"RIGHT_CONTROL", GLFW_KEY_RIGHT_CONTROL}, {"LEFT_ALT", GLFW_KEY_LEFT_ALT},
        {"RIGHT_ALT", GLFW_KEY_RIGHT_ALT}, {"LEFT_SUPER", GLFW_KEY_LEFT_SUPER}, {"RIGHT_SUPER", GLFW_KEY_RIGHT_SUPER},
        {"KP_DECIMAL", GLFW_KEY_KP_DECIMAL}, {"KP_DIVIDE", GLFW_KEY_KP_DIVIDE}, {"KP_MULTIPLY", GLFW_KEY_KP_MULTIPLY},
        {"KP_SUBTRACT", GLFW_KEY_KP_SUBTRACT}, {"KP_ADD", GLFW_KEY_KP_ADD}, {"KP_EQUAL", GLFW_KEY_KP_EQUAL},
    };
    for (const auto& [name, code] : more) keys[name] = code;
    for (int i = 0; i <= 9; ++i) keys["KP_" + std::to_string(i)] = GLFW_KEY_KP_0 + i;           // ui.keys.KP_5 ...
    for (int i = 13; i <= 25; ++i) keys["F" + std::to_string(i)] = GLFW_KEY_F1 + i - 1;
    ui["keys"] = keys;
    // ui.mods: the bits in a key hook's `mods` (if mods & ui.mods.CONTROL ~= 0 then ...)
    sol::table modBits = lua.create_table();
    modBits["SHIFT"] = GLFW_MOD_SHIFT; modBits["CONTROL"] = GLFW_MOD_CONTROL;
    modBits["ALT"] = GLFW_MOD_ALT; modBits["SUPER"] = GLFW_MOD_SUPER;
    ui["mods"] = modBits;

    sol::table actions = lua.create_table();
    actions["PRESS"]   = GLFW_PRESS;
    actions["RELEASE"] = GLFW_RELEASE;
    actions["REPEAT"]  = GLFW_REPEAT;
    ui["actions"] = actions;

    // NOTE: the hotbar.* table is defined in main.cpp (1-based slots).
}

void InventorySystem::renderCreativeInventory(GLFWwindow* window, sol::state& lua, float width, float height,
                                              GLuint uiProgram, GLuint texturedUIProgram) {
    s_lua = &lua;
    s_window = window;

    const bool open = isCreativeOpen();
    if (!open) {
        if (s_wasOpen) callLua("on_creative_closed");
        s_wasOpen = false;
        return;
    }
    if (!s_wasOpen) callLua("on_creative_opened");
    s_wasOpen = true;

    renderLuaScreen(window, lua, "render_creative_inventory", width, height, uiProgram, texturedUIProgram);
}

void InventorySystem::renderLuaScreen(GLFWwindow* window, sol::state& lua, const char* luaFunction,
                                      float width, float height, GLuint uiProgram, GLuint texturedUIProgram) {
    s_lua = &lua;
    s_window = window;

    // A missing draw function shows up as a blank screen - say why in the console (once)
    {
        sol::object fn = lua[luaFunction];
        if (!fn.valid() || fn.get_type() != sol::type::function) {
            reportLuaError(luaFunction, "function not found - is the .lua file that defines it being loaded? "
                                        "(Files in a folder with an init.lua only run if init.lua loads them.)");
            return;
        }
    }

    GLStateGuard stateGuard;   // restores all GL state when this function returns

    s_ctx.active = true;
    s_ctx.uiProgram = uiProgram;
    s_ctx.texturedProgram = texturedUIProgram;
    s_ctx.W = width;
    s_ctx.H = height;
    uidraw::clearClips();                 // every screen starts unclipped

    callLua(luaFunction, width, height);

    flushBatch();                         // what's still collected, then forget its clips
    uidraw::clearClips();
    s_ctx.active = false;
}

void InventorySystem::handleCreativeClick(double mx, double my, float width, float height,
                                          const std::string& button, bool shift) {
    if (!isCreativeOpen()) return;
    callLua("handle_creative_click", mx, my, width, height, button, shift);
}

void InventorySystem::handleCreativeScroll(double yoffset) {
    if (!isCreativeOpen()) return;
    callLua("handle_creative_scroll", yoffset);
}

bool InventorySystem::handleCreativeKey(int key, int action, int mods) {
    if (!isCreativeOpen()) return false;
    return luaTruthy(callLua("handle_creative_key", key, action, mods));
}

bool InventorySystem::handleCreativeChar(unsigned int codepoint) {
    if (!isCreativeOpen()) return false;
    std::string ch;
    if (codepoint < 128) ch.push_back(static_cast<char>(codepoint));
    return luaTruthy(callLua("handle_creative_char", static_cast<int>(codepoint), ch));
}
