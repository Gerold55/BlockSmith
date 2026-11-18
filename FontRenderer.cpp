#include "FontRenderer.h"
#include <iostream>
#include <fstream>
#include <vector>
#include <filesystem>
#include <algorithm>
#include <iterator>

#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace fs = std::filesystem;

namespace {
// Next character of a UTF-8 string as an index into the baked characters (0 = space),
// or the index of '?' for characters the font texture doesn't have.
int nextGlyph(std::string_view text, size_t& i) {
    const unsigned char c = static_cast<unsigned char>(text[i++]);
    uint32_t cp = c;
    if (c >= 0x80) {
        int extra = (c >= 0xF0) ? 3 : (c >= 0xE0) ? 2 : (c >= 0xC0) ? 1 : 0;
        cp = (extra == 3) ? (c & 0x07) : (extra == 2) ? (c & 0x0F) : (c & 0x1F);
        if (extra == 0) cp = '?';   // stray continuation byte
        for (; extra > 0 && i < text.size(); --extra) {
            const unsigned char cc = static_cast<unsigned char>(text[i]);
            if ((cc & 0xC0) != 0x80) { cp = '?'; break; }
            cp = (cp << 6) | (cc & 0x3F);
            ++i;
        }
    }
    if (cp < 32) return -1;                                    // control characters: skip
    if (cp >= static_cast<uint32_t>(FontRenderer::kFirstChar + FontRenderer::kNumChars)) cp = '?';
    if (cp >= 127 && cp < 160) cp = '?';                       // not printable
    return static_cast<int>(cp) - FontRenderer::kFirstChar;
}
}  // namespace

FontRenderer::FontRenderer(Dimensions2D resolution)
    : baseResolution{resolution}
{
}

FontRenderer::~FontRenderer() {
    if (vao != 0) glDeleteVertexArrays(1, &vao);
    if (vbo != 0) glDeleteBuffers(1, &vbo);
    if (fontTextureID != 0) glDeleteTextures(1, &fontTextureID);
}

std::vector<std::string> FontRenderer::systemFonts() {
#if defined(_WIN32)
    return { "C:/Windows/Fonts/arial.ttf", "C:/Windows/Fonts/segoeui.ttf", "C:/Windows/Fonts/tahoma.ttf" };
#elif defined(__APPLE__)
    return { "/Library/Fonts/Arial.ttf", "/System/Library/Fonts/Supplemental/Arial.ttf",
             "/System/Library/Fonts/Helvetica.ttc" };
#else
    return { "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
             "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
             "/usr/share/fonts/TTF/DejaVuSans.ttf",
             "/usr/share/fonts/dejavu/DejaVuSans.ttf",
             "/usr/share/fonts/truetype/freefont/FreeSans.ttf" };
#endif
}

namespace {
// The font file's bytes, or empty if it can't be read
std::vector<unsigned char> readFontFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return {};
    const std::streamsize size = file.tellg();
    if (size <= 0) return {};
    file.seekg(0, std::ios::beg);
    std::vector<unsigned char> data(static_cast<size_t>(size));
    if (!file.read(reinterpret_cast<char*>(data.data()), size)) return {};
    return data;
}
}  // namespace

bool FontRenderer::init(const std::vector<std::string>& fonts) {
    // The largest atlas the GPU accepts (every OpenGL 3.3 GPU takes at least 1024; capped at 4096)
    GLint maxTex = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTex);
    const int maxSize = std::clamp(static_cast<int>(maxTex), 1024, 4096);

    std::vector<std::string> tried;
    for (const std::string& font : fonts) {
        if (font.empty()) continue;
        // Find it: as given, then from one or two folders up (running from a build folder)
        std::string path;
        for (const std::string& p : { font, "../" + font, "../../" + font }) {
            std::error_code ec;
            if (fs::is_regular_file(p, ec)) { path = fs::absolute(p, ec).string(); break; }
        }
        if (path.empty()) { tried.push_back(font + " (not found)"); continue; }

        const std::vector<unsigned char> data = readFontFile(path);
        stbtt_fontinfo info;
        const int offset = data.empty() ? -1 : stbtt_GetFontOffsetForIndex(data.data(), 0);
        if (offset < 0 || !stbtt_InitFont(&info, data.data(), offset)) {
            std::cout << "[Font] Can't read '" << path << "' as a TrueType font - skipping it" << std::endl;
            tried.push_back(font + " (not a readable font)");
            continue;
        }

        // Bake every character; if they don't all fit, double the atlas (width first) and try again
        stbtt_bakedchar baked[kNumChars] = {};
        std::vector<unsigned char> bitmap;
        int w = 512, h = 512, result = 0;
        while (true) {
            bitmap.assign(static_cast<size_t>(w) * static_cast<size_t>(h), 0);
            result = stbtt_BakeFontBitmap(data.data(), offset, kPixelHeight, bitmap.data(), w, h,
                                          kFirstChar, kNumChars, baked);
            if (result > 0) break;                         // > 0: all of them fit
            // <= 0: only -result characters fit
            if (w == maxSize && h == maxSize) break;
            if (w <= h) w *= 2; else h *= 2;
        }
        if (result <= 0) {
            std::cout << "[Font] '" << path << "': only " << -result << " of " << kNumChars
                      << " characters fit even in a " << w << "x" << h << " atlas - skipping it" << std::endl;
            tried.push_back(font + " (too big to bake)");
            continue;
        }

        // Upload. Only now is the old atlas (from an earlier init) replaced.
        while (glGetError() != GL_NO_ERROR) {}
        GLuint tex = 0;
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);             // one byte per pixel
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, w, h, 0, GL_RED, GL_UNSIGNED_BYTE, bitmap.data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        if (tex == 0 || glGetError() != GL_NO_ERROR) {
            if (tex != 0) glDeleteTextures(1, &tex);
            std::cout << "[Font] The graphics card wouldn't take the " << w << "x" << h
                      << " font atlas for '" << path << "' - skipping it" << std::endl;
            tried.push_back(font + " (atlas upload failed)");
            continue;
        }

        if (fontTextureID != 0) glDeleteTextures(1, &fontTextureID);
        fontTextureID = tex;
        atlasWidth = w;
        atlasHeight = h;
        std::copy(std::begin(baked), std::end(baked), std::begin(cdata));
        loadedFont = path;
        if (vao == 0) glGenVertexArrays(1, &vao);
        if (vbo == 0) glGenBuffers(1, &vbo);
        std::cout << "[Font] Loaded " << path << " (" << kNumChars << " characters in a "
                  << w << "x" << h << " atlas)" << std::endl;
        return true;
    }

    std::cout << "[Font Error] No usable font. Tried:";
    for (const std::string& t : tried) std::cout << "\n    " << t;
    if (tried.empty()) std::cout << " (no fonts given)";
    std::cout << std::endl;
    return false;
}

