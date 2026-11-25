#include "Model.h"
#include <unordered_map>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <cstdlib>
#include <cmath>
#include <algorithm>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <stb_image.h>
#include <cctype>
#include <cstring>
#include <set>

using json = nlohmann::json;

namespace {

// Euler angles in degrees, applied X first, then Y, then Z.
// Bedrock rotations -> the engine's. Blockbench reads a Bedrock rotation (x, y, z) as (-x, -y, z)
// in its own space; the engine draws Bedrock models mirrored left-right back to what Blockbench
// shows (AnimatedModel::draw), which turns that into (-x, y, -z) here. With it, every pose,
// animation and rest rotation looks exactly as it does in Blockbench: a leg at -60 on X swings
// forward, a knee at +X bends back, a head at +X looks down.
glm::vec3 bedrockToEngine(const glm::vec3& deg) { return glm::vec3(-deg.x, deg.y, -deg.z); }

glm::mat4 eulerMatrix(const glm::vec3& deg) {
    glm::mat4 m(1.0f);
    m = glm::rotate(m, glm::radians(deg.z), glm::vec3(0, 0, 1));
    m = glm::rotate(m, glm::radians(deg.y), glm::vec3(0, 1, 0));
    m = glm::rotate(m, glm::radians(deg.x), glm::vec3(1, 0, 0));
    return m;
}

// 1x1 white texture for models without one
GLuint whiteTexture() {
    static GLuint tex = 0;
    if (tex == 0) {
        const unsigned char white[4] = { 255, 255, 255, 255 };
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    }
    return tex;
}

// Flat normals for triangles that came without normals
void computeFlatNormals(std::vector<ModelVertex>& tris) {
    for (size_t i = 0; i + 2 < tris.size(); i += 3) {
        const glm::vec3 n = glm::cross(tris[i + 1].position - tris[i].position,
                                       tris[i + 2].position - tris[i].position);
        const float len = glm::length(n);
        const glm::vec3 nn = len > 0.0f ? n / len : glm::vec3(0, 1, 0);
        tris[i].normal = tris[i + 1].normal = tris[i + 2].normal = nn;
    }
}

} // namespace

// ===========================================================================
// Mesh
// ===========================================================================

Mesh::Mesh(const std::vector<ModelVertex>& vertices) {
    vertexCount = static_cast<int>(vertices.size());
    if (vertexCount == 0) return;

    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);

    glBindVertexArray(VAO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(ModelVertex), vertices.data(), GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(ModelVertex), (void*)offsetof(ModelVertex, position));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(ModelVertex), (void*)offsetof(ModelVertex, normal));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(ModelVertex), (void*)offsetof(ModelVertex, texCoords));
    glEnableVertexAttribArray(2);

    glBindVertexArray(0);
}

Mesh::~Mesh() {
    if (VBO) glDeleteBuffers(1, &VBO);
    if (VAO) glDeleteVertexArrays(1, &VAO);
}

void Mesh::draw() const {
    if (VAO == 0 || vertexCount == 0) return;
    glBindVertexArray(VAO);
    glDrawArrays(GL_TRIANGLES, 0, vertexCount);
    glBindVertexArray(0);
}

// ===========================================================================
// Bone
// ===========================================================================

// position, then rotate/scale around the pivot (file pose first, then the Lua animation rotation)
glm::mat4 Bone::getLocalTransform() const {
    glm::mat4 m(1.0f);
    m = glm::translate(m, restPosition + position + pivot);   // file's rest position + animation offset
    m *= glm::mat4_cast(restRotation);
    m *= eulerMatrix(bedrock ? bedrockToEngine(rotation) : rotation);
    m = glm::scale(m, scale * animScale);
    m = glm::translate(m, -pivot);
    return m;
}

// ===========================================================================
// AnimatedModel
// ===========================================================================

namespace {
bool endsWithNoCase(const std::string& s, const char* suffix) {
    const size_t n = std::strlen(suffix);
    if (s.size() < n) return false;
    for (size_t i = 0; i < n; ++i)
        if (std::tolower(static_cast<unsigned char>(s[s.size() - n + i])) != suffix[i]) return false;
    return true;
}
} // namespace

void AnimatedModel::loadFromFile(const std::string& filepath) {
    if (endsWithNoCase(filepath, ".b3d")) {
        loadFromB3D(filepath);
    } else if (endsWithNoCase(filepath, ".bbmodel")) {
        loadFromBBModel(filepath);
    } else {
        loadFromJson(filepath);
    }
}

AnimatedModel::~AnimatedModel() {
    for (const ModelTexture& t : textures)
        if (t.owned && t.id) glDeleteTextures(1, &t.id);
}

GLuint AnimatedModel::textureFor(int index) const {
    if (index > 0 && index < static_cast<int>(textures.size()) && textures[index].id) return textures[index].id;
    return texture != 0 ? texture : 0;
}

std::shared_ptr<Bone> AnimatedModel::getBone(const std::string& name) {
    auto it = boneMap.find(name);
    return it != boneMap.end() ? it->second : nullptr;
}

// `bound`: the texture bound now (meshes using another of the model's textures switch it)
static void drawBoneRecursive(const AnimatedModel& model, const std::shared_ptr<Bone>& bone, GLint modelLoc,
                              const glm::mat4& parentMatrix, GLuint& bound) {
    const glm::mat4 globalMatrix = parentMatrix * bone->getLocalTransform();

    if (!bone->meshes.empty()) {
        glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(globalMatrix));
        for (const auto& m : bone->meshes) {
            GLuint tex = model.textureFor(m->textureIndex);
            if (tex == 0) tex = whiteTexture();
            if (tex != bound) { glBindTexture(GL_TEXTURE_2D, tex); bound = tex; }
            m->draw();
        }
    }
    for (const auto& child : bone->children) {
        drawBoneRecursive(model, child, modelLoc, globalMatrix, bound);
    }
}

void AnimatedModel::draw(GLuint shaderProgram, const glm::mat4& worldTransform) const {
    if (!loaded) return;

    glUseProgram(shaderProgram);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture != 0 ? texture : whiteTexture());
    const GLint texLoc = glGetUniformLocation(shaderProgram, "texture_diffuse1");
    if (texLoc >= 0) glUniform1i(texLoc, 0);

    const GLint modelLoc = glGetUniformLocation(shaderProgram, "model");
    glm::mat4 root = glm::scale(worldTransform, glm::vec3(scale));
    // Bedrock / Blockbench models store X mirrored (Blockbench flips it back when it shows them):
    // without flipping it here too, the whole model came out as its mirror image - the right
    // arm on the left, turns and tilts the wrong way round. Mirroring flips which way the
    // triangles wind, so the front-face rule is swapped while drawing.
    // (swapped from whatever it is now: drawn in a mirror's reflection (Mirrors.h) the rule is
    // already turned round, and setting it outright showed the inside of the model's far side)
    const bool mirror = !isB3D;
    GLint frontWas = GL_CCW;
    glGetIntegerv(GL_FRONT_FACE, &frontWas);
    if (mirror) {
        root = glm::scale(root, glm::vec3(-1.0f, 1.0f, 1.0f));
        glFrontFace(frontWas == GL_CW ? GL_CCW : GL_CW);
    }
    GLuint bound = texture != 0 ? texture : whiteTexture();
    for (const auto& rootBone : rootBones) {
        drawBoneRecursive(*this, rootBone, modelLoc, root, bound);
    }
    if (mirror) glFrontFace(static_cast<GLenum>(frontWas));
}

// ===========================================================================
// JSON (Bedrock / Blockbench "Bedrock Model" geometry)
//   Supports the 1.12+ format { "minecraft:geometry": [ ... ] } and the older
//   { "geometry.name": { ... } } format. Coordinates are in pixels (16 per block).
//   Per bone: pivot, rotation, parent, and any number of cubes.
//   Per cube: origin, size, uv (box UV [u, v] or per-face), inflate, mirror, pivot, rotation.
// ===========================================================================

