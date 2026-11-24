#pragma once
// Sun shafts: beams of sunlight in the air where it gets past leaves, branches and edges
// (lighting quality High). For each pixel, the view ray is walked a short way into the scene
// (up to what's drawn there), and at each step a ray toward the sun is traced through the blocks
// around the camera (VoxelVolume.h): where the sun reaches, the air glows a little - more when
// looking toward the sun. In water the light scatters much more: blue-green beams reach down
// from the surface. Drawn at half size and added over the 3D view.

#include <glad/glad.h>
#include <glm/glm.hpp>

class VoxelVolume;

class SunShafts {
public:
    // strength: how much the air glows (0..2); sunColor: the sunlight's colour now
    void apply(int width, int height, const glm::mat4& invViewProj, const glm::vec3& camera, const glm::vec3& sunDir,
               const glm::vec3& sunColor, float strength, const VoxelVolume& voxels);

private:
    GLuint m_depth = 0, m_half = 0, m_fbo = 0, m_vao = 0, m_march = 0, m_comp = 0;
    int m_w = 0, m_h = 0;
    bool m_failed = false, m_depthOk = true;
    bool init();
    void resize(int w, int h);
};
