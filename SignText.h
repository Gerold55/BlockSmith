#pragma once
// Text on signs (and any block with text whose pattern is "sign" / "wall_sign"), drawn in the
// world with the game's font: each side's lines centred on that face of the board (SignShape.h),
// squeezed to fit when a line is wider than the board. Lit like the block it's on - or, glowing,
// at full brightness with a darker outline. Built from the loaded chunks near the camera each
// frame (signs are few), so a change shows at once without meshing the chunk again.

#include "ChunkData.h"
#include "FontRenderer.h"
#include "Light.h"
#include "SignShape.h"
#include "World.h"
#include "Block.h"
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

class SignTextRenderer {
public:
    static constexpr int kSlots = 4;          // a board holds 4 lines comfortably (more get smaller)
    static constexpr float kDrawDistance = 48.0f;

    void draw(const World& world, const FontRenderer& font, const glm::mat4& viewProj, const glm::vec3& eye,
              float daylight) {
        if (!font.ready()) return;
        if (!m_program && !init()) return;
        m_verts.clear();
        auto& reg = BlockRegistry::getInstance();
        const int r = static_cast<int>(std::ceil(kDrawDistance / CHUNK_WIDTH));
        const int ecx = static_cast<int>(std::floor(eye.x / CHUNK_WIDTH)), ecz = static_cast<int>(std::floor(eye.z / CHUNK_WIDTH));
        for (int cx = ecx - r; cx <= ecx + r; ++cx) {
            for (int cz = ecz - r; cz <= ecz + r; ++cz) {
                const ChunkData* c = world.chunkData({cx, cz});
                if (!c || c->allTexts().empty()) continue;
                for (const auto& [key, text] : c->allTexts()) {
                    int lx, ly, lz;
                    ChunkData::textPos(key, lx, ly, lz);
                    const uint8_t id = c->getLocalBlock(lx, ly, lz);
                    const BlockProperty& p = reg.getBlock(id);
                    if (p.pattern != "sign" && p.pattern != "wall_sign") continue;
                    const glm::vec3 cell(cx * CHUNK_WIDTH + lx, ly, cz * CHUNK_WIDTH + lz);
                    if (glm::distance(cell + glm::vec3(0.5f), eye) > kDrawDistance) continue;
                    const sign::Board b = sign::board(p.pattern == "wall_sign", c->getLocalMetadata(lx, ly, lz));
                    // the light there (sunlight dimmed by the time of day, or the block light)
                    float bright = 1.0f;
                    if (world.lighting()) {
                        const uint16_t l = world.getLightAt(glm::ivec3(cell + b.center + b.normal * 0.6f));
                        const float sky = light::sky(l) / 15.0f * daylight;
                        const float blk = std::max({ light::red(l), light::green(l), light::blue(l) }) / 15.0f;
                        bright = std::max(0.08f, std::max(sky, blk));
                    }
                    addSide(font, text.front, cell, b, b.normal, b.right, bright);
                    addSide(font, text.back, cell, b, -b.normal, -b.right, bright);
                }
            }
        }
        if (m_verts.empty()) return;
        glUseProgram(m_program);
        glUniformMatrix4fv(glGetUniformLocation(m_program, "viewProj"), 1, GL_FALSE, glm::value_ptr(viewProj));
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, font.atlasTexture());
        glUniform1i(glGetUniformLocation(m_program, "atlas"), 0);
        glBindVertexArray(m_vao);
        glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(m_verts.size() * sizeof(float)), m_verts.data(), GL_STREAM_DRAW);
        glEnable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(-1.0f, -2.0f);
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(m_verts.size() / 9));
        glDisable(GL_POLYGON_OFFSET_FILL);
        glBindVertexArray(0);
    }

private:
    GLuint m_program = 0, m_vao = 0, m_vbo = 0;
    std::vector<float> m_verts;              // x y z, u v, r g b a
    std::vector<FontRenderer::GlyphQuad> m_glyphs;

    bool init() {
        const char* vs = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec2 aUV;
layout (location = 2) in vec4 aColor;
uniform mat4 viewProj;
out vec2 uv;
out vec4 color;
void main() { uv = aUV; color = aColor; gl_Position = viewProj * vec4(aPos, 1.0); }
)";
        const char* fs = R"(