namespace {

glm::vec3 vec3Of(const json& j, const char* key, glm::vec3 fallback) {
    if (!j.contains(key) || !j[key].is_array() || j[key].size() < 3) return fallback;
    return glm::vec3(j[key][0].get<float>(), j[key][1].get<float>(), j[key][2].get<float>());
}

struct UVRect { float u0, v0, u1, v1; bool use = true; int turns = 0; };   // pixels; (u0,v0) = top-left;
                                                                         // turns: quarter turns clockwise

enum Face { EAST, WEST, UP, DOWN, SOUTH, NORTH };   // +X, -X, +Y, -Y, +Z, -Z

void appendCube(const json& cube, float texW, float texH, bool boneMirror, std::vector<ModelVertex>& out) {
    const glm::vec3 origin = vec3Of(cube, "origin", glm::vec3(0.0f));
    const glm::vec3 size   = vec3Of(cube, "size", glm::vec3(0.0f));
    const float inflate    = cube.value("inflate", 0.0f);
    const bool mirror      = cube.value("mirror", boneMirror);

    const glm::vec3 lo = (origin - inflate) / 16.0f;
    const glm::vec3 hi = (origin + size + inflate) / 16.0f;

    // ---- UV rectangles for each face (in pixels)
    UVRect uv[6];
    const float w = size.x, h = size.y, d = size.z;
    if (cube.contains("uv") && cube["uv"].is_array() && cube["uv"].size() >= 2) {
        // Box UV: the classic unfolded-box layout starting at (u, v)
        const float u = cube["uv"][0].get<float>(), v = cube["uv"][1].get<float>();
        uv[EAST]  = { u,                 v + d, u + d,             v + d + h };
        uv[NORTH] = { u + d,             v + d, u + d + w,         v + d + h };
        uv[WEST]  = { u + d + w,         v + d, u + d + w + d,     v + d + h };
        uv[SOUTH] = { u + d + w + d,     v + d, u + d + w + d + w, v + d + h };
        uv[UP]    = { u + d,             v,     u + d + w,         v + d };
        uv[DOWN]  = { u + d + w,         v + d, u + d + w + w,     v };       // stored upside down
        if (mirror) {
            std::swap(uv[EAST], uv[WEST]);
            for (auto& r : uv) std::swap(r.u0, r.u1);
        }
    } else if (cube.contains("uv") && cube["uv"].is_object()) {
        // Per-face UV: { "north": { "uv": [u, v], "uv_size": [w, h] }, ... }
        const char* names[6] = { "east", "west", "up", "down", "south", "north" };
        for (int f = 0; f < 6; ++f) {
            uv[f].use = false;
            if (!cube["uv"].contains(names[f])) continue;
            const json& fj = cube["uv"][names[f]];
            if (!fj.contains("uv") || fj["uv"].size() < 2) continue;
            const float fu = fj["uv"][0].get<float>(), fv = fj["uv"][1].get<float>();
            float fw = 0.0f, fh = 0.0f;
            if (fj.contains("uv_size") && fj["uv_size"].size() >= 2) {
                fw = fj["uv_size"][0].get<float>();
                fh = fj["uv_size"][1].get<float>();
            }
            uv[f] = { fu, fv, fu + fw, fv + fh, true };
            // "uv_rotation": 0 / 90 / 180 / 270 - the picture turned clockwise on the face
            if (fj.contains("uv_rotation") && fj["uv_rotation"].is_number()) {
                const int deg = static_cast<int>(std::lround(fj["uv_rotation"].get<double>() / 90.0)) * 90;
                uv[f].turns = ((deg / 90) % 4 + 4) % 4;
            }
        }
    } else {
        for (auto& r : uv) r = { 0.0f, 0.0f, texW, texH, true };   // no UV: whole texture
    }

    // ---- Corners of each face, seen from outside: bottom-left, bottom-right, top-right, top-left
    const float x0 = lo.x, y0 = lo.y, z0 = lo.z, x1 = hi.x, y1 = hi.y, z1 = hi.z;
    const glm::vec3 corners[6][4] = {
        { {x1, y0, z1}, {x1, y0, z0}, {x1, y1, z0}, {x1, y1, z1} },   // EAST  +X
        { {x0, y0, z0}, {x0, y0, z1}, {x0, y1, z1}, {x0, y1, z0} },   // WEST  -X
        { {x0, y1, z1}, {x1, y1, z1}, {x1, y1, z0}, {x0, y1, z0} },   // UP    +Y
        { {x0, y0, z0}, {x1, y0, z0}, {x1, y0, z1}, {x0, y0, z1} },   // DOWN  -Y
        { {x0, y0, z1}, {x1, y0, z1}, {x1, y1, z1}, {x0, y1, z1} },   // SOUTH +Z
        { {x1, y0, z0}, {x0, y0, z0}, {x0, y1, z0}, {x1, y1, z0} },   // NORTH -Z
    };
    const glm::vec3 normals[6] = { {1,0,0}, {-1,0,0}, {0,1,0}, {0,-1,0}, {0,0,1}, {0,0,-1} };

    // ---- Optional cube rotation around its own pivot
    glm::mat4 rot(1.0f);
    glm::vec3 cubePivot(0.0f);
    const bool rotated = cube.contains("rotation");
    if (rotated) {
        rot = eulerMatrix(bedrockToEngine(vec3Of(cube, "rotation", glm::vec3(0.0f))));
        cubePivot = vec3Of(cube, "pivot", origin + size * 0.5f) / 16.0f;
    }

    for (int f = 0; f < 6; ++f) {
        if (!uv[f].use) continue;
        const UVRect& r = uv[f];
        const glm::vec2 flat[4] = {
            { r.u0 / texW, r.v1 / texH },   // bottom-left
            { r.u1 / texW, r.v1 / texH },   // bottom-right
            { r.u1 / texW, r.v0 / texH },   // top-right
            { r.u0 / texW, r.v0 / texH },   // top-left
        };
        // Turned clockwise as you look at the face (checked in game against Blockbench's 90):
        // each corner takes the picture's corner `turns` steps back round the face
        glm::vec2 tc[4];
        for (int k = 0; k < 4; ++k) tc[k] = flat[(k + 4 - r.turns) % 4];
        const int order[6] = { 0, 1, 2, 0, 2, 3 };
        for (int k : order) {
            ModelVertex v;
            v.position = corners[f][k];
            v.normal = normals[f];
            v.texCoords = tc[k];
            if (rotated) {
                v.position = cubePivot + glm::vec3(rot * glm::vec4(v.position - cubePivot, 1.0f));
                v.normal = glm::vec3(rot * glm::vec4(v.normal, 0.0f));
            }
            out.push_back(v);
        }
    }
}

} // namespace

