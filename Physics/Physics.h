// Note that Im not entoreley sure if this code will work well
#pragma once
#include <glm/glm.hpp>
#include <vector>
#include "Types.h" // If the world bluck ENUMS are here


struct AABB {
    glm::vec3 min;
    glm::vec3 max;
    
    AABB() : min(0.0f), max(0.0f) {}
    AABB(const glm::vec3& center, const glm::vec3& halfExtents)
        : min(center - halfExtents), max(center + halfExtents) {}
    
    // get center point AABB
    glm::vec3 GetCenter() const { return (min + max) * 0.5f; }
    // get half-extents size/2 of AABB
    glm::vec3 GetHalfExtents() const { return (max - min) * 0.5f; }
    // get full size for AABB
    glm::vec3 GetSize() const { return max - min; }
    
    bool Intersects(const AABB& other) const {
        return (min.x <= other.max.x && max.x >= other.min.x) &&
               (min.y <= other.max.y && max.y >= other.min.y) &&
               (min.z <= other.max.z && max.z >= other.min.z);
    }
};

struct PhysicsBody {
    glm::vec3 position;   
    glm::vec3 velocity;
    glm::vec3 halfExtents;
    glm::vec3 gravity;
    float mass = 1.0f;
    bool isOnGround = false;
    bool isDynamic = true;
    
    float linearDrag = 0.0f; 
};

// The main physics solver class thingy
class PhysicsSolver {
public:
    static void MoveBody(PhysicsBody& body, 
                         const glm::vec3& movementDelta, 
                         const class World& world,
                         float deltaTime);
    static float RaycastForBlock(const glm::vec3& origin, 
                                 const glm::vec3& direction, 
                                 float maxDistance, 
                                 const class World& world);
    
private:
    static bool IsBlockSolid(const glm::ivec3& blockPos, const class World& world);
    
    static void ResolveCollisions(PhysicsBody& body, const class World& world);
    
    static void SweepAABB(PhysicsBody& body, 
                          const AABB& originalBox, 
                          const glm::vec3& sweepDelta, 
                          const class World& world);
};
