#pragma once
// The first-person view of your hand: the block or item you're holding at the bottom of the
// screen (right hand, or left for left-handed players), or your arm when the hand is empty.
// It sways as you walk, swings when you dig, place or use something, and dips and comes back
// up when you switch hotbar slots, like Minecraft. Drawn last, on its own depth, so it never
// pokes into walls.

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <vector>

class AnimatedModel;

class FirstPersonView {
public:
    // Every frame. `held` = block id in the active hotbar slot (0 = empty hand), `slot` = which
    // slot, `speed` = how fast the player walks (blocks/s, horizontal) and `onGround` for the
    // walking bob, `yaw` / `pitch` = where the camera looks (degrees; the hand trails behind it
    // when you turn).
    void update(float dt, uint8_t held, int slot, float speed, bool onGround, float yaw, float pitch);
    void swing();                       // dig / place / use: a swing (restarts if one is going)
    bool swinging() const { return m_swing >= 0.0f; }
    float swingProgress() const { return m_swing; }   // 0..1 while swinging, -1 when not

    // Draws the hand. `blockShader` = the world shader (blocks and items), `modelShader` = the
    // model shader (the arm from the player model), `light` = the light where the camera is
    // (sun, r, g, b; 0..1). `leftHanded` mirrors everything to the left side.
    void render(GLuint blockShader, GLuint modelShader, GLuint textureArray, int width, int height,
                const glm::vec4& light, const std::shared_ptr<AnimatedModel>& playerModel, bool leftHanded);

    // Lua settings
    bool enabled = true;                         // engine.set_first_person_hand(false) hides it
    std::vector<std::string> rightArmBones = { "upper_arm_right", "right_arm", "arm_right", "rightArm", "RightArm", "arm_r" };
    std::vector<std::string> leftArmBones  = { "upper_arm_left", "left_arm", "arm_left", "leftArm", "LeftArm", "arm_l" };
    glm::vec3 skinColor{0.88f, 0.69f, 0.55f};    // the plain arm when the model has no arm bone

private:
    void buildMesh(uint8_t id);
    void drawVertices(GLuint shader, const glm::mat4& model, const glm::mat4& proj);

    float m_swing = -1.0f;       // 0..1 while swinging, -1 = not
    float m_equip = 1.0f;        // 0 = just switched (hand lowered), 1 = up
    float m_walkDist = 0.0f;     // Minecraft's walkDist: the bob's phase follows the distance walked
    float m_bob = 0.0f;          // bob strength (0..0.1), eased in and out
    float m_lagYaw = 0.0f, m_lagPitch = 0.0f;   // where the hand is still looking (it catches up)
    bool m_lagInit = false;
    float m_yawLag = 0.0f, m_pitchLag = 0.0f;   // camera minus hand, this frame
    uint8_t m_held = 0;
    int m_slot = -1;

    // Mesh of what's in the hand (rebuilt when it changes)
    uint8_t m_meshFor = 255;
    bool m_meshIsFlat = false;   // an item picture or a plant: a flat card instead of a cube
    GLuint m_vao = 0, m_vbo = 0;
    int m_vertexCount = 0;
};
