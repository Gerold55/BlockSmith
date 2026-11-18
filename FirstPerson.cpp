#include "FirstPerson.h"
#include "Block.h"
#include "Model.h"
#include "Types.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <cmath>
#include <cstddef>

namespace {

constexpr float kPi = 3.14159265f;
constexpr float kSwingSeconds = 0.3f;    // Minecraft: 6 ticks
constexpr float kEquipSeconds = 0.15f;

// The same corners and texture coordinates as chunk faces: 0 +X, 1 -X, 2 top, 3 bottom, 4 +Z, 5 -Z
void cubeFace(int f, float out[4][3]) {
    auto set = [&](int i, float x, float y, float z) { out[i][0] = x; out[i][1] = y; out[i][2] = z; };
    switch (f) {
    case 0: set(0, 1, 0, 1); set(1, 1, 0, 0); set(2, 1, 1, 0); set(3, 1, 1, 1); break;
    case 1: set(0, 0, 0, 0); set(1, 0, 0, 1); set(2, 0, 1, 1); set(3, 0, 1, 0); break;
    case 2: set(0, 0, 1, 1); set(1, 1, 1, 1); set(2, 1, 1, 0); set(3, 0, 1, 0); break;
    case 3: set(0, 0, 0, 0); set(1, 1, 0, 0); set(2, 1, 0, 1); set(3, 0, 0, 1); break;
    case 4: set(0, 0, 0, 1); set(1, 1, 0, 1); set(2, 1, 1, 1); set(3, 0, 1, 1); break;
    default: set(0, 1, 0, 0); set(1, 0, 0, 0); set(2, 0, 1, 0); set(3, 1, 1, 0); break;
    }
}
constexpr float kShade[6] = { 0.8f, 0.8f, 1.0f, 0.5f, 0.7f, 0.7f };
constexpr float kUV[4][2] = { {0, 1}, {1, 1}, {1, 0}, {0, 0} };

Vertex vert(float x, float y, float z, float u, float v, const glm::vec3& tint, float base, float overlay,
            bool tintBase, bool tintOverlay, float shade) {
    Vertex out{x, y, z, tint.r, tint.g, tint.b, u, v, base, overlay, tintBase ? 1.0f : 0.0f, tintOverlay ? 1.0f : 0.0f, shade};
    return out;
}

void pushQuad(std::vector<Vertex>& out, const Vertex (&q)[4]) {
    out.push_back(q[0]); out.push_back(q[1]); out.push_back(q[2]);
    out.push_back(q[0]); out.push_back(q[2]); out.push_back(q[3]);
}

// A box from `lo` to `hi`, all faces one colour (no texture) - the plain arm
void plainBox(std::vector<Vertex>& out, const glm::vec3& lo, const glm::vec3& hi, const glm::vec3& color) {
    for (int f = 0; f < 6; ++f) {
        float c[4][3];
        cubeFace(f, c);
        Vertex q[4];
        for (int i = 0; i < 4; ++i) {
            q[i] = vert(lo.x + c[i][0] * (hi.x - lo.x), lo.y + c[i][1] * (hi.y - lo.y), lo.z + c[i][2] * (hi.z - lo.z),
                        0, 0, color, -1.0f, -1.0f, false, false, kShade[f]);
        }
        pushQuad(out, q);
    }
}

void setupAttributes() {
    const GLsizei stride = sizeof(Vertex);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(Vertex, posX));         glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(Vertex, tintR));        glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(Vertex, texCoordX));    glEnableVertexAttribArray(2);
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(Vertex, baseLayer));    glEnableVertexAttribArray(3);
    glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(Vertex, overlayLayer)); glEnableVertexAttribArray(4);
    glVertexAttribPointer(5, 1, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(Vertex, tintBase));     glEnableVertexAttribArray(5);
    glVertexAttribPointer(6, 1, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(Vertex, tintOverlay));  glEnableVertexAttribArray(6);
    glVertexAttribPointer(7, 1, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(Vertex, faceShading));  glEnableVertexAttribArray(7);
    glVertexAttribPointer(8, 4, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(Vertex, lightSky));     glEnableVertexAttribArray(8);
}

} // namespace