void AnimatedModel::loadFromJson(const std::string& filepath) {
    m_hierarchyState = 0;   // bones may be added: work the hierarchy check out again
    std::ifstream file(filepath);
    if (!file.is_open()) {
        std::cerr << "[Model Error] Failed to open JSON model file: " << filepath << std::endl;
        return;
    }

    try {
        json j;
        file >> j;

        // Find the geometry and texture size (new and old Bedrock formats)
        json geom;
        float texW = 64.0f, texH = 64.0f;
        if (j.contains("minecraft:geometry") && j["minecraft:geometry"].is_array() && !j["minecraft:geometry"].empty()) {
            geom = j["minecraft:geometry"][0];
            if (geom.contains("description")) {
                texW = geom["description"].value("texture_width", 64.0f);
                texH = geom["description"].value("texture_height", 64.0f);
            }
        } else {
            for (auto it = j.begin(); it != j.end(); ++it) {
                if (it.key().rfind("geometry.", 0) == 0 && it->is_object()) {
                    geom = *it;
                    texW = geom.value("texturewidth", 64.0f);
                    texH = geom.value("textureheight", 64.0f);
                    break;
                }
            }
        }
        if (geom.is_null() || !geom.contains("bones")) {
            std::cerr << "[Model Error] No geometry/bones found in " << filepath << std::endl;
            return;
        }
        if (texW <= 0.0f) texW = 64.0f;
        if (texH <= 0.0f) texH = 64.0f;

        // Pass 1: create every bone
        std::vector<std::pair<std::shared_ptr<Bone>, std::string>> parents;
        for (const auto& bData : geom["bones"]) {
            auto bone = std::make_shared<Bone>();
            bone->name = bData.value("name", "bone_" + std::to_string(boneMap.size()));
            bone->pivot = vec3Of(bData, "pivot", glm::vec3(0.0f)) / 16.0f;
            bone->restRotation = glm::quat_cast(eulerMatrix(bedrockToEngine(vec3Of(bData, "rotation", glm::vec3(0.0f)))));
            bone->bedrock = true;

            if (bData.contains("cubes")) {
                std::vector<ModelVertex> verts;
                const bool boneMirror = bData.value("mirror", false);
                for (const auto& cube : bData["cubes"]) appendCube(cube, texW, texH, boneMirror, verts);
                for (const ModelVertex& v : verts) {
                    bone->boundsMin = bone->hasBounds ? glm::min(bone->boundsMin, v.position) : v.position;
                    bone->boundsMax = bone->hasBounds ? glm::max(bone->boundsMax, v.position) : v.position;
                    bone->hasBounds = true;
                }
                if (!verts.empty()) {
                    bone->meshes.push_back(std::make_shared<Mesh>(verts));
                    bone->mesh = bone->meshes.front();
                }
            }

            boneMap[bone->name] = bone;
            parents.emplace_back(bone, bData.value("parent", std::string()));
        }

        // Each bone's parent (the bone that "parent" names; none for a root or a bad name)
        std::unordered_map<const Bone*, Bone*> parentOf;
        for (auto& [bone, parentName] : parents) {
            auto it = boneMap.find(parentName);
            if (!parentName.empty() && it != boneMap.end() && it->second != bone) parentOf[bone.get()] = it->second.get();
        }
        // Before any bone owns another: refuse a loop in the "parent" names (a is b's parent and b
        // is a's). Linked, it would be a ring of bones owning each other - never freed, and
        // endless to draw. Following parents from a bone must end within the number of bones.
        for (auto& [bone, parentName] : parents) {
            const Bone* b = bone.get();
            for (size_t steps = 0; b && steps <= parents.size(); ++steps) {
                const auto p = parentOf.find(b);
                b = p == parentOf.end() ? nullptr : p->second;
            }
            if (b) {                                       // still going: it went round a loop
                std::cerr << "[Model Error] " << filepath << ": bone '" << bone->name << "' is in a loop of "
                          << "\"parent\" names (a bone can't be its own ancestor) - model not loaded" << std::endl;
                boneMap.clear();
                rootBones.clear();
                return;                                    // `loaded` stays false
            }
        }

        // Pass 2: link parents (a child may be listed before its parent)
        for (auto& [bone, parentName] : parents) {
            const auto p = parentOf.find(bone.get());
            if (p != parentOf.end()) p->second->children.push_back(bone);
            else rootBones.push_back(bone);
        }

        loaded = true;
        m_hierarchyState = 1;                              // checked above: no loops
        std::cout << "[Model] Loaded JSON model: " << filepath << " (" << boneMap.size() << " bones)" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "[Model Error] Could not read JSON model " << filepath << ": " << e.what() << std::endl;
    }
}

// ===========================================================================
// Blockbench project files (.bbmodel): what Blockbench saves, so a model needs no exporting.
//   Read as data only: the shapes, the bone tree, the pictures and the animations. Cubes and
//   bones are turned into the same Bedrock-style geometry the .json loader makes (the way
//   Blockbench's own Bedrock export does it), so a .bbmodel looks and animates the same as its
//   exported .geo.json. Mesh elements are triangulated. Locators, null objects, particle and
//   sound keyframes are ignored.
//   Pictures embedded in the file must be PNG (checked by its signature), at most 4096 pixels a
//   side and 8 MB: decoded into a texture and nothing else. Pictures not embedded are looked for
//   by file name (main.cpp, like any other model texture).
// ===========================================================================

