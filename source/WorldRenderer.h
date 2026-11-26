#pragma once
// Client-side drawing of a World: keeps one ChunkMesh per loaded chunk, rebuilds meshes
// when the world reports a chunk changed, uploads them, fades new chunks in and draws
// what's inside the view. The World itself knows nothing about this.

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <chrono>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "World.h"

class ChunkMesh;

class WorldRenderer {
public:
    using ChunkCoord = World::ChunkCoord;

    WorldRenderer();
    ~WorldRenderer();

    // Call once per frame after world.update(): picks up changes, rebuilds and uploads meshes.
    void update(World& world, const glm::vec3& playerPos);

    // Draws all visible chunk meshes (opaque then transparent).
    void render(GLuint shaderProgram, const glm::mat4& viewProj, const glm::vec3& eye);
    // The same in two halves, so everything else in the world (mobs, items, players,
    // particles...) can be drawn between them: solid blocks and plants first, then - after
    // everything else - water, glass and other see-through blocks, so what's under the water
    // is seen through it instead of drawn over it. renderTransparent uses the chunks
    // renderOpaque found visible this frame.
    void renderOpaque(GLuint shaderProgram, const glm::mat4& viewProj, const glm::vec3& eye);
    void renderTransparent(GLuint shaderProgram);

    // Fraction (0..1) of chunks within `radius` of pos that are loaded, meshed and on the GPU.
    float areaReadyProgress(const World& world, const glm::vec3& pos, int radius) const;

    // Time per frame spent building meshes / uploading them, in milliseconds.
    // Chunks meshed (their blocks changed, or they loaded) since the last call (VoxelVolume)
    std::vector<ChunkCoord> takeMeshedChunks() { std::vector<ChunkCoord> out; out.swap(m_meshed); return out; }
    void setMeshBudgetMs(float ms) { m_meshBudgetMs = ms; }
    void setUploadBudgetMs(float ms) { m_uploadBudgetMs = ms; }

    // Throws away every mesh (e.g. after textures change size or when leaving a world).
    // forceRebuild = true: on the next update, every chunk still loaded is meshed again (the
    // world won't report them as changed by itself). Plain clear() keeps working as before.
    void clear(bool forceRebuild = false);

    // Mirrors (MirrorShape.h) in the meshes within `radius` blocks of pos: x, y, z in the world
    // and the block's metadata
    void collectMirrors(const glm::vec3& pos, float radius, std::vector<glm::ivec4>& out) const;

    size_t meshCount() const { return m_meshes.size(); }

private:
    std::vector<ChunkCoord> m_meshed;
    using Clock = std::chrono::steady_clock;

    struct Visible {
        ChunkMesh* mesh;
        glm::vec3 origin;
        float dist2;
        float fade;
    };

    void rebuildMeshes(const World& world, const glm::vec3& playerPos);
    void uploadMeshes();
    float chunkFade(const ChunkCoord& c) const;
    void drawList(GLuint shaderProgram, std::vector<Visible>& list) const;
    void collectVisible(const glm::mat4& viewProj, const glm::vec3& eye, std::vector<Visible>& list);
    void drawOpaque(GLuint shaderProgram, std::vector<Visible>& list) const;
    void drawTransparent(GLuint shaderProgram, const std::vector<Visible>& list) const;
    // Stained glass seen through: what's already drawn behind it is multiplied by its colour
    void drawTint(GLuint shaderProgram, const std::vector<Visible>& list) const;
    std::vector<Visible> m_visible;     // this frame's chunks (renderOpaque -> renderTransparent)

    std::unordered_map<ChunkCoord, std::unique_ptr<ChunkMesh>, PairHash> m_meshes;
    std::unordered_set<ChunkCoord, PairHash> m_needsMesh;     // waiting to be (re)built
    std::unordered_set<ChunkCoord, PairHash> m_needsUpload;   // built, waiting for the GPU
    std::unordered_map<ChunkCoord, Clock::time_point, PairHash> m_firstUpload;

    unsigned m_seenResetCount = 0;
    bool m_forceRebuild = false;   // set by clear(true): re-mesh every loaded chunk on the next update
    float m_meshBudgetMs = 4.0f;
    float m_uploadBudgetMs = 3.0f;
};
