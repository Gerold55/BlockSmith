#include "Skybox.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>

const char* skyboxVertexShader = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
out vec3 FragPos;

uniform mat4 view;
uniform mat4 projection;

void main() {
    FragPos = aPos;
    // Remove translation from the view matrix so the skybox stays centered on the camera
    mat4 rotView = mat4(mat3(view));
    vec4 clipPos = projection * rotView * vec4(aPos, 1.0);
    // Force z = w so the skybox is always rendered at the maximum depth (far plane)
    gl_Position = clipPos.xyww;
}
)";

const char* skyboxFragmentShader = R"(
#version 330 core
out vec4 FragColor;
in vec3 FragPos;

uniform float u_SunPosY; // ranges from -1.0 (midnight) to +1.0 (noon)
uniform float u_StarDensity;    // 0..1: chance that a sky cell has a star
uniform float u_StarBrightness; // 0 = no stars, 1 = normal

void main() {
    vec3 normalizedDir = normalize(FragPos);
    float y = normalizedDir.y;

    // Authentic Minecraft-inspired palette
    vec3 dayZenith    = vec3(0.32f, 0.52f, 0.90f);
    vec3 dayHorizon   = vec3(0.58f, 0.74f, 0.94f);
    
    vec3 sunsetZenith   = vec3(0.12f, 0.15f, 0.32f);
    vec3 sunsetHorizon  = vec3(0.92f, 0.48f, 0.22f); // Vibrant orange-red sunset glow
    vec3 sunsetTransition = vec3(0.55f, 0.22f, 0.35f); // Deep purplish twilight band

    vec3 nightZenith  = vec3(0.01f, 0.012f, 0.035f);
    vec3 nightHorizon = vec3(0.03f, 0.04f, 0.09f);

    vec3 topColor;
    vec3 horizonColor;

    float sunFactor = clamp(u_SunPosY, -1.0f, 1.0f);

    // Blending based on Sun height position
    if (sunFactor > 0.0f) {
        // Day to Sunset blend
        float t = clamp(sunFactor * 2.0f - 1.0f, 0.0f, 1.0f); // 0 at horizon/sunset, 1 at noon
        topColor     = mix(sunsetZenith, dayZenith, t);
        horizonColor = mix(sunsetHorizon, dayHorizon, t);
    } else {
        // Sunset to Night blend
        float t = clamp((-sunFactor) * 2.0f, 0.0f, 1.0f); // 0 at sunset, 1 at midnight
        topColor     = mix(sunsetZenith, nightZenith, t);
        horizonColor = mix(sunsetHorizon, nightHorizon, t);
    }

    // Minecraft vertical gradient curve (smoother zenith falloff)
    float altitude = clamp(y, 0.0f, 1.0f);
    vec3 skyColor = mix(horizonColor, topColor, pow(altitude, 0.55f));

    // Add a warm/cool horizon haze band around y = 0 mimicking atmospheric scattering
    float horizonGlow = exp(-abs(y) * 4.0f);
    if (sunFactor > -0.3f && sunFactor < 0.3f) {
        // Enhance sunset band intensity when the sun is near the horizon
        float sunsetIntensity = 1.0f - abs(sunFactor) * 3.33f;
        skyColor += sunsetTransition * horizonGlow * max(0.0f, sunsetIntensity);
    } else {
        // General ambient horizon brightness lift
        skyColor += horizonColor * horizonGlow * 0.25f;
    }

    // Stars: one possible star per cell of a fixed grid on the sky sphere. Each star is a small
    // soft dot of a fixed size, so it stays put and steady when the camera moves or turns
    // (no per-pixel noise, which flickers). Count and brightness come from sky.set_stars in Lua.
    if (y > 0.0f && sunFactor < 0.15f && u_StarDensity > 0.0f && u_StarBrightness > 0.0f) {
        const float cells = 90.0f;                        // grid resolution around the sky
        vec3 p = normalizedDir * cells;
        vec3 cell = floor(p);
        float h = fract(sin(dot(cell, vec3(127.1f, 311.7f, 74.7f))) * 43758.5453f);
        if (h < u_StarDensity) {
            // Where in the cell the star sits (kept away from the edges), and how bright it is
            vec3 jitter = vec3(fract(h * 17.13f), fract(h * 31.71f), fract(h * 57.37f));
            vec3 starPos = cell + 0.25f + 0.5f * jitter;
            float d = length(p - starPos);
            float size = 0.15f + 0.08f * fract(h * 91.3f); // in cells (a few pixels: big enough not to shimmer)
            float dotShape = 1.0f - smoothstep(size * 0.4f, size, d);
            float brightness = 0.45f + 0.55f * fract(h * 7.77f);
            float nightIntensity = clamp((-sunFactor + 0.15f) * 2.5f, 0.0f, 1.0f);
            float horizonFade = smoothstep(0.0f, 0.25f, y);   // fewer, dimmer stars near the horizon
            skyColor += vec3(dotShape * brightness * nightIntensity * horizonFade * u_StarBrightness);
        }
    }

    // Pitch black below the horizon plane
    if (y < 0.0f) {
        skyColor = mix(skyColor, nightHorizon, clamp(-y * 2.0f, 0.0f, 1.0f));
    }

    FragColor = vec4(skyColor, 1.0f);
}
)";

