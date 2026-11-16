#ifndef FONTRENDERER_H
#define FONTRENDERER_H

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <stb_truetype.h>

#include <string_view>

struct Dimensions2D
{
    int width{};
    int height{};
};

class FontRenderer {
public:
    GLuint vao = 0, vbo = 0;
    GLuint fontTextureID = 0;
    stbtt_bakedchar cdata[96];

    FontRenderer(Dimensions2D resolution);
    ~FontRenderer();

    bool init(const std::string& fontPath);
<<<<<<< Updated upstream
    void renderText(GLuint fontShader, const std::string& text, float x,
            float y, float scaleX, float scaleY, glm::vec4 color,
            Dimensions2D resolution);
=======

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
>>>>>>> Stashed changes

private:
    Dimensions2D baseResolution;

    std::vector<float> generateFontVertices(float x, float y, float scaleX,
        float scaleY, Dimensions2D resolution,
        std::string_view text) const;

    void drawGLFontQuads(GLuint fontShader, glm::vec4 color,
            Dimensions2D resolution,
            const std::vector<float>& vertices);
};

#endif // FONTRENDERER_H