void FontRenderer::renderText(GLuint fontShader, const std::string& text,
        float x, float y, float scaleX, float scaleY, glm::vec4 color,
        Dimensions2D resolution) {
    if (text.empty() || fontTextureID == 0) return;

    std::vector<float> vertices = this->generateFontVertices(x, y, scaleX,
            scaleY, resolution, text);

    if (vertices.empty()) return;

    this->drawGLFontQuads(fontShader, color, resolution, vertices);
}

// Same scaling as generateFontVertices, so the numbers match what is drawn.
float FontRenderer::textWidth(std::string_view text, float scaleX, Dimensions2D resolution) const {
    if (this->baseResolution.width <= 0 || fontTextureID == 0) return 0.0f;
    const float scaleWidth = scaleX * static_cast<float>(resolution.width)
                           / static_cast<float>(this->baseResolution.width);
    float rx = 0.0f, ry = 0.0f;
    for (size_t i = 0; i < text.size();) {
        const int glyph = nextGlyph(text, i);
        if (glyph < 0) continue;
        stbtt_aligned_quad q;
        stbtt_GetBakedQuad(const_cast<stbtt_bakedchar*>(cdata), atlasWidth, atlasHeight, glyph, &rx, &ry, &q, 1);
    }
    return rx * scaleWidth;
}

float FontRenderer::capHeight(float scaleY, Dimensions2D resolution) const {
    if (this->baseResolution.height <= 0 || fontTextureID == 0) return 0.0f;
    const float scaleHeight = scaleY * static_cast<float>(resolution.height)
                            / static_cast<float>(this->baseResolution.height);
    float rx = 0.0f, ry = 0.0f;
    stbtt_aligned_quad q;
    stbtt_GetBakedQuad(const_cast<stbtt_bakedchar*>(cdata), atlasWidth, atlasHeight, 'H' - 32, &rx, &ry, &q, 1);
    return -q.y0 * scaleHeight;   // y0 is negative: how far the top of 'H' is above the baseline
}

std::vector<float> FontRenderer::generateFontVertices(float x, float y,
        float scaleX, float scaleY, Dimensions2D resolution,
        std::string_view text) const
{
    std::vector<float> vertices;
    float rx = 0.0f;
    float ry = 0.0f;

    float scaleWidth = scaleX * static_cast<float>(resolution.width)
                     / static_cast<float>(this->baseResolution.width);
    float scaleHeight = scaleY * static_cast<float>(resolution.height)
                      / static_cast<float>(this->baseResolution.height);

    for (size_t i = 0; i < text.length();) {
        const int glyph = nextGlyph(text, i);
        if (glyph < 0) continue;

        stbtt_aligned_quad q;
        stbtt_GetBakedQuad(const_cast<stbtt_bakedchar*>(cdata), atlasWidth, atlasHeight, glyph, &rx, &ry, &q, 1);

        float x0 = q.x0 * scaleWidth + x;
        float x1 = q.x1 * scaleWidth + x;
        float y0 = q.y0 * scaleHeight + y;
        float y1 = q.y1 * scaleHeight + y;

        float quad[] = {
            x0, y0, q.s0, q.t0,
            x1, y0, q.s1, q.t0,
            x1, y1, q.s1, q.t1,

            x0, y0, q.s0, q.t0,
            x1, y1, q.s1, q.t1,
            x0, y1, q.s0, q.t1
        };
        vertices.insert(vertices.end(), std::begin(quad), std::end(quad));
    }

    return vertices;
}

void FontRenderer::drawGLFontQuads(GLuint fontShader, glm::vec4 color,
        Dimensions2D resolution, const std::vector<float>& vertices)
{
    glDisable(GL_DEPTH_TEST);
    glUseProgram(fontShader);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, fontTextureID);
    glUniform1i(glGetUniformLocation(fontShader, "fontTexture"), 0);
    glUniform4f(glGetUniformLocation(fontShader, "textColor"), color.r, color.g, color.b, color.a);

    glm::mat4 projection = glm::ortho(0.0f,
            static_cast<float>(resolution.width),
            static_cast<float>(resolution.height), 0.0f);
    glUniformMatrix4fv(glGetUniformLocation(fontShader, "projection"), 1,
            GL_FALSE, glm::value_ptr(projection));

    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_DYNAMIC_DRAW);

    GLsizei stride = 4 * sizeof(float);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, (void*)(2 * sizeof(float)));

    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices.size() / 4));

    glBindVertexArray(0);
    glEnable(GL_DEPTH_TEST);
}
