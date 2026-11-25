#pragma once
// UiDraw: what every piece of Lua UI drawing shares (Inventory.cpp draws rectangles and shapes,
// main.cpp text, pictures and buttons), plus the per-frame input every Lua screen can read.
//
//   Clipping   ui.push_clip / ui.pop_clip: nested rectangles (intersected), applied with the GPU's
//              scissor test to everything drawn. Cleared before and after each Lua screen.
//   Batching   rectangles and shapes are collected and drawn together; anything else that draws
//              (text, pictures, icons) calls flush() first, so the order on screen stays exactly
//              the order the Lua code drew in.
//   Input      mouse presses/releases (with double-clicks), the wheel, typed text and keys of the
//              current frame, for ui.get_mouse_events() & co. Works in every screen.
//   Cursor     ui.set_cursor(name) asks for a mouse cursor shape for this frame.
//   Clipboard  only right after the player pressed Ctrl+C / X / V (a mod can't read or replace
//              the clipboard behind the player's back).

#include <string>
#include <vector>

namespace uidraw {

// ---- Clipping (pixels, from the top-left of the window) ----
void pushClip(float x, float y, float w, float h);
void popClip();
void clearClips();
void applyClip();            // re-enables the scissor test for the current clip (or disables it)

// ---- Batching ----
void flush();                // draws the collected rectangles/shapes now

// ---- Input of this frame ----
struct MouseEvent {
    int button = 0;          // 0 left, 1 right, 2 middle (GLFW numbering)
    bool pressed = false;    // false = released
    double x = 0, y = 0;     // framebuffer pixels, like drawing
    int clicks = 1;          // 2 = double-click, 3 = triple (presses only)
    int mods = 0;
};
struct KeyEvent { int key = 0, action = 0, mods = 0; };

void beginFrame();           // forget last frame's input (call before glfwPollEvents)
void addMouse(const MouseEvent& e);
void addScroll(double dx, double dy);
void addChar(unsigned codepoint);
void addKey(int key, int action, int mods);
const std::vector<MouseEvent>& mouseEvents();
double scrollX();
double scrollY();
const std::string& typedText();
const std::vector<KeyEvent>& keyEvents();

// ---- Cursor ----
void requestCursor(const std::string& name);
std::string takeCursorRequest();   // what was asked for this frame ("" = the normal arrow)

// ---- Clipboard (needs the player's own Ctrl+C / X / V just before) ----
void noteShortcut(int key, int mods, double now);
bool mayReadClipboard(double now);
bool mayWriteClipboard(double now);

} // namespace uidraw