namespace {

constexpr size_t kMaxEmbeddedBytes = 8u * 1024u * 1024u;   // the decoded PNG
constexpr int kMaxTextureSide = 4096;
constexpr int kMaxOutlinerDepth = 128;

// Standard base64 (as in a data: URL). False on anything else.
bool decodeBase64(const std::string& in, size_t from, std::vector<unsigned char>& out, size_t maxBytes) {
    auto val = [](unsigned char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return -1;
    };
    out.clear();
    if (in.size() > from) out.reserve(std::min(maxBytes, (in.size() - from) / 4 * 3));
    unsigned buf = 0;
    int bits = 0;
    bool ended = false;
    for (size_t i = from; i < in.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(in[i]);
        if (c == '=') { ended = true; continue; }
        if (c == '\r' || c == '\n' || c == ' ' || c == '\t') continue;
        const int v = val(c);
        if (v < 0 || ended) return false;
        buf = (buf << 6) | static_cast<unsigned>(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            if (out.size() >= maxBytes) return false;
            out.push_back(static_cast<unsigned char>((buf >> bits) & 0xFF));
        }
    }
    return true;
}

// A texture from a "data:image/png;base64,..." source, or 0 (with `why` set)
GLuint textureFromDataUrl(const std::string& src, std::string& why) {
    const std::string prefix = "data:image/png;base64,";
    if (src.compare(0, prefix.size(), prefix) != 0) { why = "not a PNG picture"; return 0; }
    std::vector<unsigned char> png;
    if (!decodeBase64(src, prefix.size(), png, kMaxEmbeddedBytes)) {
        why = png.size() >= kMaxEmbeddedBytes ? "bigger than 8 MB" : "its data is damaged";
        return 0;
    }
    static const unsigned char sig[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
    if (png.size() < 8 || std::memcmp(png.data(), sig, 8) != 0) { why = "not a PNG picture"; return 0; }
    int w = 0, h = 0, n = 0;
    if (!stbi_info_from_memory(png.data(), static_cast<int>(png.size()), &w, &h, &n)) { why = "its data is damaged"; return 0; }
    if (w <= 0 || h <= 0 || w > kMaxTextureSide || h > kMaxTextureSide) { why = "bigger than 4096 pixels a side"; return 0; }
    stbi_set_flip_vertically_on_load(false);
    unsigned char* px = stbi_load_from_memory(png.data(), static_cast<int>(png.size()), &w, &h, &n, 4);
    if (!px) { why = "its data is damaged"; return 0; }
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);   // pixel art stays crisp
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    stbi_image_free(px);
    return tex;
}

float num(const json& j, float fallback = 0.0f) {
    if (j.is_number()) return j.get<float>();
    if (j.is_string()) {
        const std::string s = j.get<std::string>();
        char* end = nullptr;
        const float f = std::strtof(s.c_str(), &end);
        if (end && end != s.c_str()) return f;
    }
    return fallback;
}
glm::vec3 bbVec(const json& j, const char* key, glm::vec3 fallback = glm::vec3(0.0f)) {
    if (!j.is_object() || !j.contains(key) || !j[key].is_array() || j[key].size() < 3) return fallback;
    return glm::vec3(num(j[key][0]), num(j[key][1]), num(j[key][2]));
}
json arr3(const glm::vec3& v) { return json::array({ v.x, v.y, v.z }); }

std::string fileNameOf(std::string path) {
    for (char& c : path) if (c == '\\') c = '/';
    const size_t slash = path.find_last_of('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

// Which of the file's textures a face uses (its index). `blank`: the face was made see-through in
// Blockbench (texture null) and isn't drawn. A face with no texture of its own (false, missing,
// or naming none of them) uses the first one.
int faceTexture(const json& face, const std::vector<std::pair<std::string, std::string>>& ids, bool& blank) {
    blank = false;
    if (!face.contains("texture")) return 0;
    const json& t = face["texture"];
    if (t.is_null()) { blank = true; return 0; }
    if (t.is_number_integer()) {
        const int i = t.get<int>();
        return (i >= 0 && i < static_cast<int>(ids.size())) ? i : 0;
    }
    if (t.is_string()) {
        std::string s = t.get<std::string>();
        if (!s.empty() && s[0] == '#') s.erase(0, 1);
        for (size_t i = 0; i < ids.size(); ++i)
            if (!s.empty() && (s == ids[i].first || s == ids[i].second)) return static_cast<int>(i);
    }
    return 0;
}

// A mesh element (free-form polygons): its faces as triangles, in the engine's Bedrock space
void appendPolyMesh(const json& el, float texW, float texH, const std::vector<std::pair<std::string, std::string>>& ids,
                    std::map<int, std::vector<ModelVertex>>& byTex) {
    if (!el.contains("vertices") || !el["vertices"].is_object() || !el.contains("faces") || !el["faces"].is_object()) return;
    const glm::vec3 origin = bbVec(el, "origin");
    const glm::mat4 rot = eulerMatrix(bbVec(el, "rotation"));   // Blockbench's own space here
    std::map<std::string, glm::vec3> verts;
    for (auto it = el["vertices"].begin(); it != el["vertices"].end(); ++it) {
        if (!it->is_array() || it->size() < 3) continue;
        const glm::vec3 local(num((*it)[0]), num((*it)[1]), num((*it)[2]));
        glm::vec3 p = origin + glm::vec3(rot * glm::vec4(local, 1.0f));
        p.x = -p.x;                                                // Blockbench -> Bedrock (mirrored on X)
        verts[it.key()] = p / 16.0f;
    }
    for (auto it = el["faces"].begin(); it != el["faces"].end(); ++it) {
        const json& f = *it;
        if (!f.is_object() || !f.contains("vertices") || !f["vertices"].is_array()) continue;
        std::vector<std::string> keys;
        for (const auto& k : f["vertices"]) if (k.is_string() && verts.count(k.get<std::string>())) keys.push_back(k.get<std::string>());
        if (keys.size() < 3 || keys.size() > 4) continue;
        bool blank = false;
        const int tex = faceTexture(f, ids, blank);
        if (blank) continue;
        auto uvOf = [&](const std::string& k) {
            glm::vec2 uv(0.0f);
            if (f.contains("uv") && f["uv"].is_object() && f["uv"].contains(k) && f["uv"][k].is_array() && f["uv"][k].size() >= 2)
                uv = glm::vec2(num(f["uv"][k][0]) / texW, num(f["uv"][k][1]) / texH);
            return uv;
        };
        // A quad's corners can be listed in any order: use the one that goes round its edge
        if (keys.size() == 4) {
            auto P = [&](int i) { return verts[keys[i]]; };
            auto goesRound = [&](int a, int b, int c, int d) {
                const glm::vec3 n1 = glm::cross(P(b) - P(a), P(c) - P(a));
                const glm::vec3 n2 = glm::cross(P(c) - P(a), P(d) - P(a));
                return glm::dot(n1, n2) > 0.0f;
            };
            if (!goesRound(0, 1, 2, 3)) {
                if (goesRound(0, 1, 3, 2)) std::swap(keys[2], keys[3]);
                else if (goesRound(0, 2, 1, 3)) std::swap(keys[1], keys[2]);
            }
        }
        std::vector<ModelVertex>& out = byTex[tex];
        const int tris = static_cast<int>(keys.size()) - 2;
        for (int t = 0; t < tris; ++t) {
            // (X was mirrored, which turns the winding round: listed backwards to keep it)
            const std::string* k[3] = { &keys[0], &keys[t + 2], &keys[t + 1] };
            const glm::vec3 a = verts[*k[0]], b = verts[*k[1]], c = verts[*k[2]];
            glm::vec3 n = glm::cross(b - a, c - a);
            const float len = glm::length(n);
            n = len > 0.0f ? n / len : glm::vec3(0, 1, 0);
            for (int i = 0; i < 3; ++i) {
                ModelVertex v;
                v.position = verts[*k[i]];
                v.normal = n;
                v.texCoords = uvOf(*k[i]);
                out.push_back(v);
            }
        }
    }
}

struct BBGroup {
    const json* node = nullptr;   // its outliner entry (children: element uuids and groups)
    std::string uuid, name;       // name: unique among the model's bones
    glm::vec3 origin{0.0f}, rotation{0.0f};
    int parent = -1;              // index in the list, -1 for a root
};

void collectGroups(const json& node, int parent, int depth, const std::map<std::string, const json*>& defs,
                   std::set<std::string>& seen, std::set<std::string>& names, std::vector<BBGroup>& out) {
    if (depth > kMaxOutlinerDepth || !node.is_object()) return;
    BBGroup g;
    g.node = &node;
    g.uuid = node.value("uuid", std::string());
    if (!g.uuid.empty() && !seen.insert(g.uuid).second) return;       // listed twice: once is enough
    const auto it = defs.find(g.uuid);
    const json& d = it != defs.end() ? *it->second : node;            // format 5: apart; older: here
    if (!d.value("export", true)) return;
    std::string name = d.value("name", std::string("bone"));
    if (name.empty()) name = "bone";
    g.name = name;
    for (int k = 2; names.count(g.name); ++k) g.name = name + "_" + std::to_string(k);
    names.insert(g.name);
    g.origin = bbVec(d, "origin");
    g.rotation = bbVec(d, "rotation");
    g.parent = parent;
    out.push_back(g);
    const int me = static_cast<int>(out.size()) - 1;
    if (node.contains("children") && node["children"].is_array())
        for (const json& c : node["children"]) if (c.is_object()) collectGroups(c, me, depth + 1, defs, seen, names, out);
}

// Every exported group of a .bbmodel, parents before children, with the bone names the model uses
std::vector<BBGroup> bbGroups(const json& j) {
    std::map<std::string, const json*> defs;
    if (j.contains("groups") && j["groups"].is_array())
        for (const json& g : j["groups"])
            if (g.is_object() && g.contains("uuid") && g["uuid"].is_string()) defs[g["uuid"].get<std::string>()] = &g;
    std::vector<BBGroup> out;
    std::set<std::string> seen, names;
    if (j.contains("outliner") && j["outliner"].is_array())
        for (const json& n : j["outliner"]) collectGroups(n, -1, 0, defs, seen, names, out);
    return out;
}

} // namespace

void AnimatedModel::loadFromBBModel(const std::string& filepath) {
    m_hierarchyState = 0;
    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "[Model Error] Failed to open Blockbench model: " << filepath << std::endl;
        return;
    }
    try {
        json j;
        file >> j;
        if (!j.is_object() || !j.contains("elements") || !j["elements"].is_array()) {
            std::cerr << "[Model Error] " << filepath << " isn't a Blockbench model (no \"elements\")" << std::endl;
            return;
        }
        const json meta = j.value("meta", json::object());
        const std::string format = meta.is_object() ? meta.value("model_format", std::string()) : std::string();
        const bool javaBlock = format == "java_block";
        // Java block models are 0..16 with the block's corner at 0: centred here, so the block's
        // middle is where the model stands (like an entity's feet)
        const glm::vec3 shift = javaBlock ? glm::vec3(-8.0f, 0.0f, -8.0f) : glm::vec3(0.0f);

        float resW = 16.0f, resH = 16.0f;
        if (j.contains("resolution") && j["resolution"].is_object()) {
            resW = num(j["resolution"].value("width", json(16)), 16.0f);
            resH = num(j["resolution"].value("height", json(16)), 16.0f);
        }
        if (!(resW > 0.0f)) resW = 16.0f;
        if (!(resH > 0.0f)) resH = 16.0f;

        // ---- Textures (pictures inside the file are decoded; others are found by name later)
        std::vector<std::pair<std::string, std::string>> texIds;    // (id, uuid) for faces to name them
        std::vector<glm::vec2> texUV;                               // each one's UV size
        if (j.contains("textures") && j["textures"].is_array()) {
            for (const json& t : j["textures"]) {
                if (!t.is_object()) continue;
                ModelTexture mt;
                std::string name = t.value("name", std::string());
                if (name.empty()) name = fileNameOf(t.value("path", std::string()));
                mt.name = fileNameOf(name);
                const std::string src = t.value("source", std::string());
                if (src.rfind("data:", 0) == 0) {
                    std::string why;
                    mt.id = textureFromDataUrl(src, why);
                    mt.owned = mt.id != 0;
                    if (!mt.id) std::cerr << "[Model] " << filepath << ": texture '" << mt.name << "' not used: " << why
                                          << " (looking for a file with its name instead)" << std::endl;
                }
                std::string id = t.contains("id") ? (t["id"].is_string() ? t["id"].get<std::string>() : t["id"].dump()) : std::string();
                texIds.emplace_back(id, t.value("uuid", std::string()));
                glm::vec2 uvSize(resW, resH);
                if (javaBlock && t.contains("uv_width") && t.contains("uv_height")) {
                    const glm::vec2 own(num(t["uv_width"], resW), num(t["uv_height"], resH));
                    if (own.x > 0.0f && own.y > 0.0f) uvSize = own;
                }
                texUV.push_back(uvSize);
                textures.push_back(mt);
            }
        }
        if (!textures.empty()) {
            texture = textures[0].id;                 // the main texture (load_model's can replace it)
            if (!textures[0].id) textureHint = textures[0].name;
        }
        auto uvSizeOf = [&](int tex) { return (tex >= 0 && tex < static_cast<int>(texUV.size())) ? texUV[tex] : glm::vec2(resW, resH); };

        // ---- Elements by uuid
        std::map<std::string, const json*> elements;
        for (const json& e : j["elements"]) {
            if (e.is_object() && e.contains("uuid") && e["uuid"].is_string()) elements[e["uuid"].get<std::string>()] = &e;
        }

        std::set<std::string> placed;            // elements already used (each goes in one bone)
        int skippedLocators = 0, cubes = 0, polyMeshes = 0;

        // Builds a bone's meshes from its elements (one mesh per texture it uses)
        auto addElements = [&](Bone& bone, const std::vector<const json*>& els) {
            std::map<int, std::vector<ModelVertex>> byTex;
            for (const json* ep : els) {
                const json& e = *ep;
                if (!e.value("export", true)) continue;
                const std::string type = e.value("type", std::string("cube"));
                if (type == "mesh") {
                    json moved = e;
                    if (javaBlock) moved["origin"] = arr3(bbVec(e, "origin") + shift);
                    appendPolyMesh(moved, resW, resH, texIds, byTex);
                    ++polyMeshes;
                    continue;
                }
                if (type != "cube") { ++skippedLocators; continue; }
                ++cubes;
                const glm::vec3 from = bbVec(e, "from") + shift, to = bbVec(e, "to") + shift;
                const glm::vec3 origin = bbVec(e, "origin") + shift;
                const glm::vec3 rotation = bbVec(e, "rotation");
                // Blockbench -> Bedrock, as its Bedrock export does it
                json cube;
                cube["origin"] = arr3(glm::vec3(-to.x, from.y, from.z));
                cube["size"] = arr3(to - from);
                if (e.contains("inflate")) cube["inflate"] = num(e["inflate"]);
                if (rotation != glm::vec3(0.0f)) {
                    cube["rotation"] = arr3(glm::vec3(-rotation.x, -rotation.y, rotation.z));
                    cube["pivot"] = arr3(glm::vec3(-origin.x, origin.y, origin.z));
                }
                // Faces, by the texture each uses. Blockbench saves every face's UV (box UV too, worked
                // out from the layout), so they're used as they are: up and down are turned round, as
                // in Bedrock's per-face UV.
                std::map<int, json> perTex;
                if (e.contains("faces") && e["faces"].is_object()) {
                    for (const char* fname : { "north", "east", "south", "west", "up", "down" }) {
                        if (!e["faces"].contains(fname)) continue;
                        const json& f = e["faces"][fname];
                        if (!f.is_object() || !f.contains("uv") || !f["uv"].is_array() || f["uv"].size() < 4) continue;
                        bool blank = false;
                        const int tex = faceTexture(f, texIds, blank);
                        if (blank) continue;
                        float u0 = num(f["uv"][0]), v0 = num(f["uv"][1]), u1 = num(f["uv"][2]), v1 = num(f["uv"][3]);
                        float su = u1 - u0, sv = v1 - v0;
                        const std::string fn = fname;
                        if (fn == "up" || fn == "down") { u0 += su; v0 += sv; su = -su; sv = -sv; }
                        // in this texture's UV size, scaled to the size the cube code divides by
                        const glm::vec2 own = uvSizeOf(tex);
                        const float kx = resW / own.x, ky = resH / own.y;
                        json fj;
                        fj["uv"] = json::array({ u0 * kx, v0 * ky });
                        fj["uv_size"] = json::array({ su * kx, sv * ky });
                        if (f.contains("rotation") && f["rotation"].is_number()) fj["uv_rotation"] = f["rotation"];
                        perTex[tex]["uv"][fname] = fj;
                    }
                }
                for (auto& [tex, faces] : perTex) {
                    json c = cube;
                    c["uv"] = faces["uv"];
                    appendCube(c, resW, resH, false, byTex[tex]);
                }
            }
            for (auto& [tex, verts] : byTex) {
                if (verts.empty()) continue;
                for (const ModelVertex& v : verts) {
                    bone.boundsMin = bone.hasBounds ? glm::min(bone.boundsMin, v.position) : v.position;
                    bone.boundsMax = bone.hasBounds ? glm::max(bone.boundsMax, v.position) : v.position;
                    bone.hasBounds = true;
                }
                auto mesh = std::make_shared<Mesh>(verts);
                mesh->textureIndex = tex;
                bone.meshes.push_back(mesh);
            }
            if (!bone.meshes.empty()) bone.mesh = bone.meshes.front();
        };

        auto makeBone = [&](const std::string& name, const glm::vec3& origin, const glm::vec3& rotation) {
            auto bone = std::make_shared<Bone>();
            bone->name = name;
            const glm::vec3 o = origin + shift;
            bone->pivot = glm::vec3(-o.x, o.y, o.z) / 16.0f;
            bone->restRotation = glm::quat_cast(eulerMatrix(bedrockToEngine(glm::vec3(-rotation.x, -rotation.y, rotation.z))));
            bone->bedrock = true;
            boneMap[bone->name] = bone;
            return bone;
        };

        // The bones: the outliner's groups, each with the elements listed in it
        const std::vector<BBGroup> groups = bbGroups(j);
        std::vector<std::shared_ptr<Bone>> bones;
        for (const BBGroup& g : groups) {
            auto bone = makeBone(g.name, g.origin, g.rotation);
            if (g.parent >= 0) bones[g.parent]->children.push_back(bone);
            else rootBones.push_back(bone);
            bones.push_back(bone);
            std::vector<const json*> els;
            const json& node = *g.node;
            if (node.contains("children") && node["children"].is_array())
                for (const json& c : node["children"]) {
                    if (!c.is_string()) continue;
                    const auto e = elements.find(c.get<std::string>());
                    if (e != elements.end() && placed.insert(e->first).second) els.push_back(e->second);
                }
            addElements(*bone, els);
        }

        std::vector<const json*> loose;          // elements outside any group
        if (j.contains("outliner") && j["outliner"].is_array()) {
            for (const json& n : j["outliner"]) {
                if (!n.is_string()) continue;
                const auto e = elements.find(n.get<std::string>());
                if (e != elements.end() && placed.insert(e->first).second) loose.push_back(e->second);
            }
        } else {
            for (const json& e : j["elements"]) if (e.is_object()) loose.push_back(&e);   // no outliner: all loose
        }
        if (!loose.empty()) {
            // (as Blockbench's export does: loose cubes go in a bone of their own)
            std::string name = "bb_main";
            for (int k = 2; boneMap.count(name); ++k) name = "bb_main_" + std::to_string(k);
            auto bone = makeBone(name, glm::vec3(0.0f) - shift, glm::vec3(0.0f));
            rootBones.push_back(bone);
            addElements(*bone, loose);
        }

        loaded = !rootBones.empty();
        m_hierarchyState = loaded ? 1 : 0;         // built as a tree: no loops
        int embedded = 0;
        for (const ModelTexture& t : textures) embedded += t.owned ? 1 : 0;
        std::cout << "[Model] Loaded Blockbench model: " << filepath << " (" << boneMap.size() << " bones, " << cubes
                  << " cubes" << (polyMeshes ? ", " + std::to_string(polyMeshes) + " meshes" : std::string())
                  << ", " << textures.size() << " texture" << (textures.size() == 1 ? "" : "s")
                  << (textures.empty() ? "" : " (" + std::to_string(embedded) + " embedded)") << ")" << std::endl;
        if (skippedLocators) std::cout << "[Model] " << filepath << ": " << skippedLocators
                                       << " locator/null object element(s) ignored" << std::endl;
        if (!loaded) std::cerr << "[Model Error] " << filepath << " has nothing to draw" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "[Model Error] Could not read Blockbench model " << filepath << ": " << e.what() << std::endl;
    }
}

// ===========================================================================
// B3D (Blitz3D, used by Luanti mods)
//   Chunks: [tag(4)][size(4)][data(size)] - size does NOT include the 8-byte header.
//   B3D is left-handed, so Z is flipped and triangles are reversed to keep faces outward.
//   Static pose only: skinned meshes are shown in their bind pose (BONE/KEYS are skipped).
// ===========================================================================

namespace {

int readInt(std::ifstream& f)     { int v = 0; f.read(reinterpret_cast<char*>(&v), 4); return v; }
float readFloat(std::ifstream& f) { float v = 0; f.read(reinterpret_cast<char*>(&v), 4); return v; }

std::string readString(std::ifstream& f) {
    std::string s;
    char c;
    while (f.get(c) && c != '\0') s += c;
    return s;
}

struct ChunkHeader {
    std::string tag;
    std::streamoff end = 0;
};

// Reads a chunk header; fails if it doesn't fit inside `parentEnd`.
bool readChunk(std::ifstream& f, std::streamoff parentEnd, ChunkHeader& c) {
    const std::streamoff start = f.tellg();
    if (start < 0 || start + 8 > parentEnd) return false;
    char tag[4];
    if (!f.read(tag, 4)) return false;
    const int size = readInt(f);
    if (!f || size < 0) return false;
    c.tag.assign(tag, 4);
    c.end = start + 8 + size;
    return c.end <= parentEnd;
}

void parseMesh(std::ifstream& f, std::streamoff end, const std::shared_ptr<Bone>& bone) {
    readInt(f);   // brush id
    std::vector<ModelVertex> verts;
    std::vector<ModelVertex> tris;
    bool hasNormals = false;

    ChunkHeader c;
    while (f.good() && readChunk(f, end, c)) {
        if (c.tag == "VRTS") {
            const int flags = readInt(f);
            const int sets = readInt(f);
            const int setSize = readInt(f);
            hasNormals = (flags & 1) != 0;
            const std::streamoff stride = 12 + (hasNormals ? 12 : 0) + ((flags & 2) ? 16 : 0)
                                        + static_cast<std::streamoff>(sets) * setSize * 4;
            const std::streamoff count = stride > 0 ? (c.end - f.tellg()) / stride : 0;

            for (std::streamoff i = 0; i < count; ++i) {
                ModelVertex v;
                v.position.x = readFloat(f);
                v.position.y = readFloat(f);
                v.position.z = -readFloat(f);          // left-handed -> right-handed
                if (hasNormals) {
                    v.normal.x = readFloat(f);
                    v.normal.y = readFloat(f);
                    v.normal.z = -readFloat(f);
                }
                if (flags & 2) f.seekg(16, std::ios::cur);   // vertex colour (unused)
                for (int s = 0; s < sets; ++s) {
                    for (int k = 0; k < setSize; ++k) {
                        const float t = readFloat(f);
                        if (s == 0 && k == 0) v.texCoords.x = t;
                        if (s == 0 && k == 1) v.texCoords.y = t;
                    }
                }
                verts.push_back(v);
            }
        } else if (c.tag == "TRIS") {
            readInt(f);   // brush id
            while (f.good() && f.tellg() + std::streamoff(12) <= c.end) {
                const int a = readInt(f), b = readInt(f), d = readInt(f);
                const int n = static_cast<int>(verts.size());
                if (a < 0 || b < 0 || d < 0 || a >= n || b >= n || d >= n) continue;
                tris.push_back(verts[a]);   // reversed winding because Z was flipped
                tris.push_back(verts[d]);
                tris.push_back(verts[b]);
            }
        }
        f.clear();
        f.seekg(c.end, std::ios::beg);
    }

    if (tris.empty()) return;
    if (!hasNormals) computeFlatNormals(tris);
    bone->meshes.push_back(std::make_shared<Mesh>(tris));
    if (!bone->mesh) bone->mesh = bone->meshes.front();
}

void parseNode(std::ifstream& f, std::streamoff end, const std::shared_ptr<Bone>& parent, AnimatedModel& model) {
    auto bone = std::make_shared<Bone>();
    std::string name = readString(f);
    if (name.empty()) name = "bone_" + std::to_string(model.boneMap.size());
    while (model.boneMap.count(name)) name += "_";   // keep names unique

    glm::vec3 pos, scl;
    pos.x = readFloat(f); pos.y = readFloat(f); pos.z = -readFloat(f);
    scl.x = readFloat(f); scl.y = readFloat(f); scl.z = readFloat(f);
    const float rw = readFloat(f), rx = readFloat(f), ry = readFloat(f), rz = readFloat(f);   // w first

    bone->name = name;
    bone->restPosition = pos;   // the node's translation: kept apart from the animation offset
    bone->scale = scl;
    bone->restRotation = glm::normalize(glm::quat(rw, -rx, -ry, rz));   // mirror for the Z flip

    model.boneMap[name] = bone;
    if (parent) parent->children.push_back(bone);
    else model.rootBones.push_back(bone);

    ChunkHeader c;
    while (f.good() && readChunk(f, end, c)) {
        if (c.tag == "MESH") parseMesh(f, c.end, bone);
        else if (c.tag == "NODE") parseNode(f, c.end, bone, model);
        // BONE / KEYS / ANIM (skeletal animation) are skipped for now
        f.clear();
        f.seekg(c.end, std::ios::beg);
    }
}

} // namespace

void AnimatedModel::loadFromB3D(const std::string& filepath) {
    m_hierarchyState = 0;   // bones may be added: work the hierarchy check out again
    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "[Model Error] Failed to open .b3d binary file: " << filepath << std::endl;
        return;
    }

