#include "BlockSmithShaders.h"

#include <iostream>

namespace {
const char* kVertexShader = R"(
#version 330 core
const vec2 P[3] = vec2[3](vec2(-1.0, -1.0), vec2(3.0, -1.0), vec2(-1.0, 3.0));
out vec2 uv;
void main() {
    uv = P[gl_VertexID] * 0.5 + 0.5;
    gl_Position = vec4(P[gl_VertexID], 0.0, 1.0);
}
)";

const char* kFragmentShader = R"(
#version 330 core
in vec2 uv;
out vec4 color;
uniform sampler2D scene;
uniform float u_SunHeight;
void main() {
    vec3 source = texture(scene, uv).rgb;
    float luminance = dot(source, vec3(0.2126, 0.7152, 0.0722));
    vec3 vivid = mix(vec3(luminance), source, 1.16);
    vivid = (vivid - 0.5) * 1.02 + 0.5;
    float night = 1.0 - smoothstep(-0.18, 0.25, u_SunHeight);
    vivid *= mix(1.0, 0.9, night);
    color = vec4(clamp(vivid, 0.0, 1.0), 1.0);
}
)";

GLuint compile(GLenum type, const char* source) {
    const GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        std::cerr << "[BlockSmith Shaders] shader: " << log << std::endl;
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}
} // namespace

bool BlockSmithShaders::init() {
    if (m_program) return true;
    if (m_failed) return false;

    const GLuint vertex = compile(GL_VERTEX_SHADER, kVertexShader);
    const GLuint fragment = compile(GL_FRAGMENT_SHADER, kFragmentShader);
    if (!vertex || !fragment) {
        if (vertex) glDeleteShader(vertex);
        if (fragment) glDeleteShader(fragment);
        m_failed = true;
        return false;
    }

    m_program = glCreateProgram();
    glAttachShader(m_program, vertex);
    glAttachShader(m_program, fragment);
    glLinkProgram(m_program);
    glDeleteShader(vertex);
    glDeleteShader(fragment);

    GLint ok = 0;
    glGetProgramiv(m_program, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetProgramInfoLog(m_program, sizeof(log), nullptr, log);
        std::cerr << "[BlockSmith Shaders] program: " << log << std::endl;
        glDeleteProgram(m_program);
        m_program = 0;
        m_failed = true;
        return false;
    }

    glGenVertexArrays(1, &m_vao);
    return true;
}

void BlockSmithShaders::resize(int width, int height) {
    if (width == m_w && height == m_h) return;
    m_w = width;
    m_h = height;

    GLint previousTexture = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTexture);
    if (!m_scene) glGenTextures(1, &m_scene);
    glBindTexture(GL_TEXTURE_2D, m_scene);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previousTexture));
}

void BlockSmithShaders::apply(int width, int height, float sunHeight) {
    if (width <= 0 || height <= 0 || !init()) return;
    resize(width, height);

    GLint previousProgram = 0, previousVao = 0, previousReadFramebuffer = 0, previousDrawFramebuffer = 0;
    GLint previousViewport[4] = {}, previousActiveTexture = 0, previousTexture = 0, previousDepthMask = GL_TRUE;
    glGetIntegerv(GL_CURRENT_PROGRAM, &previousProgram);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &previousVao);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previousReadFramebuffer);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &previousDrawFramebuffer);
    glGetIntegerv(GL_VIEWPORT, previousViewport);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &previousActiveTexture);
    glGetIntegerv(GL_DEPTH_WRITEMASK, &previousDepthMask);

    const GLboolean depthWas = glIsEnabled(GL_DEPTH_TEST);
    const GLboolean blendWas = glIsEnabled(GL_BLEND);
    const GLboolean cullWas = glIsEnabled(GL_CULL_FACE);
    const GLboolean scissorWas = glIsEnabled(GL_SCISSOR_TEST);

    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTexture);
    glBindTexture(GL_TEXTURE_2D, m_scene);
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, width, height);

    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(previousDrawFramebuffer));
    glViewport(previousViewport[0], previousViewport[1], previousViewport[2], previousViewport[3]);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    glDepthMask(GL_FALSE);
    glBindVertexArray(m_vao);
    glUseProgram(m_program);
    glBindTexture(GL_TEXTURE_2D, m_scene);
    glUniform1i(glGetUniformLocation(m_program, "scene"), 0);
    glUniform1f(glGetUniformLocation(m_program, "u_SunHeight"), sunHeight);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    glBindVertexArray(static_cast<GLuint>(previousVao));
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previousTexture));
    glActiveTexture(static_cast<GLenum>(previousActiveTexture));
    glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(previousReadFramebuffer));
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(previousDrawFramebuffer));
    glViewport(previousViewport[0], previousViewport[1], previousViewport[2], previousViewport[3]);
    if (depthWas) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    if (blendWas) glEnable(GL_BLEND); else glDisable(GL_BLEND);
    if (cullWas) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
    if (scissorWas) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);
    glDepthMask(previousDepthMask ? GL_TRUE : GL_FALSE);
    glUseProgram(static_cast<GLuint>(previousProgram));
}
