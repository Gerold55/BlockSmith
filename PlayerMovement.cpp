#include "PlayerMovement.h"
#include "World.h"
#include "Block.h"
#include "Fluids.h"
#include "DoorShape.h"
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
    const bool flying     = input.down(MoveInput::Fly);
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
    if (flying) {
        speed = keySprint ? 21.6f : 10.9f;  // Minecraft's flying speeds (blocks per second)
    } else if (!inWater && keySneak) {
        speed = 1.3f;                       // sneaking: slow walk
    } else if (!inWater && keySprint) {
        speed = 7.0f;
    }

    if (glm::length(moveDir) > 0.0f) {
        moveDir = glm::normalize(moveDir);
    }

    if (flying) {
        // Flying glides: speed builds up and dies away quickly instead of snapping
        const float k = 1.0f - std::exp(-12.0f * deltaTime);
        const glm::vec3 target(moveDir.x * speed, (keyJump ? 7.5f : 0.0f) - (keySneak ? 7.5f : 0.0f), moveDir.z * speed);
        velocity += (target - velocity) * k;
    } else {
        velocity.x = moveDir.x * speed;
        velocity.z = moveDir.z * speed;
    }

    // --- 3. GRAVITY, BUOYANCY & JUMPING/SWIMMING ---
    if (flying) {
        // no gravity: up and down come from Jump and Sneak above
    } else if (inWater) {
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

    // A block's collision box in world coordinates (false = you can walk through it). Most
    // blocks are a full cube; doors are their thin panel (DoorShape.h), wherever it is now.
    auto& registry = BlockRegistry::getInstance();
    auto solidBox = [&](int x, int y, int z, glm::vec3& mn, glm::vec3& mx) {
        const uint8_t blockId = world.getBlockAt({x, y, z});
        if (blockId == 0 || isFluidBlock(blockId)) return false;
        const auto& prop = registry.getBlock(blockId);
        if (!prop.isWalkable) return false;
        const glm::vec3 cell(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z));
        if (prop.pattern == "door") {
            door::box(world.getMetadataAt({x, y, z}), mn, mx);
            mn += cell;
            mx += cell;
        } else {
            mn = cell;
            mx = cell + glm::vec3(1.0f);
        }
        return true;
    };

    // --- Sneaking: don't walk off edges (Minecraft). On the ground, any horizontal move that
    //     would leave nothing under the player's feet is cut back, one axis at a time, so you can
    //     stand at the very edge of a block (and build out from it). ---
    // Is there something solid just under the player's feet if they stood at (px, pz)?
    auto supported = [&](float px, float pz) {
        const float x0 = px - halfWidth + skin, x1 = px + halfWidth - skin;
        const float z0 = pz - halfWidth + skin, z1 = pz + halfWidth - skin;
        const float yb = position.y - 0.06f;    // just below the feet
        const int y = static_cast<int>(std::floor(yb));
        for (int x = static_cast<int>(std::floor(x0)); x <= static_cast<int>(std::floor(x1)); ++x) {
            for (int z = static_cast<int>(std::floor(z0)); z <= static_cast<int>(std::floor(z1)); ++z) {
                glm::vec3 mn, mx;
                if (!solidBox(x, y, z, mn, mx)) continue;
                if (x0 < mx.x && x1 > mn.x && z0 < mx.z && z1 > mn.z && yb < mx.y && yb >= mn.y - 0.001f) return true;
            }
        }
        return false;
    };
    // (Standing on something right now - checked directly: the grounded flag alternates while
    // standing still)
    if (keySneak && !inWater && !flying && velocity.y <= 0.0f && supported(position.x, position.z)) {
        float dx = velocity.x * deltaTime, dz = velocity.z * deltaTime;
        constexpr float kStep = 0.05f;
        auto shrink = [&](float d) {
            if (std::fabs(d) <= kStep) return 0.0f;
            return d > 0.0f ? d - kStep : d + kStep;
        };
        while (dx != 0.0f && !supported(position.x + dx, position.z)) dx = shrink(dx);
        while (dz != 0.0f && !supported(position.x, position.z + dz)) dz = shrink(dz);
        while (dx != 0.0f && dz != 0.0f && !supported(position.x + dx, position.z + dz)) {
            dx = shrink(dx);
            dz = shrink(dz);
        }
        velocity.x = dx / deltaTime;
        velocity.z = dz / deltaTime;
    }

    // --- X Axis Collision & Move ---
    position.x += velocity.x * deltaTime;
    {
        const float x0 = position.x - halfWidth + skin, x1 = position.x + halfWidth - skin;
        const float y0 = position.y + skin, y1 = position.y + playerHeight - skin;
        const float z0 = position.z - halfWidth + skin, z1 = position.z + halfWidth - skin;
        for (int x = static_cast<int>(std::floor(x0)); x <= static_cast<int>(std::floor(x1)); ++x) {
            for (int y = static_cast<int>(std::floor(y0)); y <= static_cast<int>(std::floor(y1)); ++y) {
                for (int z = static_cast<int>(std::floor(z0)); z <= static_cast<int>(std::floor(z1)); ++z) {
                    glm::vec3 mn, mx;
                    if (!solidBox(x, y, z, mn, mx)) continue;
                    if (!(x0 < mx.x && x1 > mn.x && y0 < mx.y && y1 > mn.y && z0 < mx.z && z1 > mn.z)) continue;
                    if (velocity.x > 0.0f) {
                        position.x = mn.x - halfWidth - 0.001f;
                        velocity.x = 0.0f;
                    } else if (velocity.x < 0.0f) {
                        position.x = mx.x + halfWidth + 0.001f;
                        velocity.x = 0.0f;
                    }
                }
            }
        }
    }

    // --- Z Axis Collision & Move ---
    position.z += velocity.z * deltaTime;
    {
        const float x0 = position.x - halfWidth + skin, x1 = position.x + halfWidth - skin;
        const float y0 = position.y + skin, y1 = position.y + playerHeight - skin;
        const float z0 = position.z - halfWidth + skin, z1 = position.z + halfWidth - skin;
        for (int x = static_cast<int>(std::floor(x0)); x <= static_cast<int>(std::floor(x1)); ++x) {
            for (int y = static_cast<int>(std::floor(y0)); y <= static_cast<int>(std::floor(y1)); ++y) {
                for (int z = static_cast<int>(std::floor(z0)); z <= static_cast<int>(std::floor(z1)); ++z) {
                    glm::vec3 mn, mx;
                    if (!solidBox(x, y, z, mn, mx)) continue;
                    if (!(x0 < mx.x && x1 > mn.x && y0 < mx.y && y1 > mn.y && z0 < mx.z && z1 > mn.z)) continue;
                    if (velocity.z > 0.0f) {
                        position.z = mn.z - halfWidth - 0.001f;
                        velocity.z = 0.0f;
                    } else if (velocity.z < 0.0f) {
                        position.z = mx.z + halfWidth + 0.001f;
                        velocity.z = 0.0f;
                    }
                }
            }
        }
    }

    // --- Y Axis Collision & Move ---
    position.y += velocity.y * deltaTime;
    grounded = false;
    {
        const float x0 = position.x - halfWidth + skin, x1 = position.x + halfWidth - skin;
        const float y0 = position.y, y1 = position.y + playerHeight - skin;
        const float z0 = position.z - halfWidth + skin, z1 = position.z + halfWidth - skin;
        for (int x = static_cast<int>(std::floor(x0)); x <= static_cast<int>(std::floor(x1)); ++x) {
            for (int y = static_cast<int>(std::floor(y0)); y <= static_cast<int>(std::floor(y1)); ++y) {
                for (int z = static_cast<int>(std::floor(z0)); z <= static_cast<int>(std::floor(z1)); ++z) {
                    glm::vec3 mn, mx;
                    if (!solidBox(x, y, z, mn, mx)) continue;
                    if (!(x0 < mx.x && x1 > mn.x && y0 < mx.y && y1 >= mn.y && z0 < mx.z && z1 > mn.z)) continue;
                    if (velocity.y < 0.0f) {
                        position.y = mx.y;
                        velocity.y = 0.0f;
                        grounded = true;
                    } else if (velocity.y > 0.0f) {
                        position.y = mn.y - playerHeight;
                        velocity.y = 0.0f;
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