    ChunkHeader top;
    file.seekg(0, std::ios::end);
    const std::streamoff fileEnd = file.tellg();
    file.seekg(0, std::ios::beg);
    if (!readChunk(file, fileEnd, top) || top.tag != "BB3D") {
        std::cerr << "[Model Error] Invalid .b3d file header: " << filepath << std::endl;
        return;
    }
    readInt(file);   // version
    isB3D = true;
    scale = 0.1f;    // Luanti-style meshes: 10 units = 1 block

    ChunkHeader c;
    while (file.good() && readChunk(file, top.end, c)) {
        if (c.tag == "TEXS") {
            // First texture name is used if load_model isn't given one
            if (file.tellg() < c.end) textureHint = readString(file);
        } else if (c.tag == "NODE") {
            parseNode(file, c.end, nullptr, *this);
        }
        file.clear();
        file.seekg(c.end, std::ios::beg);
    }

    loaded = !rootBones.empty();
    int meshCount = 0;
    for (const auto& [n, b] : boneMap) meshCount += static_cast<int>(b->meshes.size());
    std::cout << "[Model] Loaded .b3d model: " << filepath << " (" << boneMap.size() << " nodes, "
              << meshCount << " meshes" << (textureHint.empty() ? "" : ", texture " + textureHint) << ")" << std::endl;
}

