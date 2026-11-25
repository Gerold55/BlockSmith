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
uniform vec3 u_SunDir;          // toward the sun
uniform float u_SkyBrightness;  // 1 = as is (Lighting Low/High: a little deeper)
uniform float u_SkyHalo;        // the soft glow around the sun (0 = none)
uniform float u_Aurora;         // 0 = none .. 1 = a strong aurora (sky.set_aurora)
uniform vec3  u_AuroraLow;      // its colour at the bottom of the curtains
uniform vec3  u_AuroraHigh;     // and at the top
uniform float u_AuroraSpeed;    // how fast it ripples
uniform float u_AuroraSize;     // how big its folds are
uniform float u_AuroraDir;      // where it hangs (radians, compass: 0 north)
uniform int   u_AuroraNight;    // 1 = only at night
uniform float u_AuroraHaze;     // how much soft glow spreads round the curtains (0 = none)
uniform float u_AuroraPixels;   // the pixel style: square sky pixels, this many across 90 degrees (0 = smooth)
uniform float u_Time;

)" BLOCKSMITH_SKY_GLSL R"(
// The aurora: thin sheets of light hanging over the land like curtains, as the real thing is.
// Each sheet stands over a wandering line on the ground (a ring all round the sky, and a ribbon
// curling overhead), from a lower edge that wanders up and down to far above it. Following the
// line of sight out across the ground, each place it passes through a sheet adds that sheet's light
// at that height - more the longer the sight line runs inside it: faint where a sheet is seen face
// on, bright where it folds and is seen edge on. That, and the sheets overlapping, is what makes
// it look like an aurora rather than a glowing band. Fine streaks run up the sheets; the low colour
// glows at the bright lower edge, fading up into the high colour.
float auroraHash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float auroraNoise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(auroraHash(i), auroraHash(i + vec2(1.0, 0.0)), f.x),
               mix(auroraHash(i + vec2(0.0, 1.0)), auroraHash(i + vec2(1.0, 1.0)), f.x), f.y);
}
// the ring: how far (across it) a ground point is from its line; its folds wander slowly
float auroraRing(vec2 p, float t) {
    float r = length(p);
    vec2 a = p / max(r, 1e-4);
    float sz = max(u_AuroraSize, 0.05);
    float f1 = auroraNoise(a * 1.7 / sz + vec2(t * 0.012, 3.0)) * 2.0 - 1.0;
    float f2 = auroraNoise(a * 5.0 / sz + vec2(-t * 0.025, 8.0)) * 2.0 - 1.0;
    float f3 = auroraNoise(a * 13.0 / sz + vec2(t * 0.04, 21.0)) * 2.0 - 1.0;
    return r - 0.62 + f1 * 0.2 + f2 * 0.07 + f3 * 0.02;
}
// the ribbon overhead
float auroraRibbon(vec2 p, float t) {
    float x = p.x / max(u_AuroraSize, 0.05);
    float big = auroraNoise(vec2(x * 0.5 + 17.0 + t * 0.01, 17.0)) * 2.0 - 1.0;
    float mid = auroraNoise(vec2(x * 1.6 - t * 0.03, 21.0)) * 2.0 - 1.0;
    float fine = auroraNoise(vec2(x * 4.5 + t * 0.05, 25.0)) * 2.0 - 1.0;
    return p.y + 0.1 + big * 0.35 + mid * 0.1 + fine * 0.03;
}
// the light a sheet gives at height h (1 = the usual lower edge), at a place along it (s)
void auroraGlow(float h, float s, float seed, float t, inout float lowA, inout float highA, float amount) {
    // the lower edge wanders up and down along the sheet
    float base = 1.0 + 0.35 * auroraNoise(vec2(s * 2.0 + seed, t * 0.02 + seed));
    float k = (h - base) / 2.1;
    if (k < 0.0) return;
    // streaks: fine and sharp, straight up the sheet, shimmering slowly; brightness drifting along it
    float st = auroraNoise(vec2(s * 30.0 + t * 0.06, seed)) * 0.6 + auroraNoise(vec2(s * 75.0 - t * 0.1, seed + 3.0)) * 0.4;
    float streak = 0.15 + 2.0 * pow(st, 3.0);
    float pulse = 0.25 + 0.75 * smoothstep(0.25, 0.75, auroraNoise(vec2(s * 1.3 - t * 0.05, seed + 9.0)));
    float lowGlow = smoothstep(0.0, 0.025, k) * exp(-k * 5.0);
    float highGlow = smoothstep(0.06, 0.35, k) * exp(-(k - 0.3) * 2.8) * 0.45;
    float a = amount * streak * pulse;
    lowA += lowGlow * a;
    highA += highGlow * a;
}
vec3 skyAurora(vec3 d, float t) {
    // The pixel style: one colour per square sky pixel (a fixed grid on the sky, like the stars
    // and the moon's pixels), so it sits with a pixel-art sky
    bool pixel = u_AuroraPixels > 0.0;
    if (pixel) d = normalize((floor(d * u_AuroraPixels) + 0.5) / u_AuroraPixels);
    // turned toward u_AuroraDir
    float ca = cos(-u_AuroraDir), sa = sin(-u_AuroraDir);
    d = vec3(d.x * ca - d.z * sa, d.y, d.x * sa + d.z * ca);
    if (d.y < 0.01) return vec3(0.0);
    t *= u_AuroraSpeed;
    // The line of sight's track across the ground, and how its height grows along it. (Ground
    // distances are scaled: a sheet at height h over ground point p is seen along d.xz / d.y * h * 0.22.)
    float flat_ = length(d.xz);
    vec2 dir = flat_ > 1e-5 ? d.xz / flat_ : vec2(1.0, 0.0);
    float rise = d.y / max(flat_, 1e-5) / 0.22;                       // height per unit of ground distance
    // where the sight line is between the sheets' lowest edge and their top: only there can it meet them
    float s0 = 0.95 / rise, s1 = 3.6 / rise;
    const int N = 48;
    float ds = (s1 - s0) / float(N);
    const float T = 0.05;                                             // how thick a sheet is
    float lowA = 0.0, highA = 0.0;
    float hazeLow = 0.0, hazeHigh = 0.0;     // the soft glow the sheets light the air around them with
    float wr0 = auroraRing(dir * s0, t), wo0 = auroraRibbon(dir * s0, t * 0.8);
    for (int i = 1; i <= N; ++i) {
        float s = s0 + ds * float(i);
        vec2 p = dir * s;
        float wr = auroraRing(p, t), wo = auroraRibbon(p, t * 0.8);
        // The haze: real aurora light spreads into a soft glow round its curtains. Near a sheet (in
        // any direction, not just through it), added up along the sight line: wider and fainter than
        // the sheets, strongest low down, with no streaks.
        {
            float near_ = exp(-wr * wr * 30.0) + 0.75 * exp(-wo * wo * 30.0);
            float hk = (s * rise - 1.1) / 2.1;
            float lowP = smoothstep(-0.12, 0.08, hk) * exp(-max(hk, 0.0) * 3.0);
            float highP = smoothstep(0.05, 0.4, hk) * exp(-max(hk - 0.35, 0.0) * 2.2) * 0.5;
            float w = near_ * ds;
            hazeLow += lowP * w;
            hazeHigh += highP * w;
        }
        // through the ring's sheet between the last step and this one?
        if (wr0 * wr <= 0.0) {
            float f = wr0 / (wr0 - wr);
            float sc = s - ds + ds * f;
            float slope = abs(wr - wr0) / ds;                          // how square on it's crossed
            float inside = min(T / max(slope, 1e-3), 0.6);             // the sight line's length in it
            vec2 pc = dir * sc;
            vec2 a = pc / max(length(pc), 1e-4);
            float along = atan(a.y, a.x) * 1.9;                        // (place along the ring)
            auroraGlow(sc * rise, along, 0.0, t, lowA, highA, inside);
        }
        if (wo0 * wo <= 0.0) {
            float f = wo0 / (wo0 - wo);
            float sc = s - ds + ds * f;
            float slope = abs(wo - wo0) / ds;
            float inside = min(T / max(slope, 1e-3), 0.6) * 0.85;
            vec2 pc = dir * sc;
            auroraGlow(sc * rise, pc.x / max(u_AuroraSize, 0.05), 40.0, t, lowA, highA, inside);
        }
        wr0 = wr; wo0 = wo;
    }
    // (the haze, kept soft: a faint glow even where the sight line runs a long way through it)
    hazeLow *= 0.8 * u_AuroraHaze;
    hazeHigh *= 0.8 * u_AuroraHaze;
    hazeLow = hazeLow / (1.0 + hazeLow * 3.0);
    hazeHigh = hazeHigh / (1.0 + hazeHigh * 3.0);
    float accLow = lowA * 13.0 + hazeLow, accHigh = highA * 13.0 + hazeHigh;
    // (very bright where several sheets line up: rolled off softly, so the colour holds)
    accLow = accLow / (1.0 + accLow * 0.9);
    accHigh = accHigh / (1.0 + accHigh * 0.9);
    if (pixel) {
        // the pixel style, like a hand-drawn texture: each pixel mostly one colour (the stronger),
        // in a few flat steps (spaced closer at the dim end, so faint glow still shows)
        if (accLow >= accHigh) accHigh *= 0.3; else accLow *= 0.3;
        accLow = sqrt(accLow);
        accHigh = sqrt(accHigh);
        accLow = floor(accLow * 5.0 + 0.5) / 5.0;
        accHigh = floor(accHigh * 5.0 + 0.5) / 5.0;
        accLow *= accLow;
        accHigh *= accHigh;
    }
    vec3 acc = u_AuroraLow * accLow + u_AuroraHigh * accHigh;
    // (the brightest parts of the lower edge a little whiter)
    acc += vec3(0.3) * u_AuroraLow * smoothstep(0.55, 0.9, accLow);
    // (thinner near the horizon, where it's far away through more air)
    return acc * 1.5 * smoothstep(0.01, 0.12, d.y);
}
void main() {
    vec3 normalizedDir = normalize(FragPos);
    float y = normalizedDir.y;
    float sunFactor = clamp(u_SunPosY, -1.0f, 1.0f);
    vec3 skyColor = blocksmithSky(normalizedDir, normalize(u_SunDir), u_SkyHalo);

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


    // The aurora (night only: it fades out as the sky gets light)
    if (u_Aurora > 0.0f && y > 0.0f) {
        float night = u_AuroraNight == 1 ? clamp((-sunFactor + 0.05f) * 4.0f, 0.0f, 1.0f) : 1.0f;
        if (night > 0.0f) skyColor += skyAurora(normalizedDir, u_Time) * u_Aurora * night;
    }

    // (dimmer and a touch richer in colour, for Lighting Low/High)
    if (u_SkyBrightness < 0.999f) {
        float l = dot(skyColor, vec3(0.299f, 0.587f, 0.114f));
        skyColor = max(mix(vec3(l), skyColor, 1.0f + (1.0f - u_SkyBrightness) * 1.2f), vec3(0.0f)) * u_SkyBrightness;
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
    {
        glm::vec3 sd = sunDirection;
        if (glm::dot(sd, sd) < 1e-6f) sd = glm::vec3(0.0f, sunPosY, 0.0f);
        sd.y = sunPosY;            // (the height always matches the time given)
        if (glm::dot(sd, sd) < 1e-6f) sd = glm::vec3(1.0f, 0.0f, 0.0f);
        glUniform3f(glGetUniformLocation(shaderProgram, "u_SunDir"), sd.x, sd.y, sd.z);
    }
    glUniform1f(glGetUniformLocation(shaderProgram, "u_StarDensity"), starDensity);
    glUniform1f(glGetUniformLocation(shaderProgram, "u_StarBrightness"), starBrightness);
    glUniform1f(glGetUniformLocation(shaderProgram, "u_SkyBrightness"), brightness);
    glUniform1f(glGetUniformLocation(shaderProgram, "u_SkyHalo"), sunHalo);
    glUniform1f(glGetUniformLocation(shaderProgram, "u_Aurora"), aurora);
    glUniform3f(glGetUniformLocation(shaderProgram, "u_AuroraLow"), auroraLow.r, auroraLow.g, auroraLow.b);
    glUniform3f(glGetUniformLocation(shaderProgram, "u_AuroraHigh"), auroraHigh.r, auroraHigh.g, auroraHigh.b);
    glUniform1f(glGetUniformLocation(shaderProgram, "u_AuroraSpeed"), auroraSpeed);
    glUniform1f(glGetUniformLocation(shaderProgram, "u_AuroraSize"), auroraSize);
    glUniform1f(glGetUniformLocation(shaderProgram, "u_AuroraDir"), glm::radians(auroraDirection));
    glUniform1i(glGetUniformLocation(shaderProgram, "u_AuroraNight"), auroraNightOnly ? 1 : 0);
    glUniform1f(glGetUniformLocation(shaderProgram, "u_AuroraPixels"), auroraPixels);
    glUniform1f(glGetUniformLocation(shaderProgram, "u_AuroraHaze"), auroraHaze);
    glUniform1f(glGetUniformLocation(shaderProgram, "u_Time"), time);

    glBindVertexArray(VAO);
    glDrawArrays(GL_TRIANGLES, 0, 36);
    glBindVertexArray(0);

    // Restore default states
    glEnable(GL_CULL_FACE);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LESS);
}