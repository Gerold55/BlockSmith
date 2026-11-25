#pragma once
// A copy of the frame as it is after the solid world is drawn (colour and depth), for water
// reflections: the water's shader follows a reflected ray across this picture until it meets
// something drawn there (screen-space reflections, lighting quality High). Header only.

#include <glad/glad.h>
#include <iostream>

class SceneCopy {
public:
    // Copies the current frame (call before the see-through blocks are drawn). Returns false if
    // this driver can't copy the depth buffer (reflections then show only the sky).
    bool capture(int w, int h) {
        if (w <= 0 || h <= 0 || m_failed) return false;
        if (w != m_w || h != m_h) {
            m_w = w; m_h = h;
            if (!m_color) glGenTextures(1, &m_color);
            glBindTexture(GL_TEXTURE_2D, m_color);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            if (!m_depth) glGenTextures(1, &m_depth);
            glBindTexture(GL_TEXTURE_2D, m_depth);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, w, h, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_NONE);
        }
        while (glGetError() != GL_NO_ERROR) {}
        glBindTexture(GL_TEXTURE_2D, m_color);
        glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, w, h);
        glBindTexture(GL_TEXTURE_2D, m_depth);
        glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, w, h);
        glBindTexture(GL_TEXTURE_2D, 0);
        if (glGetError() != GL_NO_ERROR) {
            m_failed = true;
            std::cerr << "[SceneCopy] can't copy the frame here: water reflects only the sky" << std::endl;
            return false;
        }
        return true;
    }
    // Binds the copy to texture units `colorUnit` and `depthUnit` (leaves unit 0 active)
    void bind(int colorUnit, int depthUnit) const {
        glActiveTexture(GL_TEXTURE0 + colorUnit);
        glBindTexture(GL_TEXTURE_2D, m_color);
        glActiveTexture(GL_TEXTURE0 + depthUnit);
        glBindTexture(GL_TEXTURE_2D, m_depth);
        glActiveTexture(GL_TEXTURE0);
    }

private:
    GLuint m_color = 0, m_depth = 0;
    int m_w = 0, m_h = 0;
    bool m_failed = false;
};