bool AnimatedModel::hierarchyIsAcyclic(std::string* cycleBone) const {
    // Depth-first over every bone (boneMap holds all of them, rootBones the tops), without
    // recursion: 0 = not seen, 1 = on the current path, 2 = finished. Meeting a bone that's on
    // the path again means a loop.
    std::unordered_map<const Bone*, uint8_t> state;
    std::vector<const Bone*> starts;
    for (const auto& entry : boneMap) if (entry.second) starts.push_back(entry.second.get());
    for (const auto& b : rootBones) if (b) starts.push_back(b.get());

    struct Frame { const Bone* bone; size_t next; };
    std::vector<Frame> path;
    for (const Bone* start : starts) {
        if (state[start] != 0) continue;
        state[start] = 1;
        path.push_back({ start, 0 });
        while (!path.empty()) {
            Frame& top = path.back();
            if (top.next < top.bone->children.size()) {
                const Bone* child = top.bone->children[top.next++].get();
                if (!child) continue;
                uint8_t& s = state[child];
                if (s == 1) {                                 // back to a bone on this path
                    if (cycleBone) *cycleBone = child->name;
                    return false;
                }
                if (s == 0) { s = 1; path.push_back({ child, 0 }); }   // (`top` not used after this)
            } else {
                state[top.bone] = 2;
                path.pop_back();
            }
        }
    }
    return true;
}

// The whole-hierarchy check, done once (not on every arm draw) and redone after a load
bool AnimatedModel::hierarchyOk() const {
    if (m_hierarchyState == 0) m_hierarchyState = hierarchyIsAcyclic(&m_cycleBone) ? 1 : 2;
    return m_hierarchyState == 1;
}

// The matrix of `target` below `bone` (whose parent's matrix is `parent`), or false if it isn't there
static bool findBoneMatrix(const std::shared_ptr<Bone>& bone, const Bone* target, const glm::mat4& parent,
                           glm::mat4& out, int depth) {
    if (!bone || depth > 256) return false;
    const glm::mat4 m = parent * bone->getLocalTransform();
    if (bone.get() == target) { out = m; return true; }
    for (const auto& child : bone->children)
        if (findBoneMatrix(child, target, m, out, depth + 1)) return true;
    return false;
}

bool AnimatedModel::boneMatrix(const std::string& name, const glm::mat4& world, glm::mat4& out) const {
    if (!loaded || !hierarchyOk()) return false;
    const auto it = boneMap.find(name);
    if (it == boneMap.end()) return false;
    // The same root as draw(): the model's scale, and Bedrock models' mirror on X
    glm::mat4 root = glm::scale(world, glm::vec3(scale));
    if (!isB3D) root = glm::scale(root, glm::vec3(-1.0f, 1.0f, 1.0f));
    for (const auto& r : rootBones)
        if (findBoneMatrix(r, it->second.get(), root, out, 0)) return true;
    return false;
}

