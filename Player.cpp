#include "Player.h"
#include "World.h"
#include "Block.h"
#include "Settings.h"   // isActionDown(): controls follow the key bindings from Settings
#include "PlayerMovement.h"
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>

Player::Player() {
    position = glm::vec3(8.0f, 75.0f, 8.0f);
    velocity = glm::vec3(0.0f);
    yaw = -90.0f;
    pitch = 0.0f;
    lastX = 640.0f;
    lastY = 360.0f;
    firstMouse = true;
    isGrounded = false;
}

void Player::handleMouseLook(double xpos, double ypos) {
    if (firstMouse) {
        lastX = static_cast<float>(xpos);
        lastY = static_cast<float>(ypos);
        firstMouse = false;
    }

    float xoffset = static_cast<float>(xpos) - lastX;
    float yoffset = lastY - static_cast<float>(ypos);
    lastX = static_cast<float>(xpos);
    lastY = static_cast<float>(ypos);

    float sensitivity = 0.1f;
    xoffset *= sensitivity;
    yoffset *= sensitivity;

    yaw += xoffset;
    pitch += yoffset;

    if (pitch > 89.0f)  pitch = 89.0f;
    if (pitch < -89.0f) pitch = -89.0f;
}

void Player::update(float deltaTime, const World& world, GLFWwindow* window, sol::state& lua) {
    (void)lua;
    // Keys come from Settings > Controls (defaults: W/A/S/D, Space, Left Shift, Left Ctrl).
    // The movement itself lives in stepMovement (PlayerMovement.cpp) so the multiplayer
    // server can run exactly the same code.
    MoveState state{position, velocity, isGrounded};
    stepMovement(state, readMoveInput(window, yaw, pitch), deltaTime, world);
    position = state.position;
    velocity = state.velocity;
    isGrounded = state.grounded;
}

// Keys mods allow (engine.set_player_controls): hunger can stop sprinting, death can stop
// everything. Applied here, so prediction and what the server is sent always agree.
uint8_t g_MoveKeyMask = 0xFF;

MoveInput readMoveInput(GLFWwindow* window, float yaw, float pitch) {
    MoveInput in;
    in.yaw = yaw;
    in.pitch = pitch;
    if (isActionDown(window, "forward")) in.keys |= MoveInput::Forward;
    if (isActionDown(window, "back"))    in.keys |= MoveInput::Back;
    if (isActionDown(window, "left"))    in.keys |= MoveInput::Left;
    if (isActionDown(window, "right"))   in.keys |= MoveInput::Right;
    if (isActionDown(window, "jump"))    in.keys |= MoveInput::Jump;
    if (isActionDown(window, "sneak"))   in.keys |= MoveInput::Sneak;
    if (isActionDown(window, "sprint"))  in.keys |= MoveInput::Sprint;
    in.keys &= g_MoveKeyMask;
    return in;
}

glm::mat4 Player::getViewMatrix() const {
    glm::vec3 cameraPos = position + glm::vec3(0.0f, 1.62f, 0.0f);
    glm::vec3 front(
        cos(glm::radians(yaw)) * cos(glm::radians(pitch)),
        sin(glm::radians(pitch)),
        sin(glm::radians(yaw)) * cos(glm::radians(pitch))
    );
    return glm::lookAt(cameraPos, cameraPos + front, glm::vec3(0.0f, 1.0f, 0.0f));
}
