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
    // Which way the sun is (the sunset glow is on its side of the sky). Set before render.
    glm::vec3 sunDirection{ 0.0f, 1.0f, 0.0f };
    // The soft glow around the sun: 0 = none (the standard look), 1 = Lighting Low/High
    float sunHalo = 0.0f;

    GLuint cubemapTexture = 0;

    // Stars (set from Lua with sky.set_stars): chance per sky cell (0..1) and brightness (0 = off)
    float starDensity = 0.06f;
    float starBrightness = 0.9f;
    // How bright the sky is drawn (1 = as is). Lighting Low/High set it a little lower and
    // deeper, so the sky sits right with the land's realistic light instead of washing it out.
    float brightness = 1.0f;

private:
    GLuint VAO = 0;
    GLuint VBO = 0;
    GLuint shaderProgram = 0;
};
// GLSL: the sky's colour looking along `d` (normalised) with the sun toward `sunDir`, without
// stars. Shared by the sky and the world's fog, so far-off land fades into exactly the sky
// behind it (no hard edge where the loaded world ends).
#define BLOCKSMITH_SKY_GLSL R"(
// halo: the soft glow around the sun (0 = none, the standard look; 1 with Lighting Low/High)
vec3 blocksmithSky(vec3 d, vec3 sunDir, float halo) {
    float y = d.y;
    vec3 dayZenith    = vec3(0.32, 0.52, 0.90);
    vec3 dayHorizon   = vec3(0.62, 0.76, 0.94);
    vec3 sunsetZenith = vec3(0.14, 0.17, 0.34);
    vec3 nightZenith  = vec3(0.01, 0.012, 0.035);
    vec3 nightHorizon = vec3(0.03, 0.04, 0.09);
    // Around sunset the glow is on the sun's side of the sky: orange there, a cooler dusky
    // violet-blue the other way round
    vec2 hd = d.xz, hs = sunDir.xz;
    float toward = 0.5;
    if (dot(hd, hd) > 1e-6 && dot(hs, hs) > 1e-6) toward = dot(normalize(hd), normalize(hs)) * 0.5 + 0.5;
    vec3 sunsetHorizon = mix(vec3(0.46, 0.42, 0.58), vec3(0.96, 0.52, 0.24), toward * toward);
    vec3 sunsetBand    = mix(vec3(0.20, 0.12, 0.24), vec3(0.55, 0.22, 0.20), toward * toward);

    float sunFactor = clamp(sunDir.y, -1.0, 1.0);
    vec3 topColor, horizonColor;
    if (sunFactor > 0.0) {
        float t = clamp(sunFactor * 2.0 - 1.0, 0.0, 1.0);
        float t2 = smoothstep(0.0, 0.35, sunFactor);        // the horizon clears quickly after sunrise
        topColor     = mix(sunsetZenith, dayZenith, t);
        horizonColor = mix(sunsetHorizon, dayHorizon, t2);
    } else {
        float t = clamp(-sunFactor * 2.0, 0.0, 1.0);
        topColor     = mix(sunsetZenith, nightZenith, t);
        horizonColor = mix(sunsetHorizon, nightHorizon, t);
    }
    float altitude = clamp(y, 0.0, 1.0);
    vec3 skyColor = mix(horizonColor, topColor, pow(altitude, 0.55));
    float horizonGlow = exp(-abs(y) * 4.0);
    float sunsetIntensity = max(0.0, 1.0 - abs(sunFactor) * 3.33);
    skyColor += sunsetBand * horizonGlow * sunsetIntensity;
    skyColor += horizonColor * horizonGlow * 0.2 * (1.0 - sunsetIntensity);
    // A soft bright halo around the sun (wider and warmer when it's low)
    float cs = max(dot(d, sunDir), 0.0);
    float sunUp = smoothstep(-0.15, 0.05, sunFactor);
    vec3 haloCol = mix(vec3(1.0, 0.55, 0.25), vec3(1.0, 0.95, 0.85), smoothstep(0.0, 0.4, sunFactor));
    skyColor += haloCol * (pow(cs, 6.0) * 0.22 + pow(cs, 48.0) * 0.25) * sunUp * halo;
    // Below the horizon (seen where the land isn't loaded yet, or from high up): the horizon's
    // own colour, a little deeper - not black in the middle of the day
    if (y < 0.0) skyColor = mix(skyColor, horizonColor * 0.8, clamp(-y * 2.0, 0.0, 1.0));
    return skyColor;
}
)"