#version 330 core
in vec2 uv;
in vec4 color;
out vec4 FragColor;
uniform sampler2D atlas;
void main() {
    if (texture(atlas, uv).r < 0.45) discard;
    FragColor = color;
}
)";
        auto compile = [](GLenum type, const char* src) {
            GLuint s = glCreateShader(type);
            glShaderSource(s, 1, &src, nullptr);
            glCompileShader(s);
            GLint ok = 0;
            glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
            return ok ? s : 0u;
        };
        const GLuint v = compile(GL_VERTEX_SHADER, vs), f = compile(GL_FRAGMENT_SHADER, fs);
        if (!v || !f) return false;
        m_program = glCreateProgram();
        glAttachShader(m_program, v);
        glAttachShader(m_program, f);
        glLinkProgram(m_program);
        glDeleteShader(v);
        glDeleteShader(f);
        glGenVertexArrays(1, &m_vao);
        glGenBuffers(1, &m_vbo);
        glBindVertexArray(m_vao);
        glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 9 * sizeof(float), reinterpret_cast<void*>(0));
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 9 * sizeof(float), reinterpret_cast<void*>(3 * sizeof(float)));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, 9 * sizeof(float), reinterpret_cast<void*>(5 * sizeof(float)));
        glEnableVertexAttribArray(2);
        glBindVertexArray(0);
        return true;
    }

    void addSide(const FontRenderer& font, const BlockTextSide& side, const glm::vec3& cell, const sign::Board& b,
                 const glm::vec3& normal, const glm::vec3& right, float bright) {
        if (side.empty()) return;
        const int slots = std::max(kSlots, static_cast<int>(side.lines.size()));
        const float areaH = sign::kHeight * 0.86f, areaW = sign::kWidth * 0.9f;
        const float lineH = areaH / slots;
        const float scale = lineH * 0.95f / FontRenderer::kPixelHeight;   // font pixels -> blocks
        const glm::vec3 base = cell + b.center + normal * (sign::kThickness * 0.5f + 0.004f);
        glm::vec3 col(((side.color >> 16) & 255) / 255.0f, ((side.color >> 8) & 255) / 255.0f, (side.color & 255) / 255.0f);
        glm::vec3 outline(0.0f);
        if (side.glowing) {
            // full brightness; an outline in a darker shade (black text: a pale one)
            outline = (side.color == 0) ? glm::vec3(0.94f, 0.92f, 0.8f) : col * 0.4f;
        } else {
            col *= bright;
        }
        // (a few more pixels down so the letters sit in the middle of their line)
        for (int i = 0; i < static_cast<int>(side.lines.size()); ++i) {
            const std::string& line = side.lines[i];
            if (line.empty()) continue;
            const float widthPx = font.layout(line, m_glyphs);
            if (m_glyphs.empty()) continue;
            float squeeze = 1.0f;
            if (widthPx * scale > areaW) squeeze = areaW / (widthPx * scale);
            const float startX = -widthPx * scale * squeeze * 0.5f;
            const float baseline = areaH * 0.5f - lineH * (i + 1) + lineH * 0.22f;
            if (side.glowing) {
                const float o = lineH * 0.06f;
                const float offs[4][2] = { { o, 0 }, { -o, 0 }, { 0, o }, { 0, -o } };
                for (const auto& d : offs) emitLine(base + normal * 0.0005f, right, startX + d[0], baseline + d[1], scale, squeeze, outline);
                emitLine(base + normal * 0.0015f, right, startX, baseline, scale, squeeze, col);
            } else {
                emitLine(base, right, startX, baseline, scale, squeeze, col);
            }
        }
    }

    void emitLine(const glm::vec3& base, const glm::vec3& right, float startX, float baseline, float scale,
                  float squeeze, const glm::vec3& col) {
        const glm::vec3 up(0, 1, 0);
        for (const auto& g : m_glyphs) {
            const float x0 = startX + g.x0 * scale * squeeze, x1 = startX + g.x1 * scale * squeeze;
            const float y0 = baseline - g.y0 * scale, y1 = baseline - g.y1 * scale;   // (font y is downwards)
            const glm::vec3 p00 = base + right * x0 + up * y1, p10 = base + right * x1 + up * y1;
            const glm::vec3 p11 = base + right * x1 + up * y0, p01 = base + right * x0 + up * y0;
            auto put = [&](const glm::vec3& p, float u, float v) {
                m_verts.insert(m_verts.end(), { p.x, p.y, p.z, u, v, col.r, col.g, col.b, 1.0f });
            };
            put(p00, g.s0, g.t1); put(p10, g.s1, g.t1); put(p11, g.s1, g.t0);
            put(p00, g.s0, g.t1); put(p11, g.s1, g.t0); put(p01, g.s0, g.t0);
        }
    }
};