void FirstPersonView::update(float dt, uint8_t held, int slot, float speed, bool onGround, float yaw, float pitch) {
    // Switching what's in the hand: it dips out of view and comes back up
    if (held != m_held || slot != m_slot) {
        if (m_slot != -1) m_equip = 0.0f;
        m_held = held;
        m_slot = slot;
    }
    m_equip = std::min(1.0f, m_equip + dt / kEquipSeconds);

    if (m_swing >= 0.0f) {
        m_swing += dt / kSwingSeconds;
        if (m_swing >= 1.0f) m_swing = -1.0f;
    }

    // Walking bob, Minecraft's way: the phase follows the distance walked (0.6 per block), the
    // strength eases towards min(0.1, speed per tick) on the ground and to 0 in the air
    const float perTick = speed / 20.0f;
    const float target = onGround ? std::min(0.1f, perTick) : 0.0f;
    m_bob += (target - m_bob) * std::min(1.0f, dt * 8.0f);
    m_walkDist += speed * dt * 0.6f;

    // The hand trails behind the camera when you turn and catches up (Minecraft: halfway each tick)
    if (!m_lagInit) { m_lagYaw = yaw; m_lagPitch = pitch; m_lagInit = true; }
    float dYaw = yaw - m_lagYaw;
    while (dYaw > 180.0f) { dYaw -= 360.0f; m_lagYaw += 360.0f; }
    while (dYaw < -180.0f) { dYaw += 360.0f; m_lagYaw -= 360.0f; }
    const float follow = 1.0f - std::pow(0.5f, dt * 20.0f);
    m_lagYaw += (yaw - m_lagYaw) * follow;
    m_lagPitch += (pitch - m_lagPitch) * follow;
    m_yawLag = yaw - m_lagYaw;
    m_pitchLag = pitch - m_lagPitch;
}

void FirstPersonView::swing() { m_swing = 0.0f; }

void FirstPersonView::buildMesh(uint8_t id) {
    m_meshFor = id;
    std::vector<Vertex> verts;
    auto& reg = BlockRegistry::getInstance();
    m_meshIsFlat = false;
    if (id != 0) {
        const BlockProperty& p = reg.getBlock(id);
        const bool flat = p.inventoryLayer >= 0 || p.pattern == "plantlike";
        if (flat) {
            // An item picture (stick, tool, door...) or a plant: a card, both sides
            m_meshIsFlat = true;
            int layer = p.inventoryLayer;
            bool tint = false;
            if (layer < 0) {
                for (int f : { 0, 2, 4, 1, 5, 3 }) if (layer < 0 && p.faces[f].baseLayer >= 0) { layer = p.faces[f].baseLayer; tint = p.faces[f].tintBase; }
            }
            const glm::vec3 color = tint ? p.defaultTint : glm::vec3(1.0f);
            Vertex q[4] = {
                vert(-0.5f, -0.5f, 0, 0, 1, color, static_cast<float>(layer), -1, tint, false, 1.0f),
                vert( 0.5f, -0.5f, 0, 1, 1, color, static_cast<float>(layer), -1, tint, false, 1.0f),
                vert( 0.5f,  0.5f, 0, 1, 0, color, static_cast<float>(layer), -1, tint, false, 1.0f),
                vert(-0.5f,  0.5f, 0, 0, 0, color, static_cast<float>(layer), -1, tint, false, 1.0f),
            };
            pushQuad(verts, q);
            Vertex back[4] = { q[1], q[0], q[3], q[2] };
            for (Vertex& v : back) v.faceShading = 0.8f;
            pushQuad(verts, back);
        } else {
            // A block: a little cube with its own textures
            for (int f = 0; f < 6; ++f) {
                int tf = f;
                if (p.faces[tf].baseLayer < 0) for (int g : { 2, 4, 0, 1, 5, 3 }) if (p.faces[g].baseLayer >= 0) { tf = g; break; }
                const FaceDefinition& fd = p.faces[tf];
                float c[4][3];
                cubeFace(f, c);
                Vertex q[4];
                for (int i = 0; i < 4; ++i) {
                    q[i] = vert(c[i][0] - 0.5f, c[i][1] - 0.5f, c[i][2] - 0.5f, kUV[i][0], kUV[i][1], p.defaultTint,
                                static_cast<float>(fd.baseLayer), static_cast<float>(fd.overlayLayer),
                                fd.tintBase, fd.tintOverlay, kShade[f]);
                }
                pushQuad(verts, q);
            }
        }
    } else {
        // Empty hand without an arm in the player model: a plain arm, 4 x 12 x 4 pixels, hanging
        // down from the shoulder at (0, 0, 0)
        plainBox(verts, glm::vec3(-0.125f, -0.75f, -0.125f), glm::vec3(0.125f, 0.0f, 0.125f), skinColor);
    }
    if (m_vao == 0) { glGenVertexArrays(1, &m_vao); glGenBuffers(1, &m_vbo); }
    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(Vertex), verts.data(), GL_DYNAMIC_DRAW);
    setupAttributes();
    glBindVertexArray(0);
    m_vertexCount = static_cast<int>(verts.size());
}