bool AnimatedModel::drawBone(GLuint shaderProgram, const std::string& name, const glm::mat4& world) {
    if (!loaded) return false;
    auto bone = getBone(name);
    if (!bone) return false;
    // A bone that is its own ancestor would make the pose pass and the drawing below go round
    // forever: check the whole hierarchy before touching anything, and refuse a looped one
    if (!hierarchyOk()) {
        if (!m_reportedCycle) {
            std::cerr << "[Model] Bone '" << m_cycleBone << "' is its own ancestor (a loop in the bones' parents): "
                      << "not drawing bone '" << name << "'" << std::endl;
            m_reportedCycle = true;
        }
        return false;
    }
    // The file's pose: animation offsets, rotations and scales are cleared for the subtree (rest
    // positions stay) and put back afterwards
    struct Saved { Bone* b; glm::vec3 r, p, s; };
    std::vector<Saved> saved;
    std::vector<Bone*> stack{ bone.get() };
    while (!stack.empty()) {
        Bone* b = stack.back();
        stack.pop_back();
        saved.push_back({ b, b->rotation, b->position, b->animScale });
        b->rotation = glm::vec3(0.0f);
        b->position = glm::vec3(0.0f);
        b->animScale = glm::vec3(1.0f);
        for (auto& c : b->children) stack.push_back(c.get());
    }

    glUseProgram(shaderProgram);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture != 0 ? texture : whiteTexture());
    const GLint texLoc = glGetUniformLocation(shaderProgram, "texture_diffuse1");
    if (texLoc >= 0) glUniform1i(texLoc, 0);
    const GLint modelLoc = glGetUniformLocation(shaderProgram, "model");
    // Recentre only the selected bone: cancel its own rest position and pivot so its pivot point
    // (the shoulder, for an arm) lands at the origin of `world`. Bones under it keep their rest
    // positions, so the subtree keeps its shape.
    const glm::mat4 root = glm::translate(glm::scale(world, glm::vec3(scale)), -(bone->pivot + bone->restPosition));
    GLuint bound = texture != 0 ? texture : whiteTexture();
    drawBoneRecursive(*this, bone, modelLoc, root, bound);

    for (const Saved& sv : saved) { sv.b->rotation = sv.r; sv.b->position = sv.p; sv.b->animScale = sv.s; }
    return true;
}

// ===========================================================================
// Animations
// ===========================================================================

// (rest positions from the file stay: only what animations and Lua add is cleared)
void AnimatedModel::resetPose() {
    for (auto& [name, bone] : boneMap) {
        bone->rotation = glm::vec3(0.0f);
        bone->position = glm::vec3(0.0f);
        bone->animScale = glm::vec3(1.0f);
    }
}

