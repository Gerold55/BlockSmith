#pragma once

#include <glad/glad.h>

class BlockSmithShaders {
public:
    void apply(int width, int height, float sunHeight);

private:
    GLuint m_scene = 0, m_vao = 0, m_program = 0;
    int m_w = 0, m_h = 0;
    bool m_failed = false;
    bool init();
    void resize(int width, int height);
};
