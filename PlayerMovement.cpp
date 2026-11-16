#include "PlayerMovement.h"
#include "World.h"
#include "Block.h"
#include "Fluids.h"
#include <cmath>

// Moved here from Player::update unchanged, so singleplayer movement feels exactly the same.
void stepMovement(MoveState& state, const MoveInput& input, float deltaTime, const World& world) {
    if (deltaTime > kMaxMoveStep) deltaTime = kMaxMoveStep;
    if (deltaTime <= 0.0f) return;
    glm::vec3& position = state.position;
    glm::vec3& velocity = state.velocity;
    bool& grounded = state.grounded;

    // --- 1. CHECK IF PLAYER IS SUBMERGED IN A SWIMMABLE FLUID (water, lava, modded) ---
    bool inWater = false;
    float swimSpeed = 2.2f;
    int headX = static_cast<int>(std::floor(position.x));
    int headY = static_cast<int>(std::floor(position.y + 1.0f));
    int headZ = static_cast<int>(std::floor(position.z));
    uint8_t blockAtHead = world.getBlockAt({headX, headY, headZ});
    FluidRegistry::get().ensureFresh();
    if (const FluidDef* fluid = FluidRegistry::get().forBlock(blockAtHead)) {
        if (fluid->swimmable) {
            inWater = true;
            swimSpeed = fluid->swimSpeed;
        }
    }
    auto isFluidBlock = [](uint8_t id) { return FluidRegistry::get().forBlock(id) != nullptr; };

    // --- 2. HANDLE INPUT & MOVEMENT DIRECTION ---
    const bool keyForward = input.down(MoveInput::Forward);
    const bool keyBack    = input.down(MoveInput::Back);
    const bool keyLeft    = input.down(MoveInput::Left);
    const bool keyRight   = input.down(MoveInput::Right);
    const bool keyJump    = input.down(MoveInput::Jump);
    const bool keySneak   = input.down(MoveInput::Sneak);
    const bool keySprint  = input.down(MoveInput::Sprint);
    const float yaw = input.yaw;
    const float pitch = input.pitch;

    glm::vec3 forward(
        cos(glm::radians(yaw)) * cos(glm::radians(pitch)),
        sin(glm::radians(pitch)),
        sin(glm::radians(yaw)) * cos(glm::radians(pitch))
    );
    forward = glm::normalize(glm::vec3(forward.x, 0.0f, forward.z));
    glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f)));

    glm::vec3 moveDir(0.0f);
    if (keyForward) moveDir += forward;
    if (keyBack)    moveDir -= forward;
    if (keyRight)   moveDir += right;
    if (keyLeft)    moveDir -= right;

    float speed = inWater ? swimSpeed : 4.3f;
    if (!inWater && keySneak) {
        speed = 1.3f;                       // sneaking: slow walk
    } else if (!inWater && keySprint) {
        speed = 7.0f;
    }

    if (glm::length(moveDir) > 0.0f) {
        moveDir = glm::normalize(moveDir);
    }

    velocity.x = moveDir.x * speed;
    velocity.z = moveDir.z * speed;

    // --- 3. GRAVITY, BUOYANCY & JUMPING/SWIMMING ---
    if (inWater) {
        float waterGravity = -4.0f;
        velocity.y += waterGravity * deltaTime;
        velocity.y *= 0.85f;

        if (keyJump) {
            velocity.y = 4.0f;              // swim up
        } else if (keySneak) {
            velocity.y = -3.0f;             // swim down
        }
    } else {
        float gravity = -22.0f;
        if (!grounded) {
            velocity.y += gravity * deltaTime;
        }

        if (keyJump && grounded) {
            velocity.y = 8.0f;
            grounded = false;
        }
    }

    // --- 4. AXIS-SEPARATED AABB COLLISION RESOLUTION ---
    float playerWidth = 0.52f; // Slim profile to easily navigate caves and 1x1 shafts
    float playerHeight = 1.8f;
    float halfWidth = playerWidth / 2.0f;
    float skin = 0.02f;

    // --- X Axis Collision & Move ---
    position.x += velocity.x * deltaTime;
    {
        int minX = static_cast<int>(std::floor(position.x - halfWidth + skin));
        int maxX = static_cast<int>(std::floor(position.x + halfWidth - skin));
        int minY = static_cast<int>(std::floor(position.y + skin));
        int maxY = static_cast<int>(std::floor(position.y + playerHeight - skin));
        int minZ = static_cast<int>(std::floor(position.z - halfWidth + skin));
        int maxZ = static_cast<int>(std::floor(position.z + halfWidth - skin));

        for (int x = minX; x <= maxX; ++x) {
            for (int y = minY; y <= maxY; ++y) {
                for (int z = minZ; z <= maxZ; ++z) {
                    uint8_t blockId = world.getBlockAt({x, y, z});
                    if (blockId != 0) {
                        const auto& prop = BlockRegistry::getInstance().getBlock(blockId);
                        if (isFluidBlock(blockId) || !prop.isWalkable) continue;

                        if (velocity.x > 0.0f) {
                            position.x = static_cast<float>(x) - halfWidth - 0.001f;
                            velocity.x = 0.0f;
                        } else if (velocity.x < 0.0f) {
                            position.x = static_cast<float>(x) + 1.0f + halfWidth + 0.001f;
                            velocity.x = 0.0f;
                        }
                    }
                }
            }
        }
    }

    // --- Z Axis Collision & Move ---
    position.z += velocity.z * deltaTime;
    {
        int minX = static_cast<int>(std::floor(position.x - halfWidth + skin));
        int maxX = static_cast<int>(std::floor(position.x + halfWidth - skin));
        int minY = static_cast<int>(std::floor(position.y + skin));
        int maxY = static_cast<int>(std::floor(position.y + playerHeight - skin));
        int minZ = static_cast<int>(std::floor(position.z - halfWidth + skin));
        int maxZ = static_cast<int>(std::floor(position.z + halfWidth - skin));

        for (int x = minX; x <= maxX; ++x) {
            for (int y = minY; y <= maxY; ++y) {
                for (int z = minZ; z <= maxZ; ++z) {
                    uint8_t blockId = world.getBlockAt({x, y, z});
                    if (blockId != 0) {
                        const auto& prop = BlockRegistry::getInstance().getBlock(blockId);
                        if (isFluidBlock(blockId) || !prop.isWalkable) continue;

                        if (velocity.z > 0.0f) {
                            position.z = static_cast<float>(z) - halfWidth - 0.001f;
                            velocity.z = 0.0f;
                        } else if (velocity.z < 0.0f) {
                            position.z = static_cast<float>(z) + 1.0f + halfWidth + 0.001f;
                            velocity.z = 0.0f;
                        }
                    }
                }
            }
        }
    }

    // --- Y Axis Collision & Move ---
    position.y += velocity.y * deltaTime;
    grounded = false;
    {
        int minX = static_cast<int>(std::floor(position.x - halfWidth + skin));
        int maxX = static_cast<int>(std::floor(position.x + halfWidth - skin));
        int minY = static_cast<int>(std::floor(position.y));
        int maxY = static_cast<int>(std::floor(position.y + playerHeight - skin));
        int minZ = static_cast<int>(std::floor(position.z - halfWidth + skin));
        int maxZ = static_cast<int>(std::floor(position.z + halfWidth - skin));

        for (int x = minX; x <= maxX; ++x) {
            for (int y = minY; y <= maxY; ++y) {
                for (int z = minZ; z <= maxZ; ++z) {
                    uint8_t blockId = world.getBlockAt({x, y, z});
                    if (blockId != 0) {
                        const auto& prop = BlockRegistry::getInstance().getBlock(blockId);
                        if (isFluidBlock(blockId) || !prop.isWalkable) continue;

                        if (velocity.y < 0.0f) {
                            position.y = static_cast<float>(y) + 1.0f;
                            velocity.y = 0.0f;
                            grounded = true;
                        } else if (velocity.y > 0.0f) {
                            position.y = static_cast<float>(y) - playerHeight;
                            velocity.y = 0.0f;
                        }
                    }
                }
            }
        }
    }

    // --- VOID SAFETY NET ---
    if (position.y < -10.0f) {
        position = glm::vec3(8.0f, 75.0f, 8.0f);
        velocity = glm::vec3(0.0f);
        grounded = true;
    }
}
