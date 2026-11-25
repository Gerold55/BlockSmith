#include "GpuCheck.h"
#include <glad/glad.h>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace {

GLuint compile(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) { glDeleteShader(s); return 0; }
    return s;
}

const char* kVS = R"(
#version 330 core
const vec2 P[3] = vec2[3](vec2(-1.0, -1.0), vec2(3.0, -1.0), vec2(-1.0, 3.0));
out vec2 uv;
void main() { uv = P[gl_VertexID] * 0.5 + 0.5; gl_Position = vec4(P[gl_VertexID], 0.0, 1.0); }
)";
// The lighting's inner loop: rays stepping cell by cell through a 128^3 volume
const char* kFS = R"(
#version 330 core
in vec2 uv;
out vec4 color;
uniform sampler3D vol;
void main() {
    float total = 0.0;
    for (int l = 0; l < 5; ++l) {          // (4 lights and the sun)
        vec3 from = vec3(uv * 100.0 + 10.0, 20.0 + float(l));
        vec3 d = normalize(vec3(0.3 + 0.1 * float(l), 0.8, 0.5));
        ivec3 cell = ivec3(floor(from));
        ivec3 stp = ivec3(sign(d));
        vec3 inv = 1.0 / max(abs(d), vec3(1e-6));
        vec3 tMax = abs(vec3(cell) + max(vec3(stp), vec3(0.0)) - from) * inv;
        float T = 1.0;
        for (int i = 0; i < 96; ++i) {
            if (tMax.x < tMax.y && tMax.x < tMax.z) { cell.x += stp.x; tMax.x += inv.x; }
            else if (tMax.y < tMax.z)              { cell.y += stp.y; tMax.y += inv.y; }
            else                                   { cell.z += stp.z; tMax.z += inv.z; }
            T *= 1.0 - texelFetch(vol, cell & ivec3(127), 0).r;
        }
        total += T;
    }
    color = vec4(vec3(total * 0.2), 1.0);
}
)";

bool isSoftware(const std::string& r) {
    std::string l = r;
    std::transform(l.begin(), l.end(), l.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    for (const char* k : { "llvmpipe", "softpipe", "swrast", "swiftshader", "gdi generic", "microsoft basic render",
                           "software rasterizer" })
        if (l.find(k) != std::string::npos) return true;
    return false;
}

// The test, in ms for one 1920x1080 frame; < 0 if it couldn't run
float runTest() {
    const int W = 960, H = 540;
    GLint prevFbo = 0, prevProg = 0, prevVp[4];
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
    glGetIntegerv(GL_CURRENT_PROGRAM, &prevProg);
    glGetIntegerv(GL_VIEWPORT, prevVp);

    // a volume of faint "blocks", so rays never stop early
    std::vector<uint8_t> cells(128 * 128 * 128);
    uint32_t seed = 12345;
    for (auto& c : cells) { seed = seed * 1664525u + 1013904223u; c = static_cast<uint8_t>((seed >> 24) & 3); }
    GLuint vol = 0, tex = 0, fbo = 0, vao = 0;
    glGenTextures(1, &vol);
    glBindTexture(GL_TEXTURE_3D, vol);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage3D(GL_TEXTURE_3D, 0, GL_R8, 128, 128, 128, 0, GL_RED, GL_UNSIGNED_BYTE, cells.data());
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, W, H, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    float ms = -1.0f;
    const GLuint vs = compile(GL_VERTEX_SHADER, kVS), fs = compile(GL_FRAGMENT_SHADER, kFS);
    GLuint prog = 0;
    if (vs && fs && glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE) {
        prog = glCreateProgram();
        glAttachShader(prog, vs);
        glAttachShader(prog, fs);
        glLinkProgram(prog);
        GLint ok = 0;
        glGetProgramiv(prog, GL_LINK_STATUS, &ok);
        if (ok) {
            glGenVertexArrays(1, &vao);
            glBindVertexArray(vao);
            glUseProgram(prog);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_3D, vol);
            glUniform1i(glGetUniformLocation(prog, "vol"), 0);
            glViewport(0, 0, W, H);
            glDisable(GL_DEPTH_TEST);
            glDisable(GL_BLEND);
            glDrawArrays(GL_TRIANGLES, 0, 3);          // warm up (compiling, uploads)
            glFinish();
            // Timed on the CPU around glFinish (waits for the GPU to finish): works with every
            // driver and GL loader, NVIDIA, AMD or Intel alike. The best of three.
            float best = 1e9f;
            for (int run = 0; run < 3; ++run) {
                const auto t0 = std::chrono::steady_clock::now();
                glDrawArrays(GL_TRIANGLES, 0, 3);
                glFinish();
                const auto t1 = std::chrono::steady_clock::now();
                best = std::min(best, std::chrono::duration<float, std::milli>(t1 - t0).count());
            }
            ms = best * (1920.0f * 1080.0f) / (static_cast<float>(W) * H);
            glBindVertexArray(0);
            glDeleteVertexArrays(1, &vao);
        }
    }
    if (prog) glDeleteProgram(prog);
    if (vs) glDeleteShader(vs);
    if (fs) glDeleteShader(fs);
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFbo));
    glDeleteFramebuffers(1, &fbo);
    glDeleteTextures(1, &tex);
    glDeleteTextures(1, &vol);
    glUseProgram(static_cast<GLuint>(prevProg));
    glViewport(prevVp[0], prevVp[1], prevVp[2], prevVp[3]);
    glEnable(GL_DEPTH_TEST);
    return ms;
}

} // namespace

GpuCaps detectGpuCaps(bool skipTest) {
    GpuCaps c;
    const GLubyte* r = glGetString(GL_RENDERER);
    c.renderer = r ? reinterpret_cast<const char*>(r) : "unknown";
    c.software = isSoftware(c.renderer);
    if (skipTest) {
        std::cout << "[GPU] " << c.renderer << ": check skipped (gpu_check = off) - every setting allowed" << std::endl;
        return c;
    }
    GLint max3d = 0;
    glGetIntegerv(GL_MAX_3D_TEXTURE_SIZE, &max3d);
    if (c.software) {
        c.maxLightQuality = 0;
        c.bloom = false;
        c.lightReason = c.bloomReason = "Not available: no graphics card driver (software rendering)";
    } else if (max3d < 128) {
        c.maxLightQuality = 0;
        c.lightReason = "Not available on this graphics card";
    } else {
        c.testMs = runTest();
        if (c.testMs < 0.0f) {
            c.maxLightQuality = 0;
            c.lightReason = "Not available: this graphics card couldn't run the lighting test";
        } else if (c.testMs > 16.0f) {
            c.maxLightQuality = 0;
            c.lightReason = "Too slow on this graphics card";
        } else if (c.testMs > 6.0f) {
            c.maxLightQuality = 1;
            c.lightReason = "High is too slow on this graphics card";
        }
    }
    std::cout << "[GPU] " << c.renderer;
    if (c.testMs >= 0.0f) std::cout << ": lighting test " << c.testMs << " ms at 1920x1080";
    std::cout << " -> lighting up to " << (c.maxLightQuality == 2 ? "High" : c.maxLightQuality == 1 ? "Low" : "Off")
              << ", bloom " << (c.bloom ? "allowed" : "off") << std::endl;
    return c;
}
