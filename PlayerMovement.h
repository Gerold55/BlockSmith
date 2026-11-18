#pragma once
// Player movement as a pure function: the same input on the same world always gives the
// same result. The client runs it to move instantly (prediction); the server runs it too
// and its result is the real one (it corrects the client if they disagree).

#include <glm/glm.hpp>
#include <cstdint>

class World;
struct GLFWwindow;

struct MoveInput {
    enum Key : uint8_t {
        Forward = 1, Back = 2, Left = 4, Right = 8, Jump = 16, Sneak = 32, Sprint = 64,
        Fly = 128,        // creative flying: no gravity, Jump rises, Sneak sinks (the server only
                          // accepts it from players in creative mode)
    };
    uint8_t keys = 0;     // Key flags held this frame
    float yaw = 0.0f;     // facing (degrees) - decides which way "forward" is
    float pitch = 0.0f;   // looking up/down (only used to show where the player looks)

    bool down(Key k) const { return (keys & k) != 0; }
};

// Movement keys the player may use (all by default); set with engine.set_player_controls
extern uint8_t g_MoveKeyMask;

struct MoveState {
    glm::vec3 position{0.0f};
    glm::vec3 velocity{0.0f};
    bool grounded = false;
};

// Longest step allowed (longer frames are cut to this, on both client and server).
constexpr float kMaxMoveStep = 0.1f;

// Moves `state` by one step of `dt` seconds with `input` (walking, jumping, swimming,
// gravity and block collision).
void stepMovement(MoveState& state, const MoveInput& input, float dt, const World& world);

// The movement keys held right now (Settings > Controls), plus the facing, plus Fly while
// flying. Defined in Player.cpp.
MoveInput readMoveInput(GLFWwindow* window, float yaw, float pitch);

// Creative flying (the player's game only; Player.cpp). updateFlight runs once per frame before
// moving: double-tapping Jump starts or stops flying when `canFly`; landing stops it.
extern bool g_Flying;
void updateFlight(GLFWwindow* window, bool canFly, bool grounded);
