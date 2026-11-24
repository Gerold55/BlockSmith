#include "Physics.h"
#include "World.h" // THIS SHOUDL HAVE THE GetBlock header
#include <cmath>
#include <algorithm>

bool PhysicsSolver::IsBlockSolid(const glm::ivec3& blockPos, const World& world) {
    BlockType block = world.GetBlock(blockPos.x, blockPos.y, blockPos.z);
    return block != BlockType::AIR && block != BlockType::WATER; 
}

void PhysicsSolver::MoveBody(PhysicsBody& body, 
                             const glm::vec3& movementDelta, 
                             const World& world,
                             float deltaTime) {
    if (!body.isDynamic) return;
    body.velocity += body.gravity * deltaTime;
    body.velocity -= body.velocity * body.linearDrag * deltaTime;
    glm::vec3 totalMovement = movementDelta + (body.velocity * deltaTime);
    AABB originalBox(body.position, body.halfExtents);
    SweepAABB(body, originalBox, totalMovement, world);
    body.position += totalMovement;
    

    body.isOnGround = false;
    AABB groundCheckBox(body.position - glm::vec3(0, 0.01f, 0), body.halfExtents);
    for (int x = -1; x <= 1; ++x) {
        for (int z = -1; z <= 1; ++z) {
            glm::ivec3 blockPos = glm::ivec3(groundCheckBox.GetCenter()) + glm::ivec3(x, -1, z);
            if (IsBlockSolid(blockPos, world)) {
                body.isOnGround = true;
                break;
            }
        }
        if (body.isOnGround) break;
    }
}

void PhysicsSolver::SweepAABB(PhysicsBody& body, 
                              const AABB& originalBox, 
                              const glm::vec3& sweepDelta, 
                              const World& world) {
    
    glm::vec3 newMovement = sweepDelta;
    const float epsilon = 0.001f; // Small tolerance for float errors.

    if (newMovement.y != 0.0f) {
        AABB movedBox = originalBox;
        movedBox.min.y += newMovement.y;
        movedBox.max.y += newMovement.y;
        
 
        int startY = (int)std::floor(originalBox.min.y);
        int endY = (int)std::floor(originalBox.max.y + newMovement.y);
        
        for (int y = startY; y <= endY; ++y) {
            for (int x = (int)std::floor(originalBox.min.x); x <= (int)std::floor(originalBox.max.x); ++x) {
                for (int z = (int)std::floor(originalBox.min.z); z <= (int)std::floor(originalBox.max.z); ++z) {
                    if (IsBlockSolid({x, y, z}, world)) {
                        if (newMovement.y > 0) { // Moving up
                            newMovement.y = std::min(newMovement.y, (float)y - originalBox.max.y - epsilon);
                        } else if (newMovement.y < 0) { // Moving down
                            newMovement.y = std::max(newMovement.y, (float)(y + 1) - originalBox.min.y + epsilon);
                        }
                    }
                }
            }
        }
    }

    if (newMovement.x != 0.0f) {
        AABB movedBox = originalBox;
        movedBox.min.x += newMovement.x;
        movedBox.max.x += newMovement.x;
        movedBox.min.y += newMovement.y;
        movedBox.max.y += newMovement.y;
        
        int startX = (int)std::floor(originalBox.min.x);
        int endX = (int)std::floor(originalBox.max.x + newMovement.x);
        for (int x = startX; x <= endX; ++x) {
            for (int y = (int)std::floor(movedBox.min.y); y <= (int)std::floor(movedBox.max.y); ++y) {
                for (int z = (int)std::floor(movedBox.min.z); z <= (int)std::floor(movedBox.max.z); ++z) {
                    if (IsBlockSolid({x, y, z}, world)) {
                        if (newMovement.x > 0) {
                            newMovement.x = std::min(newMovement.x, (float)x - originalBox.max.x - epsilon);
                        } else if (newMovement.x < 0) {
                            newMovement.x = std::max(newMovement.x, (float)(x + 1) - originalBox.min.x + epsilon);
                        }
                    }
                }
            }
        }
    }

    if (newMovement.z != 0.0f) {
        AABB movedBox = originalBox;
        movedBox.min.x += newMovement.x;
        movedBox.max.x += newMovement.x;
        movedBox.min.z += newMovement.z;
        movedBox.max.z += newMovement.z;
        
        int startZ = (int)std::floor(originalBox.min.z);
        int endZ = (int)std::floor(originalBox.max.z + newMovement.z);
        for (int z = startZ; z <= endZ; ++z) {
            for (int x = (int)std::floor(movedBox.min.x); x <= (int)std::floor(movedBox.max.x); ++x) {
                for (int y = (int)std::floor(movedBox.min.y); y <= (int)std::floor(movedBox.max.y); ++y) {
                    if (IsBlockSolid({x, y, z}, world)) {
                        if (newMovement.z > 0) {
                            newMovement.z = std::min(newMovement.z, (float)z - originalBox.max.z - epsilon);
                        } else if (newMovement.z < 0) {
                            newMovement.z = std::max(newMovement.z, (float)(z + 1) - originalBox.min.z + epsilon);
                        }
                    }
                }
            }
        }
    }
    
    if (newMovement.y != sweepDelta.y) body.velocity.y = 0;
    if (newMovement.x != sweepDelta.x) body.velocity.x = 0;
    if (newMovement.z != sweepDelta.z) body.velocity.z = 0;
    
    body.position += (newMovement - sweepDelta);
    // Note: The caller (MoveBody) will add the newMovement to position later.
}