namespace {

// A number, or a number written as text. Molang expressions ("math.sin(...)") aren't
// supported: they count as 0 and are reported once per file.
float animNumber(const json& v, bool& unsupported) {
    if (v.is_number()) return v.get<float>();
    if (v.is_string()) {
        const std::string s = v.get<std::string>();
        char* end = nullptr;
        const float f = std::strtof(s.c_str(), &end);
        if (end && end != s.c_str() && *end == '\0') return f;
        unsupported = true;
    }
    return 0.0f;
}

glm::vec3 animVec(const json& v, bool& unsupported) {
    if (v.is_array() && v.size() >= 3) return glm::vec3(animNumber(v[0], unsupported), animNumber(v[1], unsupported), animNumber(v[2], unsupported));
    if (v.is_array() && v.size() == 1) { const float f = animNumber(v[0], unsupported); return glm::vec3(f); }
    if (v.is_number() || v.is_string()) { const float f = animNumber(v, unsupported); return glm::vec3(f); }
    return glm::vec3(0.0f);
}

// "rotation": [x, y, z]   or   { "0.0": [x, y, z], "0.5": { "pre": [...], "post": [...], "lerp_mode": "catmullrom" } }
AnimChannel readChannel(const json& j, bool& unsupported) {
    AnimChannel ch;
    if (j.is_object() && !j.contains("pre") && !j.contains("post")) {
        for (auto it = j.begin(); it != j.end(); ++it) {
            AnimKey k;
            k.time = std::strtof(it.key().c_str(), nullptr);
            const json& v = it.value();
            if (v.is_object()) {
                k.post = animVec(v.contains("post") ? v["post"] : v.value("pre", json::array()), unsupported);
                k.pre = v.contains("pre") ? animVec(v["pre"], unsupported) : k.post;
                const std::string mode = v.value("lerp_mode", std::string("linear"));
                k.lerp = mode == "catmullrom" ? 1 : mode == "step" ? 2 : 0;
            } else {
                k.pre = k.post = animVec(v, unsupported);
            }
            ch.keys.push_back(k);
        }
        std::sort(ch.keys.begin(), ch.keys.end(), [](const AnimKey& a, const AnimKey& b) { return a.time < b.time; });
    } else {
        AnimKey k;
        k.pre = k.post = animVec(j.is_object() ? j.value("post", j.value("pre", json::array())) : j, unsupported);
        ch.keys.push_back(k);
    }
    return ch;
}

glm::vec3 catmullRom(const glm::vec3& p0, const glm::vec3& p1, const glm::vec3& p2, const glm::vec3& p3, float t) {
    const float t2 = t * t, t3 = t2 * t;
    return 0.5f * ((2.0f * p1) + (-p0 + p2) * t + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2 + (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3);
}

// Between keys a and b on Blockbench's bezier curves, each axis on its own: the curve through
// (time, value) from a, pulled by a's right handle and b's left one (their times kept between
// the keys), read at time t.
glm::vec3 bezierBetween(const AnimKey& a, const AnimKey& b, float t) {
    glm::vec3 out;
    for (int i = 0; i < 3; ++i) {
        const float t0 = a.time, t3 = b.time;
        const float t1 = std::clamp(a.time + a.rightTime[i], t0, t3);
        const float t2 = std::clamp(b.time + b.leftTime[i], t0, t3);
        const float v0 = a.post[i], v1 = a.post[i] + a.rightValue[i];
        const float v2 = b.pre[i] + b.leftValue[i], v3 = b.pre[i];
        auto cubic = [](float p0, float p1, float p2, float p3, float s) {
            const float r = 1.0f - s;
            return r * r * r * p0 + 3.0f * r * r * s * p1 + 3.0f * r * s * s * p2 + s * s * s * p3;
        };
        // time along the curve only grows (the handles stay between the keys): find s for t
        float lo = 0.0f, hi = 1.0f;
        for (int k = 0; k < 30; ++k) {
            const float mid = 0.5f * (lo + hi);
            if (cubic(t0, t1, t2, t3, mid) < t) lo = mid; else hi = mid;
        }
        out[i] = cubic(v0, v1, v2, v3, 0.5f * (lo + hi));
    }
    return out;
}

// Animations saved in a .bbmodel: { "animations": [ { name, loop: "once"/"loop"/"hold", length,
// animators: { <group uuid>: { name, type: "bone", keyframes: [ { channel, time, interpolation,
// data_points: [ {x, y, z} (, {x, y, z} after the key) ], bezier_... } ] } } } ] }.
// Keyframe values are what Blockbench shows, the same numbers its Bedrock export writes.
glm::vec3 dataPoint(const json& p, bool& unsupported) {
    if (!p.is_object()) return glm::vec3(0.0f);
    auto axis = [&](const char* k) { return p.contains(k) ? animNumber(p[k], unsupported) : 0.0f; };
    return glm::vec3(axis("x"), axis("y"), axis("z"));
}

bool loadBBAnimations(const json& root, const std::string& path, std::vector<std::shared_ptr<ModelAnimation>>& out) {
    if (!root.contains("animations") || !root["animations"].is_array()) {
        std::cerr << "[Animation] " << path << " has no animations" << std::endl;
        return false;
    }
    std::map<std::string, std::string> boneOf;           // group uuid -> the model's bone name
    for (const BBGroup& g : bbGroups(root)) if (!g.uuid.empty()) boneOf[g.uuid] = g.name;
    bool unsupported = false;
    int effects = 0;
    for (const json& a : root["animations"]) {
        if (!a.is_object()) continue;
        auto anim = std::make_shared<ModelAnimation>();
        anim->name = a.value("name", std::string("animation"));
        const std::string loop = a.contains("loop") && a["loop"].is_string() ? a["loop"].get<std::string>() : "once";
        anim->loop = loop == "loop";                     // "hold" and "once" stop at the end
        float last = 0.0f;
        if (a.contains("animators") && a["animators"].is_object()) {
            for (auto it = a["animators"].begin(); it != a["animators"].end(); ++it) {
                const json& an = it.value();
                if (!an.is_object()) continue;
                const std::string type = an.value("type", std::string("bone"));
                if (type != "bone") { if (an.contains("keyframes")) ++effects; continue; }
                BoneAnimation ba;
                const auto b = boneOf.find(it.key());
                ba.bone = b != boneOf.end() ? b->second : an.value("name", std::string());
                if (ba.bone.empty() || !an.contains("keyframes") || !an["keyframes"].is_array()) continue;
                for (const json& kf : an["keyframes"]) {
                    if (!kf.is_object() || !kf.contains("data_points") || !kf["data_points"].is_array() || kf["data_points"].empty()) continue;
                    const std::string channel = kf.value("channel", std::string());
                    AnimChannel* ch = channel == "rotation" ? &ba.rotation : channel == "position" ? &ba.position
                                    : channel == "scale" ? &ba.scale : nullptr;
                    if (!ch) continue;
                    AnimKey k;
                    k.time = kf.contains("time") ? num(kf["time"]) : 0.0f;
                    const json& dp = kf["data_points"];
                    k.pre = dataPoint(dp[0], unsupported);
                    k.post = dp.size() > 1 ? dataPoint(dp[1], unsupported) : k.pre;
                    const std::string mode = kf.value("interpolation", std::string("linear"));
                    k.lerp = mode == "catmullrom" ? 1 : mode == "step" ? 2 : mode == "bezier" ? 3 : 0;
                    k.leftTime = bbVec(kf, "bezier_left_time", k.leftTime);
                    k.leftValue = bbVec(kf, "bezier_left_value", k.leftValue);
                    k.rightTime = bbVec(kf, "bezier_right_time", k.rightTime);
                    k.rightValue = bbVec(kf, "bezier_right_value", k.rightValue);
                    ch->keys.push_back(k);
                }
                for (AnimChannel* c : { &ba.rotation, &ba.position, &ba.scale }) {
                    std::stable_sort(c->keys.begin(), c->keys.end(), [](const AnimKey& x, const AnimKey& y) { return x.time < y.time; });
                    if (!c->keys.empty()) last = std::max(last, c->keys.back().time);
                }
                if (!ba.rotation.empty() || !ba.position.empty() || !ba.scale.empty()) anim->bones.push_back(std::move(ba));
            }
        }
        const float len = a.contains("length") ? num(a["length"], 0.0f) : 0.0f;
        anim->length = len > 0.0f ? len : last;
        out.push_back(anim);
    }
    if (unsupported) {
        std::cout << "[Animation] " << path << " uses Molang expressions; those values count as 0 (use plain numbers)" << std::endl;
    }
    if (effects) std::cout << "[Animation] " << path << ": sound/particle/instruction keyframes are ignored" << std::endl;
    std::cout << "[Animation] Loaded " << out.size() << " animation(s) from " << path << std::endl;
    return !out.empty();
}

} // namespace

glm::vec3 AnimChannel::sample(float t) const {
    if (keys.empty()) return glm::vec3(0.0f);
    // "pre" is the value before a key, "post" from it on: before the first key its pre, at or after
    // the last key its post (a single key too). Constant channels have pre == post anyway.
    if (t < keys.front().time) return keys.front().pre;
    if (t >= keys.back().time) return keys.back().post;
    size_t i = 0;
    while (i + 1 < keys.size() && keys[i + 1].time <= t) ++i;
    const AnimKey& a = keys[i];
    const AnimKey& b = keys[i + 1];
    const float span = b.time - a.time;
    const float f = span > 0.0f ? (t - a.time) / span : 1.0f;
    if (a.lerp == 2) return a.post;                                  // step: hold until the next key
    if (a.lerp == 3 || b.lerp == 3) return bezierBetween(a, b, t);   // Blockbench's curves
    if (a.lerp == 1 || b.lerp == 1) {                               // smooth curve through the neighbours
        const glm::vec3 p0 = i > 0 ? keys[i - 1].post : a.post;
        const glm::vec3 p3 = i + 2 < keys.size() ? keys[i + 2].pre : b.pre;
        return catmullRom(p0, a.post, b.pre, p3, f);
    }
    return glm::mix(a.post, b.pre, f);
}

void ModelAnimation::apply(AnimatedModel& model, float time, float weight) const {
    if (length > 0.0f) {
        if (loop) { time = std::fmod(time, length); if (time < 0.0f) time += length; }
        else time = std::clamp(time, 0.0f, length);
    }
    weight = std::clamp(weight, 0.0f, 1.0f);
    for (const BoneAnimation& ba : bones) {
        auto bone = model.getBone(ba.bone);
        if (!bone) continue;
        if (!ba.rotation.empty()) bone->rotation = glm::mix(bone->rotation, ba.rotation.sample(time), weight);
        // Positions are in pixels (16 per block), like the model's cubes
        if (!ba.position.empty()) bone->position = glm::mix(bone->position, ba.position.sample(time) / 16.0f, weight);
        if (!ba.scale.empty()) bone->animScale = glm::mix(bone->animScale, ba.scale.sample(time), weight);
    }
}

bool AnimationSet::loadFromFile(const std::string& path) {
    std::ifstream in(path);
    if (!in) {
        std::cerr << "[Animation] Can't open " << path << std::endl;
        return false;
    }
    json root;
    try {
        in >> root;
    } catch (const std::exception& e) {
        std::cerr << "[Animation] " << path << " isn't valid JSON: " << e.what() << std::endl;
        return false;
    }
    // A Blockbench project (.bbmodel) keeps its animations as a list
    if (root.is_object() && root.contains("elements") && root.contains("meta")) return loadBBAnimations(root, path, animations);
    if (!root.contains("animations") || !root["animations"].is_object()) {
        std::cerr << "[Animation] " << path << " has no \"animations\" section" << std::endl;
        return false;
    }
    bool unsupported = false;
    for (auto it = root["animations"].begin(); it != root["animations"].end(); ++it) {
        const json& a = it.value();
        auto anim = std::make_shared<ModelAnimation>();
        anim->name = it.key();
        if (a.contains("loop")) anim->loop = a["loop"].is_boolean() ? a["loop"].get<bool>() : false;   // "hold_on_last_frame" = hold
        float last = 0.0f;
        if (a.contains("bones") && a["bones"].is_object()) {
            for (auto b = a["bones"].begin(); b != a["bones"].end(); ++b) {
                BoneAnimation ba;
                ba.bone = b.key();
                const json& bj = b.value();
                if (bj.contains("rotation")) ba.rotation = readChannel(bj["rotation"], unsupported);
                if (bj.contains("position")) ba.position = readChannel(bj["position"], unsupported);
                if (bj.contains("scale")) ba.scale = readChannel(bj["scale"], unsupported);
                for (const AnimChannel* c : { &ba.rotation, &ba.position, &ba.scale }) {
                    if (!c->keys.empty()) last = std::max(last, c->keys.back().time);
                }
                anim->bones.push_back(std::move(ba));
            }
        }
        anim->length = a.contains("animation_length") && a["animation_length"].is_number()
                     ? a["animation_length"].get<float>() : last;
        animations.push_back(anim);
    }
    if (unsupported) {
        std::cout << "[Animation] " << path << " uses Molang expressions; those values count as 0 (use plain numbers)" << std::endl;
    }
    std::cout << "[Animation] Loaded " << animations.size() << " animation(s) from " << path << std::endl;
    return !animations.empty();
}

std::shared_ptr<ModelAnimation> AnimationSet::get(const std::string& name) const {
    if (name.empty()) return animations.empty() ? nullptr : animations.front();
    for (const auto& a : animations) if (a->name == name) return a;
    // Short names work too: "walk" finds "animation.player.walk"
    for (const auto& a : animations) {
        const std::string& n = a->name;
        if (n.size() > name.size() && n.compare(n.size() - name.size(), name.size(), name) == 0 && n[n.size() - name.size() - 1] == '.') return a;
    }
    return nullptr;
}

std::vector<std::string> AnimationSet::names() const {
    std::vector<std::string> out;
    for (const auto& a : animations) out.push_back(a->name);
    return out;
}
