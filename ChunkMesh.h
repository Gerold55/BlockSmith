#pragma once
// Client-side drawable mesh for one chunk, built from its ChunkData.
// Holds no block data itself - the world (ChunkData) is the single source of truth.

#include <glad/glad.h>
#include <vector>
#include "Types.h"

class ChunkData;

class ChunkMesh {
public:
    ChunkMesh() = default;
    ~ChunkMesh();
    ChunkMesh(const ChunkMesh&) = delete;
    ChunkMesh& operator=(const ChunkMesh&) = delete;

    // Builds vertex lists on the CPU (needs the chunk's neighbours linked for border faces).
    void generateMeshData(const ChunkData& data);
    // Sends the last built vertex lists to the GPU. Needs the GL context.
    void uploadMesh();

    // Smooth lighting and ambient occlusion (soft shadows in corners). Changing these needs
    // every chunk rebuilt (World::markAllDirty).
    static bool smoothLighting;
    static bool ambientOcclusion;

    void drawOpaque() const;
    void drawTransparent() const;
    // See-through blocks that tint the view (stained glass, tintView): their faces again, for
    // the colour-filter pass (WorldRenderer multiplies what's behind them by their colour)
    void drawTint() const;

    // One whole block on its own, as a cube from (0,0,0) to (1,1,1) in this mesh's vertex
    // format, textured like the same block in the world, with an even light (0..1: sky, then
    // red, green, blue block light). For blocks drawn outside chunks, e.g. falling sand.
    static void buildSingleBlock(uint8_t id, const float light[4], std::vector<Vertex>& out);
    // Puts vertices in a VAO/VBO with this mesh's attribute layout (made the first time)
    static void uploadVertices(GLuint& vao, GLuint& vbo, const std::vector<Vertex>& verts);

    GLuint opaqueVAO = 0, opaqueVBO = 0;
    GLsizei opaqueVertexCount = 0;
    GLuint transparentVAO = 0, transparentVBO = 0;
    GLsizei transparentVertexCount = 0;
    GLuint tintVAO = 0, tintVBO = 0;
    GLsizei tintVertexCount = 0;

    bool meshReadyForUpload = false;   // built on the CPU, not yet on the GPU
    bool everUploaded = false;         // has reached the GPU at least once

private:
    std::vector<Vertex> cpuOpaqueVertices;
    std::vector<Vertex> cpuTransparentVertices;
    std::vector<Vertex> cpuTintVertices;
};
