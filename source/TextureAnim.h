#pragma once
// Animated textures (register_block's `animation`): the picture is a vertical strip of square
// frames (top frame first), loaded into consecutive layers of the block texture array. This
// table says, for each first layer, how many frames follow and how long each one shows.
//
// The world shader (chunks, items, the hotbar's icons, the hand) looks it up on the GPU (a small
// table texture, main.cpp); pictures drawn by the inventory screens pick their frame here.

#include <cmath>
#include <vector>

struct LayerAnimation {
    int frames = 1;              // 1 = not animated
    float frameTime = 0.1f;      // seconds each frame shows
};

inline std::vector<LayerAnimation>& layerAnimations() { static std::vector<LayerAnimation> v; return v; }
inline bool& layerAnimationsChanged() { static bool b = false; return b; }
// The time the animations follow (seconds; set once a frame, the same clock as the shaders' u_Time)
inline double& animationClock() { static double t = 0.0; return t; }

inline void setLayerAnimation(int base, int frames, float frameTime) {
    if (base < 0) return;
    auto& v = layerAnimations();
    if (static_cast<int>(v.size()) <= base) v.resize(base + 1);
    v[base] = LayerAnimation{ frames, frameTime > 0.001f ? frameTime : 0.1f };
    layerAnimationsChanged() = true;
}

// The layer to show now for a picture starting at `base`
inline int animatedLayer(int base) {
    const auto& v = layerAnimations();
    if (base < 0 || base >= static_cast<int>(v.size()) || v[base].frames <= 1) return base;
    const LayerAnimation& a = v[base];
    const long long step = static_cast<long long>(std::floor(animationClock() / a.frameTime));
    return base + static_cast<int>(((step % a.frames) + a.frames) % a.frames);
}
