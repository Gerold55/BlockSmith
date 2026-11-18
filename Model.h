#pragma once
// 3D models for entities: Bedrock-style .json geometry and Blitz3D .b3d meshes (the
// format Luanti mods use). Bones can be posed from Lua with set_rotation / set_position.

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <map>
#include <memory>
#include <string>
#include <vector>

struct ModelVertex {
    glm::vec3 position{0.0f};
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    glm::vec2 texCoords{0.0f};
};

class Mesh {
public:
    explicit Mesh(const std::vector<ModelVertex>& vertices);
    ~Mesh();
    Mesh(const Mesh&) = delete;
    Mesh& operator=(const Mesh&) = delete;

    void draw() const;

    GLuint VAO = 0;
    GLuint VBO = 0;
    int vertexCount = 0;
};

struct Bone {
    std::string name;

    // Set from Lua for animation (on top of the pose stored in the model file)
    glm::vec3 position{0.0f};    // offset (b3d: starts at the node's position)
    glm::vec3 rotation{0.0f};    // extra rotation in degrees (x, y, z)
    glm::vec3 animScale{1.0f};   // extra scale (animations)

    // From the model file
    glm::vec3 pivot{0.0f};       // point the bone rotates around (json)
    glm::vec3 scale{1.0f};
    glm::quat restRotation{1.0f, 0.0f, 0.0f, 0.0f};

    std::shared_ptr<Mesh> mesh;                    // first mesh (kept for older code)
    std::vector<std::shared_ptr<Mesh>> meshes;     // all meshes attached to this bone
    std::vector<std::shared_ptr<Bone>> children;

    glm::mat4 getLocalTransform() const;
};

class AnimatedModel {
public:
    void loadFromFile(const std::string& filepath);
    void loadFromJson(const std::string& filepath);
    void loadFromB3D(const std::string& filepath);

    std::shared_ptr<Bone> getBone(const std::string& name);
    void draw(GLuint shaderProgram, const glm::mat4& worldTransform) const;

    bool loaded = false;
    bool isB3D = false;
    float scale = 1.0f;          // b3d models use 10 units per block, so they default to 0.1
    GLuint texture = 0;          // 0 = plain white
    std::string textureHint;     // texture file named inside the model (b3d TEXS chunk)

    std::map<std::string, std::shared_ptr<Bone>> boneMap;
    std::vector<std::shared_ptr<Bone>> rootBones;

    // Every bone back to the pose in the model file (no Lua/animation rotation or offset)
    void resetPose();
    // Draws one bone and everything attached to it (e.g. an arm for the first-person view),
    // in the model file's pose, with the bone's pivot at `world`'s origin. False if no such bone.
    bool drawBone(GLuint shaderProgram, const std::string& name, const glm::mat4& world);
};

// ===========================================================================
// Animations: Bedrock / Blockbench .animation.json files. A file holds any number of
// animations; each moves bones (rotation in degrees, position in pixels, scale) over time
// with keyframes - linear, catmullrom (smooth) or step. Lua decides which animation plays,
// at what time and how strongly (see the player mod).
// ===========================================================================
struct AnimKey {
    float time = 0.0f;
    glm::vec3 pre{0.0f}, post{0.0f};   // value arriving at / leaving this key (usually the same)
    int lerp = 0;                      // 0 linear, 1 catmullrom, 2 step
};

struct AnimChannel {
    std::vector<AnimKey> keys;         // sorted by time; one key = a constant value
    bool empty() const { return keys.empty(); }
    glm::vec3 sample(float t) const;
};

struct BoneAnimation {
    std::string bone;
    AnimChannel rotation, position, scale;
};

class ModelAnimation {
public:
    std::string name;
    float length = 0.0f;               // seconds (animation_length, or the last key)
    bool loop = false;                 // the file's "loop"
    std::vector<BoneAnimation> bones;

    // Poses `model` at `time` seconds (wrapped if the animation loops, else held at the end).
    // weight 0..1 blends from the bone's current pose to this one, so one animation can fade
    // into another. Bones the model doesn't have are skipped.
    void apply(AnimatedModel& model, float time, float weight = 1.0f) const;
};

class AnimationSet {
public:
    bool loadFromFile(const std::string& path);   // false (and a console message) on failure
    std::shared_ptr<ModelAnimation> get(const std::string& name) const;   // "" = the first one
    std::vector<std::string> names() const;

    std::vector<std::shared_ptr<ModelAnimation>> animations;   // in file order
};
