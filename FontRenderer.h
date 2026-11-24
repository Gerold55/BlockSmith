#pragma once
// TrueType text rendering (stb_truetype). The font is baked once at 32 px for the base
// resolution (1920x1080) and scaled with the window: on a 1280-wide window text is drawn at
// 1280/1920 of its size sideways and (window height / 1080) up and down.
//
// textWidth / capHeight use exactly the same maths as renderText, so menus can centre and
// space text correctly at any window size (Lua: ui.text_width / ui.text_height).

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <stb_truetype.h>
#include <string>
#include <string_view>
#include <vector>

struct Dimensions2D {
    int width = 0;
    int height = 0;
};

class FontRenderer {
public:
    explicit FontRenderer(Dimensions2D resolution);
    ~FontRenderer();
    FontRenderer(const FontRenderer&) = delete;
    FontRenderer& operator=(const FontRenderer&) = delete;

    // Loads the first usable font of `fonts` (each also looked for in ../ and ../../) and bakes
    // all kNumChars characters into the atlas, growing the atlas (512x512 up to the GPU's limit)
    // until every one fits. A font that can't be read, parsed or fully baked is skipped - never
    // used half-baked. Returns false if none worked: the renderer then draws nothing (and an
    // earlier successful init, if any, stays in use).
    bool init(const std::vector<std::string>& fonts);
    bool init(const std::string& fontPath) { return init(std::vector<std::string>{ fontPath }); }
    // Fonts installed with the operating system, for when the game's own font won't load
    static std::vector<std::string> systemFonts();
    bool ready() const { return fontTextureID != 0; }
    const std::string& fontFile() const { return loadedFont; }

    // Characters 32..255 (ASCII + Latin-1: ³ ² é ü ñ © ...), baked at 32 px. Text is UTF-8;
    // anything outside this range is drawn as '?'.
    static constexpr int kFirstChar = 32;
    static constexpr int kNumChars = 224;
    static constexpr float kPixelHeight = 32.0f;

    // x, y: pixels from the top-left; y is the text BASELINE. Text is UTF-8.
    void renderText(GLuint fontShader, const std::string& text,
                    float x, float y, float scaleX, float scaleY, glm::vec4 color,
                    Dimensions2D resolution);

    // Width in pixels that renderText would use for `text` (sum of letter advances).
    float textWidth(std::string_view text, float scaleX, Dimensions2D resolution) const;
    // Height of capital letters above the baseline, in pixels (from the letter 'H').
    float capHeight(float scaleY, Dimensions2D resolution) const;
    // The resolution the scales are relative to (scale 1 = the font's size at this size)
    Dimensions2D baseSize() const { return baseResolution; }

    // For text drawn anywhere else (in the world, on signs): each letter of `text` as a quad in
    // font pixels (baseline at y = 0, y downwards) with its place in the atlas. Returns the width.
    struct GlyphQuad { float x0, y0, x1, y1, s0, t0, s1, t1; };
    float layout(std::string_view text, std::vector<GlyphQuad>& out) const;
    GLuint atlasTexture() const { return fontTextureID; }

private:
    std::vector<float> generateFontVertices(float x, float y, float scaleX, float scaleY,
                                            Dimensions2D resolution, std::string_view text) const;
    void drawGLFontQuads(GLuint fontShader, glm::vec4 color, Dimensions2D resolution,
                         const std::vector<float>& vertices);

    Dimensions2D baseResolution;
    GLuint vao = 0;
    GLuint vbo = 0;
    GLuint fontTextureID = 0;
    int atlasWidth = 512;      // the atlas size the characters were baked into (the texture
    int atlasHeight = 512;     // coordinates in cdata are relative to it)
    std::string loadedFont;
    stbtt_bakedchar cdata[kNumChars] = {};
};
