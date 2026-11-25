#pragma once
// Mirror reflections (blocks with mirror = true, MirrorShape.h).
//
// Each frame the nearest mirror surface in view is picked (mirrors side by side on one wall,
// facing the same way, are one surface). The scene is drawn a second time from the camera's
// reflection in it, into a picture of its own (only the part of the screen the mirror covers),
// with everything on the far side of the glass cut away (a clip plane: GL_CLIP_DISTANCE0). Then that picture is laid on the
// mirror's glass: where the block's picture (the frame) is see-through.
//
// Other mirrors in view show plain silvered glass that frame (the shader's fallback). Nothing is
// drawn twice when no mirror is near and in view.

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <utility>
#include <vector>

class Mirrors {
public:
    struct Cell {
        glm::ivec3 pos;
        uint8_t meta = 0;
        int layer = -1;        // the frame's picture (texture array layer; -1 = none: all glass)
        int joined = 0;        // sides other mirrors carry on from (MirrorShape.h joinedSides)
        glm::vec3 tint{ 1.0f };                         // register_block's mirror = { tint, clarity, glass }
        float clarity = 1.0f;
        glm::vec3 glass{ 0.62f, 0.67f, 0.72f };
    };

    // How far away a mirror still reflects (blocks)
    static constexpr float kRange = 40.0f;

    // Picks this frame's mirror from the ones near the camera. False: none to draw.
    bool choose(const std::vector<Cell>& cells, const glm::mat4& viewProj, const glm::vec3& eye, int fbw, int fbh);

    const glm::vec4& plane() const { return m_plane; }
    // The reflection across the mirror (world space)
    glm::mat4 reflection() const;
    glm::vec3 reflect(const glm::vec3& p) const;
    // The plane that cuts away everything behind the glass while the reflection is drawn: for
    // gl_ClipDistance[0] = dot(vec4(worldPos, 1), plane) in the world and model shaders
    glm::vec4 clipPlane() const;

    // Draw the reflection between these two (into the mirror's own picture)
    void begin(const glm::vec3& clearColor);
    void end();

    // Lays the reflection on the chosen mirror's glass (after the solid blocks are drawn)
    void composite(const glm::mat4& viewProj, GLuint textureArray);

    void release();

private:
    bool ensureTarget(int w, int h);
    bool ensureProgram();

    std::vector<Cell> m_chosen;
    std::pair<int, int> m_lastKey{ 0, 0 };   // the surface live last frame (its facing, where its glass is)
    bool m_hasLast = false;
    glm::vec4 m_plane{ 0.0f };
    int m_w = 0, m_h = 0;
    int m_rect[4] = { 0, 0, 0, 0 };      // x, y, w, h on the screen
    GLuint m_fbo = 0, m_color = 0, m_depth = 0;
    GLuint m_program = 0, m_vao = 0, m_vbo = 0;
    bool m_failed = false;
    GLint m_prevFbo = 0;
    GLint m_prevViewport[4] = { 0, 0, 0, 0 };
};

extern Mirrors g_Mirrors;