void PhysicsSolver::ResolveCollisions(PhysicsBody& body, const World& world) {
    // this funct would be called after a pos update
    // for a simpler stable approach, thus I shalt use the axis separated method.
    // (The implementation would be similar to the one I have above but callable separately.)
}

void PhysicsSolver::MoveBody(PhysicsBody& body, 
                             const glm::vec3& movementDelta, 
                             const World& world,
                             float deltaTime) {
    if (!body.isDynamic) return;

    body.velocity += body.gravity * deltaTime;
    body.velocity -= body.velocity * body.linearDrag * deltaTime;
    
    glm::vec3 displacement = movementDelta + (body.velocity * deltaTime);
    glm::vec3 originalPos = body.position;
    

    body.position.y += displacement.y;
    AABB box(body.position, body.halfExtents);
    for (int x = (int)std::floor(box.min.x); x <= (int)std::floor(box.max.x); ++x) {
        for (int z = (int)std::floor(box.min.z); z <= (int)std::floor(box.max.z); ++z) {
            for (int y = (int)std::floor(box.min.y); y <= (int)std::floor(box.max.y); ++y) {
                if (IsBlockSolid({x, y, z}, world)) {
                    if (displacement.y > 0) {
                        body.position.y = (float)y - body.halfExtents.y - 0.001f;
                    } else if (displacement.y < 0) {
                        body.position.y = (float)(y + 1) + body.halfExtents.y + 0.001f;
                    }
                    body.velocity.y = 0;
                    displacement.y = body.position.y - originalPos.y;
                    box = AABB(body.position, body.halfExtents);
                }
            }
        }
    }

    body.position.x += displacement.x;
    box = AABB(body.position, body.halfExtents);
    for (int y = (int)std::floor(box.min.y); y <= (int)std::floor(box.max.y); ++y) {
        for (int z = (int)std::floor(box.min.z); z <= (int)std::floor(box.max.z); ++z) {
            for (int x = (int)std::floor(box.min.x); x <= (int)std::floor(box.max.x); ++x) {
                if (IsBlockSolid({x, y, z}, world)) {
                    if (displacement.x > 0) {
                        body.position.x = (float)x - body.halfExtents.x - 0.001f;
                    } else if (displacement.x < 0) {
                        body.position.x = (float)(x + 1) + body.halfExtents.x + 0.001f;
                    }
                    body.velocity.x = 0;
                    displacement.x = body.position.x - originalPos.x;
                    box = AABB(body.position, body.halfExtents);
                }
            }
        }
    }
    
    body.position.z += displacement.z;
    box = AABB(body.position, body.halfExtents);
    for (int x = (int)std::floor(box.min.x); x <= (int)std::floor(box.max.x); ++x) {
        for (int y = (int)std::floor(box.min.y); y <= (int)std::floor(box.max.y); ++y) {
            for (int z = (int)std::floor(box.min.z); z <= (int)std::floor(box.max.z); ++z) {
                if (IsBlockSolid({x, y, z}, world)) {
                    if (displacement.z > 0) {
                        body.position.z = (float)z - body.halfExtents.z - 0.001f;
                    } else if (displacement.z < 0) {
                        body.position.z = (float)(z + 1) + body.halfExtents.z + 0.001f;
                    }
                    body.velocity.z = 0;
                    displacement.z = body.position.z - originalPos.z;
                    box = AABB(body.position, body.halfExtents);
                }
            }
        }
    }
    
    // update ground stat
    body.isOnGround = false;
    glm::ivec3 footPos = glm::ivec3(glm::floor(body.position.x), 
                                    glm::floor(body.position.y - body.halfExtents.y - 0.1f), 
                                    glm::floor(body.position.z));
    if (IsBlockSolid(footPos, world)) {
        body.isOnGround = true;
    }
}
