#pragma once
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>

class Skybox {
public:
    Skybox();
    ~Skybox();

    void init();
    void render(const glm::mat4& view, const glm::mat4& projection, float sunPosY);

    GLuint cubemapTexture = 0;

    // Stars (set from Lua with sky.set_stars): chance per sky cell (0..1) and brightness (0 = off)
    float starDensity = 0.06f;
    float starBrightness = 0.9f;

private:
    GLuint VAO = 0;
    GLuint VBO = 0;
    GLuint shaderProgram = 0;
};