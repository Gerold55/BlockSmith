#pragma once
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include "DayTime.h"

struct PointLight {
    glm::vec3 position;
    glm::vec3 color;
    float radius;
    float intensity;
    bool active = true;   // removed lights stay as empty slots, so other lights keep their ids
};

class LightingSystem {
public:
    LightingSystem();
    ~LightingSystem();

    // Runs the day clock (call every frame while playing; not while paused in singleplayer)
    void update(float deltaTime);

    // The time of day, 0..1: 0.25 midnight, 0.5 sunrise, 0.75 midday, 0 sunset (DayTime.h)
    float getTimeOfDay() const;
    glm::vec3 getSunDirection() const;
    glm::vec3 getMoonDirection() const;
    int getMoonPhase() const;

    // Texture loaders
    void setSunTexture(const std::string& path);
    void setMoonTexture(const std::string& path);
    void setMoonPhaseTextures(const std::vector<std::string>& paths);

    // Advanced Global Lighting Protocols for Lua Devs
    void setSunColor(float r, float g, float b);
    void setAmbientColor(float r, float g, float b);
    void setFogColor(float r, float g, float b);
    void setSunIntensity(float intensity);
    void setAmbientIntensity(float intensity);
    void resetToDefaults();

    // Local Light Protocols (Torches, Colored Lights, Magic)
    int addPointLight(glm::vec3 pos, glm::vec3 color, float radius, float intensity);
    void updatePointLight(int id, glm::vec3 pos, glm::vec3 color, float radius, float intensity);
    void removePointLight(int id);
    const std::vector<PointLight>& getPointLights() const;

    // The day clock: saved with the world; on a server, the server's (GameClient::takeDayTime)
    DayClock clock;
    // (older: a fixed time set by sky.set_time; kept for code that still sets it. -1 = use the clock)
    float customTimeOfDay = -1.0f;
    int forcedMoonPhase = -1;
    float weatherRainIntensity = 0.0f;

    // Color & Intensity parameters
    glm::vec3 sunColor = glm::vec3(1.0f, 0.95f, 0.8f);
    glm::vec3 ambientColor = glm::vec3(0.2f, 0.2f, 0.25f);
    glm::vec3 customFogColor = glm::vec3(-1.0f);
    float sunIntensity = 1.0f;
    float ambientIntensity = 1.0f;

    GLuint sunTextureID = 0;
    GLuint moonTextureIDs[8] = { 0 };

private:
    std::vector<PointLight> m_PointLights;
};