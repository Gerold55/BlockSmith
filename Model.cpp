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

using json = nlohmann::json;

namespace {

// Euler angles in degrees, applied X first, then Y, then Z.
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
    m *= eulerMatrix(rotation);
    m = glm::scale(m, scale * animScale);
    m = glm::translate(m, -pivot);
    return m;
}

// ===========================================================================
// AnimatedModel
// ===========================================================================

void AnimatedModel::loadFromFile(const std::string& filepath) {
    const std::string ext = filepath.size() >= 4 ? filepath.substr(filepath.size() - 4) : "";
    if (ext == ".b3d" || ext == ".B3D") {
        loadFromB3D(filepath);
    } else {
        loadFromJson(filepath);
    }
}

std::shared_ptr<Bone> AnimatedModel::getBone(const std::string& name) {
    auto it = boneMap.find(name);
    return it != boneMap.end() ? it->second : nullptr;
}

static void drawBoneRecursive(const std::shared_ptr<Bone>& bone, GLint modelLoc, const glm::mat4& parentMatrix) {
    const glm::mat4 globalMatrix = parentMatrix * bone->getLocalTransform();

    if (!bone->meshes.empty()) {
        glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(globalMatrix));
        for (const auto& m : bone->meshes) m->draw();
    }
    for (const auto& child : bone->children) {
        drawBoneRecursive(child, modelLoc, globalMatrix);
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
    const glm::mat4 root = glm::scale(worldTransform, glm::vec3(scale));
    for (const auto& rootBone : rootBones) {
        drawBoneRecursive(rootBone, modelLoc, root);
    }
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

struct UVRect { float u0, v0, u1, v1; bool use = true; };   // pixels; (u0,v0) = top-left

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
        rot = eulerMatrix(vec3Of(cube, "rotation", glm::vec3(0.0f)));
        cubePivot = vec3Of(cube, "pivot", origin + size * 0.5f) / 16.0f;
    }

    for (int f = 0; f < 6; ++f) {
        if (!uv[f].use) continue;
        const UVRect& r = uv[f];
        const glm::vec2 tc[4] = {
            { r.u0 / texW, r.v1 / texH },   // bottom-left
            { r.u1 / texW, r.v1 / texH },   // bottom-right
            { r.u1 / texW, r.v0 / texH },   // top-right
            { r.u0 / texW, r.v0 / texH },   // top-left
        };
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
            bone->restRotation = glm::quat_cast(eulerMatrix(vec3Of(bData, "rotation", glm::vec3(0.0f))));

            if (bData.contains("cubes")) {
                std::vector<ModelVertex> verts;
                const bool boneMirror = bData.value("mirror", false);
                for (const auto& cube : bData["cubes"]) appendCube(cube, texW, texH, boneMirror, verts);
                if (!verts.empty()) {
                    bone->meshes.push_back(std::make_shared<Mesh>(verts));
                    bone->mesh = bone->meshes.front();
                }
            }

            boneMap[bone->name] = bone;
            parents.emplace_back(bone, bData.value("parent", std::string()));
        }

        // Pass 2: link parents (a child may be listed before its parent)
        for (auto& [bone, parentName] : parents) {
            auto it = boneMap.find(parentName);
            if (!parentName.empty() && it != boneMap.end() && it->second != bone) {
                it->second->children.push_back(bone);
            } else {
                rootBones.push_back(bone);
            }
        }

        loaded = true;
        std::cout << "[Model] Loaded JSON model: " << filepath << " (" << boneMap.size() << " bones)" << std::endl;
        {
            std::string where;
            if (!hierarchyIsAcyclic(&where)) {
                std::cerr << "[Model] " << filepath << ": bone '" << where << "' is its own ancestor (check the "
                          << "\"parent\" names). Those bones won't be drawn on their own (drawBone)." << std::endl;
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "[Model Error] Could not read JSON model " << filepath << ": " << e.what() << std::endl;
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

bool AnimatedModel::drawBone(GLuint shaderProgram, const std::string& name, const glm::mat4& world) {
    if (!loaded) return false;
    auto bone = getBone(name);
    if (!bone) return false;
    // A bone that is its own ancestor would make the pose pass and the drawing below go round
    // forever: check the whole hierarchy before touching anything, and refuse a looped one
    {
        std::string where;
        if (!hierarchyIsAcyclic(&where)) {
            if (!m_reportedCycle) {
                std::cerr << "[Model] Bone '" << where << "' is its own ancestor (a loop in the bones' parents): "
                          << "not drawing bone '" << name << "'" << std::endl;
                m_reportedCycle = true;
            }
            return false;
        }
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
    drawBoneRecursive(bone, modelLoc, root);

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

} // namespace

glm::vec3 AnimChannel::sample(float t) const {
    if (keys.empty()) return glm::vec3(0.0f);
    if (keys.size() == 1 || t <= keys.front().time) return keys.front().pre;
    if (t >= keys.back().time) return keys.back().post;
    size_t i = 0;
    while (i + 1 < keys.size() && keys[i + 1].time <= t) ++i;
    const AnimKey& a = keys[i];
    const AnimKey& b = keys[i + 1];
    const float span = b.time - a.time;
    const float f = span > 0.0f ? (t - a.time) / span : 1.0f;
    if (a.lerp == 2) return a.post;                                  // step: hold until the next key
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