void FirstPersonView::drawVertices(GLuint shader, const glm::mat4& model, const glm::mat4& proj) {
    glUseProgram(shader);
    glUniformMatrix4fv(glGetUniformLocation(shader, "model"), 1, GL_FALSE, glm::value_ptr(model));
    glUniformMatrix4fv(glGetUniformLocation(shader, "view"), 1, GL_FALSE, glm::value_ptr(glm::mat4(1.0f)));
    glUniformMatrix4fv(glGetUniformLocation(shader, "projection"), 1, GL_FALSE, glm::value_ptr(proj));
    glBindVertexArray(m_vao);
    glDrawArrays(GL_TRIANGLES, 0, m_vertexCount);
    glBindVertexArray(0);
}

void FirstPersonView::render(GLuint blockShader, GLuint modelShader, GLuint textureArray, int width, int height,
                             const glm::vec4& light, const std::shared_ptr<AnimatedModel>& playerModel, bool leftHanded) {
    if (!enabled || width <= 0 || height <= 0) return;

    // Its own projection (the hand doesn't stretch with the field of view setting) and depth
    const glm::mat4 proj = glm::perspective(glm::radians(70.0f), static_cast<float>(width) / height, 0.05f, 10.0f);
    glClear(GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // Everything is placed for the right hand; left-handed players get the mirror image
    const glm::mat4 mirror = leftHanded ? glm::scale(glm::mat4(1.0f), glm::vec3(-1.0f, 1.0f, 1.0f)) : glm::mat4(1.0f);

    // Minecraft's swing curves (s = 0..1 over the 0.3 s swing)
    const bool sw = m_swing >= 0.0f;
    const float sP = std::max(m_swing, 0.0f);
    const float sq = std::sqrt(sP);
    const float fArc = sw ? std::sin(sq * kPi) : 0.0f;          // out and back
    const float fDip = sw ? std::sin(sP * sP * kPi) : 0.0f;     // late turn
    const float fUp  = sw ? std::sin(sq * 2.0f * kPi) : 0.0f;   // up, then down
    const float fFwd = sw ? std::sin(sP * kPi) : 0.0f;          // forward
    const float lower = (1.0f - m_equip) * 0.6f;                // dips when switching slots

    // Camera motion shared by everything in the hand: Minecraft's walking bob, then the lag
    // behind the camera when turning (a tenth of how far the hand is behind)
    const float ph = m_walkDist * kPi;
    glm::mat4 cam = glm::translate(glm::mat4(1.0f), glm::vec3(std::sin(ph) * m_bob * 0.5f, -std::fabs(std::cos(ph) * m_bob), 0.0f));
    cam = glm::rotate(cam, glm::radians(std::sin(ph) * m_bob * 3.0f), glm::vec3(0, 0, 1));
    cam = glm::rotate(cam, glm::radians(std::fabs(std::cos(ph - 0.2f) * m_bob) * 5.0f), glm::vec3(1, 0, 0));
    cam = glm::rotate(cam, glm::radians(-m_pitchLag * 0.1f), glm::vec3(1, 0, 0));
    cam = glm::rotate(cam, glm::radians(m_yawLag * 0.1f), glm::vec3(0, 1, 0));
    const glm::mat4 base = cam * mirror;

    if (m_meshFor != m_held || m_vao == 0) buildMesh(m_held);

    if (m_held != 0) {
        glUseProgram(blockShader);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D_ARRAY, textureArray);
        glUniform1i(glGetUniformLocation(blockShader, "textureArray"), 0);
        glUniform1i(glGetUniformLocation(blockShader, "u_IsGUI"), 0);
        glUniform1i(glGetUniformLocation(blockShader, "u_Unlit"), 0);
        glUniform1i(glGetUniformLocation(blockShader, "u_VoxelLight"), 0);
        glUniform4f(glGetUniformLocation(blockShader, "u_FallbackLight"), light.x, light.y, light.z, light.w);
        glUniform1f(glGetUniformLocation(blockShader, "u_ChunkFade"), 1.0f);
        GLint fogWas = 0;
        const GLint fogLoc = glGetUniformLocation(blockShader, "u_FogEnabled");
        if (fogLoc >= 0) { glGetUniformiv(blockShader, fogLoc, &fogWas); glUniform1i(fogLoc, 0); }

        // Where Minecraft holds things (measured against its screenshots): (0.56, -0.52, -0.72),
        // with its swing: out to the side and back, up then down, forward, while turning
        glm::mat4 m = base * glm::translate(glm::mat4(1.0f), glm::vec3(-0.4f * fArc, 0.2f * fUp, -0.2f * fFwd));
        m = glm::translate(m, glm::vec3(0.56f, -0.52f - lower, -0.72f));
        m = glm::rotate(m, glm::radians(45.0f - 20.0f * fDip), glm::vec3(0, 1, 0));
        m = glm::rotate(m, glm::radians(-20.0f * fArc), glm::vec3(0, 0, 1));
        m = glm::rotate(m, glm::radians(-80.0f * fArc), glm::vec3(1, 0, 0));
        if (m_meshIsFlat) {
            // Items and plants: a flat picture held up and tilted
            m = glm::rotate(m, glm::radians(-45.0f), glm::vec3(0, 1, 0));
            m = glm::translate(m, glm::vec3(0.0f, 0.17f, -0.03f));
            m = glm::rotate(m, glm::radians(-25.0f), glm::vec3(0, 1, 0));
            m = glm::rotate(m, glm::radians(15.0f), glm::vec3(0, 0, 1));
            m = glm::scale(m, glm::vec3(0.5f));
        } else {
            m = glm::scale(m, glm::vec3(0.4f));   // a block: turned 45 degrees (above), 0.4 big
        }
        drawVertices(blockShader, m, proj);
        if (fogLoc >= 0) glUniform1i(fogLoc, fogWas);
    } else {
        // Empty hand: the arm rising from the bottom corner with the hand end towards you, like
        // Minecraft's (fitted to its screenshots). The arm hangs from the shoulder at the origin.
        // Minecraft's arm swing: out and back, up then down, forward, turning 70 degrees
        glm::mat4 m = base * glm::translate(glm::mat4(1.0f), glm::vec3(-0.3f * fArc, 0.4f * fUp, -0.4f * fFwd));
        m = glm::translate(m, glm::vec3(0.570f, -0.903f - lower, -0.743f));
        m = glm::rotate(m, glm::radians(70.0f * fArc), glm::vec3(0, 1, 0));
        m = glm::rotate(m, glm::radians(-20.0f * fDip), glm::vec3(0, 0, 1));
        m = glm::rotate(m, glm::radians(74.1f), glm::vec3(0, 1, 0));
        m = glm::rotate(m, glm::radians(206.4f), glm::vec3(1, 0, 0));
        m = glm::rotate(m, glm::radians(56.0f), glm::vec3(0, 0, 1));
        m = glm::scale(m, glm::vec3(1.19f, 1.0f, 1.19f));   // a touch thicker, like Minecraft's

        bool drawn = false;
        if (playerModel && playerModel->loaded) {
            glUseProgram(modelShader);
            glUniformMatrix4fv(glGetUniformLocation(modelShader, "view"), 1, GL_FALSE, glm::value_ptr(glm::mat4(1.0f)));
            glUniformMatrix4fv(glGetUniformLocation(modelShader, "projection"), 1, GL_FALSE, glm::value_ptr(proj));
            for (const std::string& bone : leftHanded ? leftArmBones : rightArmBones) {
                // The left arm is already the mirror of the right one: mirror it back
                if (playerModel->drawBone(modelShader, bone, leftHanded ? m * glm::scale(glm::mat4(1.0f), glm::vec3(-1, 1, 1)) : m)) {
                    drawn = true;
                    break;
                }
            }
        }
        if (!drawn) {
            glUseProgram(blockShader);
            glUniform1i(glGetUniformLocation(blockShader, "u_IsGUI"), 0);
            glUniform1i(glGetUniformLocation(blockShader, "u_Unlit"), 0);
            glUniform1i(glGetUniformLocation(blockShader, "u_VoxelLight"), 0);
            glUniform4f(glGetUniformLocation(blockShader, "u_FallbackLight"), light.x, light.y, light.z, light.w);
            glUniform1f(glGetUniformLocation(blockShader, "u_ChunkFade"), 1.0f);
            GLint fogWas = 0;
            const GLint fogLoc = glGetUniformLocation(blockShader, "u_FogEnabled");
            if (fogLoc >= 0) { glGetUniformiv(blockShader, fogLoc, &fogWas); glUniform1i(fogLoc, 0); }
            drawVertices(blockShader, m, proj);
            if (fogLoc >= 0) glUniform1i(fogLoc, fogWas);
        }
    }
    glClear(GL_DEPTH_BUFFER_BIT);   // the HUD draws on top without fighting the hand
}
