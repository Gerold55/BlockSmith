#pragma once
// 3D models for entities: Bedrock-style .json geometry, Blockbench project files (.bbmodel: the
// file Blockbench saves, with its textures and animations inside) and Blitz3D .b3d meshes (the
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
    int textureIndex = 0;   // which of the model's textures (AnimatedModel::textures); 0 = the main one
};

struct Bone {
    std::string name;

    // Set from Lua for animation (on top of the pose stored in the model file)
    glm::vec3 position{0.0f};    // offset added to the rest position (0 = where the file puts it)
    glm::vec3 rotation{0.0f};    // extra rotation in degrees (x, y, z)
    glm::vec3 animScale{1.0f};   // extra scale (animations)

    // From the model file (never changed by animations, resetPose or drawBone)
    glm::vec3 restPosition{0.0f};   // where the bone sits in its parent (b3d node translation)
    glm::vec3 pivot{0.0f};       // point the bone rotates around (json)
    glm::vec3 scale{1.0f};
    glm::quat restRotation{1.0f, 0.0f, 0.0f, 0.0f};
    // From a Bedrock / Blockbench file: `rotation` is in Bedrock's terms (what Blockbench shows,
    // e.g. a leg at -60 on X swings forward) and is turned into the engine's when drawn
    bool bedrock = false;
    // The box around the bone's own cubes, in the model file's coordinates (blocks), if it has any
    // (where the hand is, for holding things in third person)
    bool hasBounds = false;
    glm::vec3 boundsMin{0.0f}, boundsMax{0.0f};

    std::shared_ptr<Mesh> mesh;                    // first mesh (kept for older code)
    std::vector<std::shared_ptr<Mesh>> meshes;     // all meshes attached to this bone
    std::vector<std::shared_ptr<Bone>> children;

    glm::mat4 getLocalTransform() const;
};

// A picture a model uses. .bbmodel files list theirs (usually embedded in the file); `name` is
// the file name to look for when it isn't embedded.
struct ModelTexture {
    std::string name;
    GLuint id = 0;          // 0 = not loaded (yet): the model's main texture is used instead
    bool owned = false;     // made from the file's embedded picture: deleted with the model
};

class AnimatedModel {
public:
    AnimatedModel() = default;
    ~AnimatedModel();
    AnimatedModel(const AnimatedModel&) = delete;
    AnimatedModel& operator=(const AnimatedModel&) = delete;

    void loadFromFile(const std::string& filepath);
    void loadFromJson(const std::string& filepath);
    void loadFromB3D(const std::string& filepath);
    void loadFromBBModel(const std::string& filepath);

    std::shared_ptr<Bone> getBone(const std::string& name);
    void draw(GLuint shaderProgram, const glm::mat4& worldTransform) const;

    bool loaded = false;
    bool isB3D = false;
    float scale = 1.0f;          // b3d models use 10 units per block, so they default to 0.1
    GLuint texture = 0;          // 0 = plain white. The main texture: meshes using texture 0
    std::string textureHint;     // texture file named inside the model (b3d TEXS chunk, a .bbmodel's
                                 // first texture when it isn't embedded)
    // A .bbmodel's textures, in the file's order (texture 0 is also `texture`, unless load_model
    // was given another). Faces using one that couldn't be loaded use the main texture.
    std::vector<ModelTexture> textures;
    GLuint textureFor(int index) const;

    std::map<std::string, std::shared_ptr<Bone>> boneMap;
    std::vector<std::shared_ptr<Bone>> rootBones;

    // Every bone back to the pose in the model file (no Lua/animation rotation or offset)
    void resetPose();
    // Draws one bone and everything attached to it (e.g. an arm for the first-person view),
    // in the model file's pose, with the bone's pivot at `world`'s origin. False if no such bone.
    bool drawBone(GLuint shaderProgram, const std::string& name, const glm::mat4& world);
    // Where a bone is now (its current pose), as draw() would put it with `world`: the matrix
    // that takes the model file's coordinates (blocks) to the world. False if no such bone.
    bool boneMatrix(const std::string& name, const glm::mat4& world, glm::mat4& out) const;

    // True if no bone is its own ancestor: following children from any bone - including bones
    // no root reaches - never comes back to a bone already on the path. False sets `cycleBone`
    // (if given) to a bone in the loop. drawBone refuses models that fail this.
    bool hierarchyIsAcyclic(std::string* cycleBone = nullptr) const;

private:
    mutable bool m_reportedCycle = false;   // the cycle error is printed once per model
    // hierarchyIsAcyclic's answer, worked out once: 0 = not known yet, 1 = no loop, 2 = a loop
    // (m_cycleBone names a bone in it). Bones are only linked while a file loads, so the loaders
    // reset it to 0 - that is the one place the hierarchy can change.
    mutable int m_hierarchyState = 0;
    mutable std::string m_cycleBone;
    bool hierarchyOk() const;
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
    int lerp = 0;                      // 0 linear, 1 catmullrom, 2 step, 3 bezier (Blockbench)
    // Bezier handles (per axis), relative to the key: time in seconds, value in the channel's units
    glm::vec3 leftTime{-0.1f}, leftValue{0.0f}, rightTime{0.1f}, rightValue{0.0f};
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
    // .animation.json, or the animations saved in a .bbmodel. False (and a console message) on failure
    bool loadFromFile(const std::string& path);
    std::shared_ptr<ModelAnimation> get(const std::string& name) const;   // "" = the first one
    std::vector<std::string> names() const;

    std::vector<std::shared_ptr<ModelAnimation>> animations;   // in file order
};