Skybox::Skybox() {}
Skybox::~Skybox() {
    if (VAO) glDeleteVertexArrays(1, &VAO);
    if (VBO) glDeleteBuffers(1, &VBO);
    if (shaderProgram) glDeleteProgram(shaderProgram);
}

void Skybox::init() {
    float skyboxVertices[] = {
        -1.0f,  1.0f, -1.0f, -1.0f, -1.0f, -1.0f,  1.0f, -1.0f, -1.0f,
         1.0f, -1.0f, -1.0f,  1.0f,  1.0f, -1.0f, -1.0f,  1.0f, -1.0f,
        -1.0f, -1.0f,  1.0f, -1.0f, -1.0f, -1.0f, -1.0f,  1.0f, -1.0f,
        -1.0f,  1.0f, -1.0f, -1.0f,  1.0f,  1.0f, -1.0f, -1.0f,  1.0f,
         1.0f, -1.0f, -1.0f,  1.0f, -1.0f,  1.0f,  1.0f,  1.0f,  1.0f,
         1.0f,  1.0f,  1.0f,  1.0f,  1.0f, -1.0f,  1.0f, -1.0f, -1.0f,
        -1.0f, -1.0f,  1.0f,  1.0f, -1.0f,  1.0f,  1.0f,  1.0f,  1.0f,
         1.0f,  1.0f,  1.0f, -1.0f,  1.0f,  1.0f, -1.0f, -1.0f,  1.0f,
        -1.0f,  1.0f, -1.0f,  1.0f,  1.0f, -1.0f,  1.0f,  1.0f,  1.0f,
         1.0f,  1.0f,  1.0f, -1.0f,  1.0f,  1.0f, -1.0f,  1.0f, -1.0f,
        -1.0f, -1.0f, -1.0f, -1.0f, -1.0f,  1.0f,  1.0f, -1.0f, -1.0f,
         1.0f, -1.0f, -1.0f, -1.0f, -1.0f,  1.0f,  1.0f, -1.0f,  1.0f
    };

    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);
    glBindVertexArray(VAO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(skyboxVertices), &skyboxVertices, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glBindVertexArray(0);

    auto compileShader = [](GLenum type, const char* src) {
        GLuint shader = glCreateShader(type);
        glShaderSource(shader, 1, &src, NULL);
        glCompileShader(shader);
        return shader;
    };

    shaderProgram = glCreateProgram();
    glAttachShader(shaderProgram, compileShader(GL_VERTEX_SHADER, skyboxVertexShader));
    glAttachShader(shaderProgram, compileShader(GL_FRAGMENT_SHADER, skyboxFragmentShader));
    glLinkProgram(shaderProgram);
}

void Skybox::render(const glm::mat4& view, const glm::mat4& projection, float sunPosY) {
    glDepthFunc(GL_LEQUAL); // Ensure skybox passes depth test at the far plane
    glDepthMask(GL_FALSE);  // Disable writing to depth buffer so it stays behind everything
    glDisable(GL_CULL_FACE); // Disable culling so inner cube faces render properly

    glUseProgram(shaderProgram);
    glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "view"), 1, GL_FALSE, glm::value_ptr(view));
    glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "projection"), 1, GL_FALSE, glm::value_ptr(projection));
    glUniform1f(glGetUniformLocation(shaderProgram, "u_SunPosY"), sunPosY);
    glUniform1f(glGetUniformLocation(shaderProgram, "u_StarDensity"), starDensity);
    glUniform1f(glGetUniformLocation(shaderProgram, "u_StarBrightness"), starBrightness);

    glBindVertexArray(VAO);
    glDrawArrays(GL_TRIANGLES, 0, 36);
    glBindVertexArray(0);

    // Restore default states
    glEnable(GL_CULL_FACE);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LESS);
}