#include "MainMenu.h"
#include "Version.h"
#include "FontRenderer.h"
#include "Types.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stb_image.h>

namespace fs = std::filesystem;

// External reference to the active game folder managed by the engine/Lua
extern std::string g_ActiveGameFolder;

static GLuint loadMenuTexture(const std::string& path, int* outW = nullptr, int* outH = nullptr,
                              bool pixelArt = false) {
    if (!fs::exists(path)) return 0;

    int width, height, nrChannels;
    stbi_set_flip_vertically_on_load(true);
    unsigned char* data = stbi_load(path.c_str(), &width, &height, &nrChannels, 4);
    if (!data) return 0;

    GLuint texID;
    glGenTextures(1, &texID);
    glBindTexture(GL_TEXTURE_2D, texID);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    // Button pictures are usually pixel art: keep them crisp when scaled up
    const GLint filter = pixelArt ? GL_NEAREST : GL_LINEAR;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    if (outW) *outW = width;
    if (outH) *outH = height;

    stbi_image_free(data);
    return texID;
}

// The engine's own button, made in memory: used when neither the game nor the base mod has a
// button picture, so the menu never needs a file to look right. 100x20, a dark outline, a
// light bevel along the top and left, a shadow along the bottom and right, and a little
// stone-like grain. The hover one is blue with a white outline.
static GLuint makeBuiltInButton(bool hover, int& outW, int& outH) {
    const int W = 100, H = 20;
    std::vector<unsigned char> px(static_cast<size_t>(W * H * 4));
    const int base[3]  = { hover ? 110 : 108, hover ? 128 : 108, hover ? 170 : 112 };
    const int light[3] = { hover ? 150 : 150, hover ? 170 : 150, hover ? 215 : 156 };
    const int dark[3]  = { hover ? 70 : 70,   hover ? 85 : 70,   hover ? 125 : 74 };
    const int edge[3]  = { hover ? 235 : 20,  hover ? 235 : 20,  hover ? 240 : 22 };
    uint32_t seed = 12345u;
    for (int row = 0; row < H; ++row) {
        const int y = H - 1 - row;            // textures are stored bottom row first
        for (int x = 0; x < W; ++x) {
            seed = seed * 1664525u + 1013904223u;
            const int grain = static_cast<int>((seed >> 24) % 21) - 10;
            const int* c = base;
            bool noisy = true;
            if (x == 0 || y == 0 || x == W - 1 || y == H - 1) { c = edge; noisy = false; }
            else if (y <= 2 || x <= 2) { c = light; noisy = false; }
            else if (y >= H - 3 || x >= W - 3) { c = dark; noisy = false; }
            unsigned char* p = &px[static_cast<size_t>((row * W + x) * 4)];
            for (int k = 0; k < 3; ++k) p[k] = static_cast<unsigned char>(std::clamp(c[k] + (noisy ? grain : 0), 0, 255));
            p[3] = 255;
        }
    }
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, W, H, 0, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    outW = W;
    outH = H;
    return tex;
}

// "#rrggbb" or "r, g, b" (0-255) -> colour; false if it isn't one
static bool parseMenuColor(const std::string& text, glm::vec4& out) {
    std::string t = text;
    if (!t.empty() && t[0] == '#') {
        if (t.size() != 7) return false;
        const unsigned long v = std::strtoul(t.c_str() + 1, nullptr, 16);
        out = glm::vec4(((v >> 16) & 255) / 255.0f, ((v >> 8) & 255) / 255.0f, (v & 255) / 255.0f, 1.0f);
        return true;
    }
    int r = 0, g = 0, b = 0;
    if (std::sscanf(t.c_str(), "%d , %d , %d", &r, &g, &b) == 3) {
        out = glm::vec4(r / 255.0f, g / 255.0f, b / 255.0f, 1.0f);
        return true;
    }
    return false;
}

MainMenu::MainMenu() {
    scanSubgames();

    // Center vertical button stack (Minecraft style)
    centerButtons.push_back({ "singleplayer", "Singleplayer", -0.30f,  0.08f, 0.60f, 0.08f });
    centerButtons.push_back({ "multiplayer",  "Multiplayer",  -0.30f, -0.02f, 0.60f, 0.08f });
    centerButtons.push_back({ "settings",     "Settings",     -0.30f, -0.12f, 0.60f, 0.08f });
    centerButtons.push_back({ "credits",      "Credits",      -0.30f, -0.22f, 0.60f, 0.08f });
    centerButtons.push_back({ "quit",         "Exit Game",    -0.30f, -0.32f, 0.60f, 0.08f });

    // Top-right header buttons
    topRightButtons.push_back({ "texturepacks", "Textures", 0.53f, 0.86f, 0.20f, 0.08f });
    topRightButtons.push_back({ "mods",         "Mods",     0.75f, 0.86f, 0.20f, 0.08f });
}

MainMenu::~MainMenu() {
    if (vao != 0) glDeleteVertexArrays(1, &vao);
    if (vbo != 0) glDeleteBuffers(1, &vbo);
    if (headerVAO != 0) glDeleteVertexArrays(1, &headerVAO);
    if (headerVBO != 0) glDeleteBuffers(1, &headerVBO);
    if (iconVAO != 0) glDeleteVertexArrays(1, &iconVAO);
    if (iconVBO != 0) glDeleteBuffers(1, &iconVBO);

    for (auto& game : availableGames) {
        if (game.headerTextureID != 0) glDeleteTextures(1, &game.headerTextureID);
        for (GLuint& t : game.panorama) if (t != 0) glDeleteTextures(1, &t);
        if (game.iconTextureID != 0) glDeleteTextures(1, &game.iconTextureID);
        if (game.buttonIsBuiltIn) continue;   // shared: deleted once below
        if (game.buttonTextureID != 0) glDeleteTextures(1, &game.buttonTextureID);
        if (game.buttonHoverTextureID != 0) glDeleteTextures(1, &game.buttonHoverTextureID);
    }
    for (auto& game : availableGames) {
        if (!game.buttonIsBuiltIn) continue;
        glDeleteTextures(1, &game.buttonTextureID);
        glDeleteTextures(1, &game.buttonHoverTextureID);
        break;
    }
    if (overlayVAO != 0) glDeleteVertexArrays(1, &overlayVAO);
    if (overlayVBO != 0) glDeleteBuffers(1, &overlayVBO);
    if (panoramaVAO != 0) glDeleteVertexArrays(1, &panoramaVAO);
    if (panoramaVBO != 0) glDeleteBuffers(1, &panoramaVBO);
    if (panoramaProgram != 0) glDeleteProgram(panoramaProgram);
    if (buttonVAO != 0) glDeleteVertexArrays(1, &buttonVAO);
    if (buttonVBO != 0) glDeleteBuffers(1, &buttonVBO);
}

void MainMenu::scanSubgames() {
    availableGames.clear();
    std::string gamesPath = "games";
    if (!fs::exists(gamesPath) && fs::exists("../../games")) {
        gamesPath = "../../games";
    }

    std::cout << "[Menu] Scanning games path: " << fs::absolute(gamesPath) << std::endl;

    if (fs::exists(gamesPath)) {
        for (const auto& entry : fs::directory_iterator(gamesPath)) {
            if (entry.is_directory()) {
                SubgameInfo info;
                info.folderName = entry.path().filename().string();
                info.title = info.folderName;

                std::string confPath = entry.path().string() + "/game.conf";
                if (!fs::exists(confPath)) {
                    confPath = entry.path().string() + "/mod.conf";
                }

                if (fs::exists(confPath)) {
                    std::ifstream file(confPath);
                    std::string line;
                    while (std::getline(file, line)) {
                        size_t eqPos = line.find('=');
                        if (eqPos != std::string::npos) {
                            std::string key = line.substr(0, eqPos);
                            std::string value = line.substr(eqPos + 1);

                            key.erase(0, key.find_first_not_of(" \t\r\n"));
                            key.erase(key.find_last_not_of(" \t\r\n") + 1);
                            value.erase(0, value.find_first_not_of(" \t\r\n"));
                            value.erase(value.find_last_not_of(" \t\r\n") + 1);

                            if (key == "title" || key == "name") info.title = value;
                            else if (key == "author") info.author = value;
                            // Main menu buttons (see menu/button.png)
                            else if (key == "button_border") info.buttonBorder = std::max(0, std::atoi(value.c_str()));
                            else if (key == "button_text_color") parseMenuColor(value, info.buttonTextColor);
                            else if (key == "button_text_hover_color") parseMenuColor(value, info.buttonTextHoverColor);
                            else if (key == "description") info.description = value;
                        }
                    }
                }

                std::string headerPath = entry.path().string() + "/menu/header.png";

                std::string iconPath = entry.path().string() + "/menu/icon.png";
                if (!fs::exists(iconPath)) {
                    iconPath = entry.path().string() + "/icon.png";
                }

                info.headerTextureID = loadMenuTexture(headerPath, &info.headerTexW, &info.headerTexH);
                {
                    // The panorama: all six pictures, or none
                    bool all = true;
                    for (int f = 0; f < 6; ++f) {
                        if (!fs::exists(entry.path() / "menu" / ("panorama_" + std::to_string(f) + ".png"))) all = false;
                    }
                    if (all) {
                        for (int f = 0; f < 6; ++f) {
                            info.panorama[f] = loadMenuTexture((entry.path() / "menu" / ("panorama_" + std::to_string(f) + ".png")).string());
                            if (info.panorama[f] == 0) all = false;
                        }
                        info.hasPanorama = all;
                        if (all) std::cout << "[Menu] " << info.folderName << ": panorama background" << std::endl;
                    }
                }
                {
                    // menu/splashes.txt: lines shown under the logo, one picked at random
                    std::ifstream sf(entry.path() / "menu" / "splashes.txt");
                    std::string line;
                    while (std::getline(sf, line)) {
                        if (!line.empty() && line.back() == '\r') line.pop_back();
                        if (line.empty() || line[0] == '#' || line.size() > 80) continue;
                        info.splashes.push_back(line);
                    }
                }
                info.iconTextureID = loadMenuTexture(iconPath);
                // Buttons: the game's menu/button.png, else the base mod's (shared by every game),
                // else the engine's built-in one (applied after the scan). The same for the hover look.
                const std::string menuDir = entry.path().string() + "/menu/";
                const std::string baseDir = (fs::path(gamesPath).parent_path() / "mods" / "base" / "menu").string() + "/";
                for (const std::string& dir : { menuDir, baseDir }) {
                    if (info.buttonTextureID != 0) break;
                    info.buttonTextureID = loadMenuTexture(dir + "button.png", &info.buttonTexW, &info.buttonTexH, true);
                    if (info.buttonTextureID != 0) {
                        int hw = 0, hh = 0;
                        info.buttonHoverTextureID = loadMenuTexture(dir + "button_hover.png", &hw, &hh, true);
                        std::cout << "[Menu] " << info.folderName << ": buttons from " << dir << "button.png ("
                                  << info.buttonTexW << "x" << info.buttonTexH
                                  << (info.buttonHoverTextureID ? ", with hover picture" : "") << ")" << std::endl;
                    }
                }

                availableGames.push_back(info);
            }
        }
    }

    if (availableGames.empty()) {
        availableGames.push_back({ "craft_and_ruin", "Craft & Ruin", "Developer", "Default Game", 0, 0 });
    }

    // No button picture anywhere: the engine's built-in button (one copy shared by all games)
    {
        GLuint builtIn = 0, builtInHover = 0;
        int bw = 0, bh = 0;
        for (auto& g : availableGames) {
            if (g.buttonTextureID != 0) continue;
            if (builtIn == 0) {
                builtIn = makeBuiltInButton(false, bw, bh);
                builtInHover = makeBuiltInButton(true, bw, bh);
                std::cout << "[Menu] No button.png found: using the built-in buttons" << std::endl;
            }
            g.buttonTextureID = builtIn;
            g.buttonHoverTextureID = builtInHover;
            g.buttonTexW = bw;
            g.buttonTexH = bh;
            g.buttonBorder = 3;
            g.buttonIsBuiltIn = true;
        }
    }

    // Highlight the game that is actually loaded, not just the first folder found
    selectedGameIndex = 0;
    for (size_t i = 0; i < availableGames.size(); ++i) {
        if (availableGames[i].folderName == g_ActiveGameFolder) {
            selectedGameIndex = static_cast<int>(i);
            break;
        }
    }
}

static int g_TotalUIVertices = 0;

// A game's name to show: its title, or its folder name tidied up (wastelands_survival ->
// "Wastelands Survival") when game.conf has none
static std::string gameTitle(const SubgameInfo& g) {
    if (!g.title.empty()) return g.title;
    std::string out;
    bool up = true;
    for (char ch : g.folderName) {
        if (ch == '_' || ch == '-') { out += ' '; up = true; continue; }
        out += up ? static_cast<char>(std::toupper(static_cast<unsigned char>(ch))) : ch;
        up = false;
    }
    return out;
}

// Seconds since the menu first showed (the scene's animation)
static float menuSeconds() {
    static const auto start = std::chrono::steady_clock::now();
    return std::chrono::duration<float>(std::chrono::steady_clock::now() - start).count();
}

void MainMenu::layout(int screenWidth, int screenHeight) {
    if (screenWidth <= 0 || screenHeight <= 0) return;
    const float W = static_cast<float>(screenWidth), H = static_cast<float>(screenHeight);
    // Pixels -> NDC (2 units across / down the screen)
    const float px2x = 2.0f / W, px2y = 2.0f / H;

    // The top bar: a comfortable height whatever the window's size
    barPx = std::round(std::clamp(H * 0.085f, 46.0f, 72.0f));
    const float pad = std::round(barPx * 0.16f);
    const float tile = barPx - 2.0f * pad;
    // The game selector, on the left: the selected game shows its icon, title and author;
    // the others are icons
    // The selected game stays at the left; the others follow, at most 4 at a time (fewer if the
    // window is narrow). More than that, and arrows either side of them scroll through them,
    // one game per click: [Selected] < A B C D >
    const int gameCount = static_cast<int>(availableGames.size());
    const float tileGap = std::round(pad * 0.7f);
    const float selW = std::round(tile + pad * 1.4f + selTitleW + pad * 1.6f);
    const float arrowW = std::round(tile * 0.55f);
    const float startX = std::round(pad * 1.5f);
    const float room = W * 0.55f - startX - selW - tileGap;      // leaves the right for Textures / Mods
    std::vector<int> others;
    for (int i = 0; i < gameCount; ++i) if (i != selectedGameIndex) others.push_back(i);
    const int otherCount = static_cast<int>(others.size());
    int fit = std::clamp(static_cast<int>(std::floor((room + tileGap) / (tile + tileGap))), 0, 4);
    const bool arrows = otherCount > fit;
    if (arrows) fit = std::clamp(static_cast<int>(std::floor((room - 2 * (arrowW + tileGap) + tileGap) / (tile + tileGap))), 1, 4);
    gamesShown = std::min(otherCount, fit);
    gameScroll = std::clamp(gameScroll, 0, std::max(0, otherCount - gamesShown));

    gameTiles.assign(availableGames.size(), glm::vec4(0.0f));   // w = 0: scrolled out of sight
    float gx = startX;
    if (selectedGameIndex >= 0 && selectedGameIndex < gameCount) {
        gameTiles[static_cast<size_t>(selectedGameIndex)] = glm::vec4(gx, pad, selW, tile);
        gx += selW + tileGap;
    }
    arrowLeft = arrowRight = glm::vec4(0.0f);
    if (arrows) {
        arrowLeft = glm::vec4(gx, pad, arrowW, tile);
        gx += arrowW + tileGap;
    }
    for (int k = gameScroll; k < gameScroll + gamesShown && k < otherCount; ++k) {
        gameTiles[static_cast<size_t>(others[static_cast<size_t>(k)])] = glm::vec4(gx, pad, tile, tile);
        gx += tile + tileGap;
    }
    if (arrows) arrowRight = glm::vec4(gx, pad, arrowW, tile);

    // The centre stack: buttons 6% of the window's height, but never under 34 px (readable in a
    // small window) or over 76 px; ten times as wide as tall, within 86% of the width
    float btnH = std::clamp(H * 0.06f, 34.0f, 76.0f);
    const float gapOf = 0.28f;                       // gap between buttons, of a button's height
    // Room for the stack: under the header (it ends at NDC 0.40) down to above the footer text
    const float roomPx = (0.36f - (-0.86f)) * 0.5f * H;
    const size_t n = centerButtons.size();
    const float needPx = btnH * (n + (n - 1) * gapOf);
    if (needPx > roomPx && n > 0) btnH = std::max(20.0f, roomPx / (n + (n - 1) * gapOf));   // a tiny window
    const float btnW = std::min(btnH * 10.0f, W * 0.86f);
    const float gap = btnH * gapOf;
    float top = 0.30f;                               // NDC, just under the header

    // The logo: between the top bar and the buttons, at its picture's proportions
    {
        const float barBottom = barPx;
        const float stackTop = (1.0f - top) * 0.5f * H;
        const float room = stackTop - barBottom;
        float hh = std::clamp(H * 0.14f, 36.0f, 160.0f);
        hh = std::min(hh, room * 0.62f);
        const SubgameInfo* g = currentGame();
        const float aspect = (g && g->headerTexW > 0 && g->headerTexH > 0)
                           ? static_cast<float>(g->headerTexW) / g->headerTexH : 5.0f;
        float hw = hh * aspect;
        if (hw > W * 0.86f) { hw = W * 0.86f; hh = hw / aspect; }
        headerPxW = hw;
        headerPxH = hh;
        headerPxX = (W - hw) * 0.5f;
        headerPxY = barBottom + (room - hh) * 0.45f;
    }

    for (auto& b : centerButtons) {
        b.w = btnW * px2x;
        b.h = btnH * px2y;
        b.x = -b.w * 0.5f;
        b.y = top - b.h;
        top = b.y - gap * px2y;
    }

    // Top-right: flat buttons with an icon, centred in the bar, right-aligned
    const float trH = std::round(barPx * 0.56f);
    const float trW = std::round(trH * 3.7f);
    float right = W - std::round(pad * 1.5f);
    for (auto it = topRightButtons.rbegin(); it != topRightButtons.rend(); ++it) {
        it->w = trW * px2x;
        it->h = trH * px2y;
        it->x = (right - trW) * px2x - 1.0f;
        it->y = 1.0f - std::round((barPx + trH) * 0.5f) * px2y;
        right -= trW + std::round(pad * 0.8f);
    }
}

// The panorama's six faces: which way the camera looked for each picture (degrees). Rebuilt
// with the same maths as the game's camera (Player::getViewMatrix), so pictures taken in the
// game with engine.set_player_look and a 90 degree view fit together exactly.
static const float kPanoramaYaw[6]   = { -90.0f, 0.0f, 90.0f, 180.0f, -90.0f, -90.0f };
static const float kPanoramaPitch[6] = { 0.0f, 0.0f, 0.0f, 0.0f, 89.9f, -89.9f };

static glm::vec3 lookDirection(float yawDeg, float pitchDeg) {
    const float y = yawDeg * 3.14159265f / 180.0f, p = pitchDeg * 3.14159265f / 180.0f;
    return glm::vec3(std::cos(y) * std::cos(p), std::sin(p), std::sin(y) * std::cos(p));
}

void MainMenu::renderPanorama(const SubgameInfo& game, int screenWidth, int screenHeight) {
    if (panoramaProgram == 0) {
        const char* vs = "#version 330 core\n"
            "layout(location = 0) in vec3 aPos; layout(location = 1) in vec2 aUV;\n"
            "uniform mat4 u_MVP; out vec2 vUV;\n"
            "void main() { vUV = aUV; gl_Position = u_MVP * vec4(aPos, 1.0); }\n";
        const char* fs = "#version 330 core\n"
            "in vec2 vUV; uniform sampler2D u_Tex; out vec4 FragColor;\n"
            "void main() { FragColor = vec4(texture(u_Tex, vUV).rgb, 1.0); }\n";
        auto compile = [](GLenum type, const char* src) {
            GLuint sh = glCreateShader(type);
            glShaderSource(sh, 1, &src, nullptr);
            glCompileShader(sh);
            GLint ok = 0; glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
            if (!ok) { char log[512]; glGetShaderInfoLog(sh, 512, nullptr, log); std::cerr << "[Menu] Panorama shader: " << log << std::endl; }
            return sh;
        };
        const GLuint v = compile(GL_VERTEX_SHADER, vs), f = compile(GL_FRAGMENT_SHADER, fs);
        panoramaProgram = glCreateProgram();
        glAttachShader(panoramaProgram, v); glAttachShader(panoramaProgram, f);
        glLinkProgram(panoramaProgram);
        glDeleteShader(v); glDeleteShader(f);
        glGenVertexArrays(1, &panoramaVAO);
        glGenBuffers(1, &panoramaVBO);
    }

    // The camera turns slowly, tilting a little up and down, like Minecraft's title screen
    const float t = menuSeconds();
    const float yaw = -90.0f + t * 2.2f;
    const float pitch = 4.0f + std::sin(t * 0.045f) * 4.0f;
    const glm::vec3 eye(0.0f), look = lookDirection(yaw, pitch);
    const glm::mat4 proj = glm::perspective(glm::radians(80.0f),
                                            static_cast<float>(screenWidth) / std::max(1, screenHeight), 0.05f, 10.0f);
    const glm::mat4 mvp = proj * glm::lookAt(eye, look, glm::vec3(0.0f, 1.0f, 0.0f));

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glUseProgram(panoramaProgram);
    glUniformMatrix4fv(glGetUniformLocation(panoramaProgram, "u_MVP"), 1, GL_FALSE, &mvp[0][0]);
    glUniform1i(glGetUniformLocation(panoramaProgram, "u_Tex"), 0);
    glActiveTexture(GL_TEXTURE0);
    glBindVertexArray(panoramaVAO);
    glBindBuffer(GL_ARRAY_BUFFER, panoramaVBO);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
    for (int i = 0; i < 6; ++i) {
        // The face as the game's camera saw it: forward, right and up of that view, a square
        // one unit away and two across (90 degrees)
        const glm::vec3 f = lookDirection(kPanoramaYaw[i], kPanoramaPitch[i]);
        const glm::vec3 r = glm::normalize(glm::cross(f, glm::vec3(0.0f, 1.0f, 0.0f)));
        const glm::vec3 u = glm::cross(r, f);
        const glm::vec3 tl = f - r + u, tr = f + r + u, bl = f - r - u, br = f + r - u;
        // (pictures are loaded upside down: v = 1 is their top row)
        const float v[] = {
            bl.x, bl.y, bl.z, 0, 0,   br.x, br.y, br.z, 1, 0,   tr.x, tr.y, tr.z, 1, 1,
            bl.x, bl.y, bl.z, 0, 0,   tr.x, tr.y, tr.z, 1, 1,   tl.x, tl.y, tl.z, 0, 1,
        };
        glBufferData(GL_ARRAY_BUFFER, sizeof(v), v, GL_DYNAMIC_DRAW);
        glBindTexture(GL_TEXTURE_2D, game.panorama[i]);
        glDrawArrays(GL_TRIANGLES, 0, 6);
    }
    glBindVertexArray(0);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

bool MainMenu::renderBackdrop(int screenWidth, int screenHeight) {
    const SubgameInfo* g = currentGame();
    if (!g || !g->hasPanorama || screenWidth <= 0 || screenHeight <= 0) return false;
    renderPanorama(*g, screenWidth, screenHeight);
    glDisable(GL_DEPTH_TEST);
    return true;
}

void MainMenu::updateAndBuildBuffers(double mouseX, double mouseY, int screenWidth, int screenHeight) {
    if (screenWidth == 0 || screenHeight == 0) return;
    lastMouseX = mouseX;
    lastMouseY = mouseY;
    layout(screenWidth, screenHeight);

    float ndcX = (static_cast<float>(mouseX) / screenWidth) * 2.0f - 1.0f;
    float ndcY = 1.0f - (static_cast<float>(mouseY) / screenHeight) * 2.0f;

    std::vector<float> vertices;

    auto addQuad = [&](float x, float y, float w, float h, glm::vec4 color) {
        float q[] = {
            x,     y,     color.r, color.g, color.b, color.a,
            x + w, y,     color.r, color.g, color.b, color.a,
            x + w, y + h, color.r, color.g, color.b, color.a,

            x,     y,     color.r, color.g, color.b, color.a,
            x + w, y + h, color.r, color.g, color.b, color.a,
            x,     y + h, color.r, color.g, color.b, color.a
        };
        vertices.insert(vertices.end(), std::begin(q), std::end(q));
    };

    auto addBorderBox = [&](float x, float y, float w, float h, float thickness, glm::vec4 color) {
        addQuad(x, y + h - thickness, w, thickness, color);
        addQuad(x, y, w, thickness, color);
        addQuad(x, y, thickness, h, color);
        addQuad(x + w - thickness, y, thickness, h, color);
    };

    // ---- 1. The scene behind the menu: a sky, a sun, drifting clouds and three layers of
    //      blocky hills scrolling past at different speeds (all plain coloured quads)
    const float W = static_cast<float>(screenWidth), H = static_cast<float>(screenHeight);
    const float t = menuSeconds();
    // In pixels from the top left; colours at the top and the bottom
    auto px = [&](float x, float y, float w, float h, glm::vec4 top, glm::vec4 bottom) {
        if (w <= 0.0f || h <= 0.0f) return;
        const float x0 = x / W * 2.0f - 1.0f, x1 = (x + w) / W * 2.0f - 1.0f;
        const float y0 = 1.0f - (y + h) / H * 2.0f, y1 = 1.0f - y / H * 2.0f;   // bottom, top
        const glm::vec4& b = bottom; const glm::vec4& a = top;
        float q[] = {
            x0, y0, b.r, b.g, b.b, b.a,   x1, y0, b.r, b.g, b.b, b.a,   x1, y1, a.r, a.g, a.b, a.a,
            x0, y0, b.r, b.g, b.b, b.a,   x1, y1, a.r, a.g, a.b, a.a,   x0, y1, a.r, a.g, a.b, a.a,
        };
        vertices.insert(vertices.end(), std::begin(q), std::end(q));
    };
    auto pxs = [&](float x, float y, float w, float h, glm::vec4 c) { px(x, y, w, h, c, c); };
    // ...with the colour going from left to right instead
    auto pxh = [&](float x, float y, float w, float h, glm::vec4 left, glm::vec4 right) {
        if (w <= 0.0f || h <= 0.0f) return;
        const float x0 = x / W * 2.0f - 1.0f, x1 = (x + w) / W * 2.0f - 1.0f;
        const float y0 = 1.0f - (y + h) / H * 2.0f, y1 = 1.0f - y / H * 2.0f;
        const glm::vec4& l = left; const glm::vec4& r = right;
        float q[] = {
            x0, y0, l.r, l.g, l.b, l.a,   x1, y0, r.r, r.g, r.b, r.a,   x1, y1, r.r, r.g, r.b, r.a,
            x0, y0, l.r, l.g, l.b, l.a,   x1, y1, r.r, r.g, r.b, r.a,   x0, y1, l.r, l.g, l.b, l.a,
        };
        vertices.insert(vertices.end(), std::begin(q), std::end(q));
    };
    auto hash = [](int i, int seed) {
        uint32_t h = static_cast<uint32_t>(i) * 374761393u + static_cast<uint32_t>(seed) * 668265263u;
        h = (h ^ (h >> 13)) * 1274126177u;
        return static_cast<float>((h ^ (h >> 16)) & 0xFFFFFF) / 16777216.0f;
    };
    auto noise = [&](float x, int seed) {           // smooth 0..1
        const float fl = std::floor(x);
        const int i = static_cast<int>(fl);
        const float f = x - fl, s = f * f * (3.0f - 2.0f * f);
        return hash(i, seed) * (1.0f - s) + hash(i + 1, seed) * s;
    };
    const float P = std::max(3.0f, std::round(H / 110.0f));   // one "pixel" of the scene

    const SubgameInfo* sceneGame = currentGame();
    const bool panoramaShown = sceneGame && sceneGame->hasPanorama;   // drawn in render() instead
    if (!panoramaShown) {
    // Sky
    px(0, 0, W, H * 0.70f, glm::vec4(0.25f, 0.47f, 0.85f, 1), glm::vec4(0.66f, 0.82f, 0.98f, 1));
    pxs(0, H * 0.70f, W, H * 0.30f, glm::vec4(0.66f, 0.82f, 0.98f, 1));
    // Sun, with a soft square glow
    {
        const float s = P * 9.0f, sx = W * 0.13f, sy = H * 0.24f;
        for (int k = 3; k >= 1; --k) {
            const float g = s + k * P * 3.0f;
            pxs(sx - g / 2, sy - g / 2, g, g, glm::vec4(1.0f, 0.97f, 0.80f, 0.10f));
        }
        pxs(sx - s / 2, sy - s / 2, s, s, glm::vec4(1.0f, 0.96f, 0.72f, 1));
    }
    // Clouds: flat and blocky, drifting slowly right
    {
        const float span = W + 60.0f * P;
        for (int i = 0; i < 9; ++i) {
            const float cw = (10.0f + std::floor(hash(i, 11) * 14.0f)) * P * 2.0f;
            const float ch = (2.0f + std::floor(hash(i, 12) * 2.0f)) * P * 2.0f;
            const float speed = 4.0f + hash(i, 13) * 5.0f;
            float cx = std::fmod(hash(i, 14) * span + t * speed * P * 0.5f, span) - 30.0f * P;
            cx = std::round(cx / P) * P;
            const float cy = std::round((H * 0.06f + hash(i, 15) * H * 0.30f) / P) * P;
            const glm::vec4 white(1.0f, 1.0f, 1.0f, 0.82f), under(0.86f, 0.90f, 0.96f, 0.82f);
            pxs(cx, cy, cw, ch - P * 2.0f, white);
            pxs(cx, cy + ch - P * 2.0f, cw, P * 2.0f, under);
        }
    }
    // Hills: far (hazy), middle and near, each a row of columns P*2 wide
    struct Layer { float base, amp, speed, scale; int seed; glm::vec4 top, body, deep; bool trees; };
    const Layer layers[] = {
        { 0.58f, 0.20f,  5.0f, 0.045f, 21, {0.50f, 0.66f, 0.70f, 1}, {0.42f, 0.58f, 0.64f, 1}, {0.50f, 0.66f, 0.76f, 1}, false },
        { 0.70f, 0.12f, 12.0f, 0.07f, 22, {0.33f, 0.62f, 0.28f, 1}, {0.45f, 0.34f, 0.24f, 1}, {0.36f, 0.27f, 0.20f, 1}, true  },
        { 0.82f, 0.10f, 24.0f, 0.09f, 23, {0.38f, 0.70f, 0.30f, 1}, {0.50f, 0.37f, 0.25f, 1}, {0.30f, 0.30f, 0.32f, 1}, true  },
    };
    const float col = P * 2.0f;
    for (const Layer& L : layers) {
        const float scroll = t * L.speed * P * 0.25f;
        const int first = static_cast<int>(std::floor(scroll / col)) - 6;
        const int count = static_cast<int>(W / col) + 12;
        for (int c = first; c < first + count; ++c) {
            const float x = c * col - scroll;
            const float n = noise(c * L.scale, L.seed) * 0.75f + noise(c * L.scale * 3.1f, L.seed + 50) * 0.25f;
            const float top = std::round((H * L.base - n * H * L.amp) / P) * P;
            const bool hazy = !L.trees;   // the far layer: no grass edge, no trees
            const float edge = P * (hazy ? 1.0f : 2.0f);
            pxs(x, top, col, edge, L.top);                                      // grass
            px(x, top + edge, col, H - top, L.body, hazy ? L.body : L.deep);     // dirt, darker deeper
            // A tree now and then: a trunk and a block of leaves
            if (L.trees && hash(c, L.seed + 7) < 0.07f) {
                const float trunkH = P * (6.0f + std::floor(hash(c, L.seed + 8) * 4.0f));
                pxs(x + col * 0.25f, top - trunkH, col * 0.5f, trunkH, glm::vec4(0.40f, 0.29f, 0.18f, 1));
                const float lw = col * 3.0f, lh = P * 7.0f;
                pxs(x - col, top - trunkH - lh * 0.6f, lw, lh, glm::vec4(0.22f, 0.50f, 0.20f, 1));
                pxs(x - col, top - trunkH - lh * 0.6f + lh - P, lw, P, glm::vec4(0.17f, 0.40f, 0.16f, 1));
            }
        }
    }
    }   // !panoramaShown

    // Readability: the whole scene a little darker, darker still at the edges
    pxs(0, 0, W, H, glm::vec4(0.0f, 0.0f, 0.0f, 0.22f));
    const glm::vec4 clear(0.0f, 0.0f, 0.0f, 0.0f), shade(0.0f, 0.0f, 0.0f, 0.45f);
    px(0, H * 0.72f, W, H * 0.28f, clear, shade);
    {
        const float e = W * 0.16f;      // left and right edges, fading in
        pxh(0, 0, e, H, glm::vec4(0, 0, 0, 0.32f), clear);
        pxh(W - e, 0, e, H, clear, glm::vec4(0, 0, 0, 0.32f));
    }

    // ---- 2. The top bar: see-through, with a thin light line under it
    // ---- The top bar: dark glass, a fine line under it
    px(0, 0, W, barPx, glm::vec4(0.02f, 0.03f, 0.05f, 0.72f), glm::vec4(0.02f, 0.03f, 0.05f, 0.52f));
    pxs(0, barPx, W, 1.0f, glm::vec4(1.0f, 1.0f, 1.0f, 0.14f));
    pxs(0, barPx + 1.0f, W, 2.0f, glm::vec4(0.0f, 0.0f, 0.0f, 0.18f));
    // a box with its corners clipped (pixel-art rounded), and its outline
    auto box = [&](float x, float y, float w, float h, float r, glm::vec4 c) {
        pxs(x + r, y, w - 2 * r, h, c);
        pxs(x, y + r, r, h - 2 * r, c);
        pxs(x + w - r, y + r, r, h - 2 * r, c);
    };
    auto outline = [&](float x, float y, float w, float h, float r, glm::vec4 c) {
        pxs(x + r, y, w - 2 * r, 1, c);
        pxs(x + r, y + h - 1, w - 2 * r, 1, c);
        pxs(x, y + r, 1, h - 2 * r, c);
        pxs(x + w - 1, y + r, 1, h - 2 * r, c);
        if (r >= 2) {   // the clipped corners' diagonal pixels
            pxs(x + 1, y + 1, 1, 1, c); pxs(x + w - 2, y + 1, 1, 1, c);
            pxs(x + 1, y + h - 2, 1, 1, c); pxs(x + w - 2, y + h - 2, 1, 1, c);
        }
    };
    const float mouseXp = static_cast<float>(mouseX), mouseYp = static_cast<float>(mouseY);
    hoveredGame = -1;
    // The scroll arrows: a frame with a chevron, dimmed when there's nothing more that way
    hoveredArrow = 0;
    for (int side : { -1, 1 }) {
        const glm::vec4 a = (side < 0) ? arrowLeft : arrowRight;
        if (a.z <= 0.0f) continue;
        const int others = static_cast<int>(availableGames.size()) - ((selectedGameIndex >= 0) ? 1 : 0);
        const bool can = (side < 0) ? gameScroll > 0 : gameScroll + gamesShown < others;
        const bool hov = can && mouseXp >= a.x && mouseXp < a.x + a.z && mouseYp >= a.y && mouseYp < a.y + a.w;
        if (hov) hoveredArrow = side;
        box(a.x, a.y, a.z, a.w, 3.0f, glm::vec4(1.0f, 1.0f, 1.0f, hov ? 0.17f : (can ? 0.07f : 0.03f)));
        outline(a.x, a.y, a.z, a.w, 3.0f, glm::vec4(1.0f, 1.0f, 1.0f, hov ? 0.50f : (can ? 0.22f : 0.08f)));
        // the chevron: two short diagonal strokes, pixel by pixel
        const float cs = std::max(2.0f, std::round(a.w / 14.0f));      // stroke thickness
        const int steps = 4;
        const float cx = std::round(a.x + a.z * 0.5f), cy = std::round(a.y + a.w * 0.5f);
        const glm::vec4 c(1.0f, 1.0f, 1.0f, can ? (hov ? 1.0f : 0.85f) : 0.25f);
        for (int k = 0; k < steps; ++k) {
            // rows nearer the middle sit further the way the arrow points: < or >
            const float dx = static_cast<float>(steps - 1 - k) * cs * 0.7f * static_cast<float>(side);
            const float ox = std::round(cx - cs * 0.5f + dx - side * cs * 1.05f);
            pxs(ox, cy - cs * (k + 1), cs, cs, c);
            pxs(ox, cy + cs * k, cs, cs, c);
        }
    }
    for (size_t i = 0; i < gameTiles.size(); ++i) {
        const glm::vec4 t = gameTiles[i];
        if (t.z <= 0.0f) continue;                  // scrolled out of sight
        const bool sel = static_cast<int>(i) == selectedGameIndex;
        const bool hov = mouseXp >= t.x && mouseXp < t.x + t.z && mouseYp >= t.y && mouseYp < t.y + t.w;
        if (hov) hoveredGame = static_cast<int>(i);
        const float r = 3.0f, grow = 3.0f;   // the frame sits a little outside the icon
        const float fx = t.x - grow, fy = t.y - grow, fw = t.z + 2 * grow, fh = t.w + 2 * grow;
        // A game with no icon: a coloured tile (its initial is written on it when drawn)
        if (i < availableGames.size() && availableGames[i].iconTextureID == 0) {
            const std::string& n = availableGames[i].folderName;
            uint32_t h = 2166136261u;
            for (char ch : n) h = (h ^ static_cast<uint8_t>(ch)) * 16777619u;
            const float hue = (h % 360) / 360.0f;
            auto chan = [&](float off) { return 0.30f + 0.22f * std::cos(6.2832f * (hue + off)); };
            box(t.x, t.y, t.w, t.w, 2.0f, glm::vec4(chan(0.0f), chan(0.33f), chan(0.67f), 1.0f));
            pxs(t.x + 2, t.y + t.w - 3, t.w - 4, 3, glm::vec4(0.0f, 0.0f, 0.0f, 0.25f));
        }
        if (sel) {
            box(fx, fy, fw, fh, r, glm::vec4(1.0f, 1.0f, 1.0f, hov ? 0.16f : 0.11f));
            outline(fx, fy, fw, fh, r, glm::vec4(1.0f, 1.0f, 1.0f, 0.28f));
            pxs(fx + r, fy + fh - 2, fw - 2 * r, 2, glm::vec4(1.0f, 0.78f, 0.28f, 0.95f));   // the gold accent
        } else if (hov) {
            box(fx, fy, fw, fh, r, glm::vec4(1.0f, 1.0f, 1.0f, 0.12f));
            outline(fx, fy, fw, fh, r, glm::vec4(1.0f, 1.0f, 1.0f, 0.22f));
        }
    }
    // Textures / Mods: flat outlined buttons with an icon
    for (auto& btn : topRightButtons) {
        btn.isHovered = (ndcX >= btn.x && ndcX <= (btn.x + btn.w) && ndcY >= btn.y && ndcY <= (btn.y + btn.h));
        const float bx = (btn.x + 1.0f) * 0.5f * W, bw = btn.w * 0.5f * W;
        const float by = (1.0f - (btn.y + btn.h)) * 0.5f * H, bh = btn.h * 0.5f * H;
        box(bx, by, bw, bh, 3.0f, glm::vec4(1.0f, 1.0f, 1.0f, btn.isHovered ? 0.17f : 0.07f));
        outline(bx, by, bw, bh, 3.0f, glm::vec4(1.0f, 1.0f, 1.0f, btn.isHovered ? 0.50f : 0.22f));
        // the icon, in a square at the left
        const float is = std::round(bh * 0.44f), ix = std::round(bx + bh * 0.36f), iy = std::round(by + (bh - is) * 0.5f);
        const float q = std::round(is * 0.5f);
        const float a = btn.isHovered ? 1.0f : 0.85f;
        if (btn.id == "texturepacks") {
            // four tiles: a texture sheet
            pxs(ix, iy, q - 1, q - 1, glm::vec4(0.45f, 0.75f, 0.35f, a));
            pxs(ix + q, iy, q - 1, q - 1, glm::vec4(0.60f, 0.45f, 0.30f, a));
            pxs(ix, iy + q, q - 1, q - 1, glm::vec4(0.55f, 0.58f, 0.62f, a));
            pxs(ix + q, iy + q, q - 1, q - 1, glm::vec4(0.40f, 0.62f, 0.90f, a));
        } else {
            // a puzzle piece: a square with a knob on top and one on the right
            const float k = std::max(2.0f, std::round(is * 0.22f));
            const glm::vec4 c(0.95f, 0.80f, 0.40f, a);
            pxs(ix, iy + k, is - k, is - k, c);
            pxs(ix + (is - k) * 0.5f - k * 0.5f, iy, k, k, c);
            pxs(ix + is - k, iy + k + (is - k) * 0.5f - k * 0.5f, k, k, c);
        }
    }

    // The selected game's own button picture replaces the plain boxes (drawn in render)
    const bool customButtons = selectedGameIndex >= 0 && selectedGameIndex < static_cast<int>(availableGames.size())
                            && availableGames[selectedGameIndex].buttonTextureID != 0;

    // 3. Center buttons
    for (auto& btn : centerButtons) {
        btn.isHovered = (ndcX >= btn.x && ndcX <= (btn.x + btn.w) &&
                         ndcY >= btn.y && ndcY <= (btn.y + btn.h));
        if (customButtons) continue;

        glm::vec4 bgColor = btn.isHovered ? glm::vec4(0.35f, 0.35f, 0.45f, 0.95f) : glm::vec4(0.20f, 0.20f, 0.20f, 0.90f);

        float border = 0.003f;
        addQuad(btn.x - border, btn.y - border, btn.w + (border * 2.0f), btn.h + (border * 2.0f), glm::vec4(0.08f, 0.08f, 0.08f, 1.0f));
        addQuad(btn.x, btn.y, btn.w, btn.h, bgColor);
    }

    g_TotalUIVertices = static_cast<int>(vertices.size() / 6);

    // Drawn after the game icons: the games not selected are dimmed (until pointed at)
    {
        std::vector<float> ov;
        for (size_t i = 0; i < gameTiles.size(); ++i) {
            if (static_cast<int>(i) == selectedGameIndex || static_cast<int>(i) == hoveredGame) continue;
            const glm::vec4 t = gameTiles[i];
            if (t.z <= 0.0f) continue;
            const float x0 = t.x / W * 2.0f - 1.0f, x1 = (t.x + t.w) / W * 2.0f - 1.0f;
            const float y0 = 1.0f - (t.y + t.w) / H * 2.0f, y1 = 1.0f - t.y / H * 2.0f;
            const float c[4] = { 0.02f, 0.03f, 0.05f, 0.50f };
            const float q[] = { x0, y0, c[0], c[1], c[2], c[3],  x1, y0, c[0], c[1], c[2], c[3],  x1, y1, c[0], c[1], c[2], c[3],
                                x0, y0, c[0], c[1], c[2], c[3],  x1, y1, c[0], c[1], c[2], c[3],  x0, y1, c[0], c[1], c[2], c[3] };
            ov.insert(ov.end(), std::begin(q), std::end(q));
        }
        if (overlayVAO == 0) { glGenVertexArrays(1, &overlayVAO); glGenBuffers(1, &overlayVBO); }
        glBindVertexArray(overlayVAO);
        glBindBuffer(GL_ARRAY_BUFFER, overlayVBO);
        glBufferData(GL_ARRAY_BUFFER, ov.size() * sizeof(float), ov.empty() ? nullptr : ov.data(), GL_DYNAMIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(2 * sizeof(float)));
        glBindVertexArray(0);
        overlayCount = static_cast<int>(ov.size() / 6);
    }

    if (vao == 0) glGenVertexArrays(1, &vao);
    if (vbo == 0) glGenBuffers(1, &vbo);

    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_DYNAMIC_DRAW);

    GLsizei stride = 6 * sizeof(float);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, stride, (void*)(2 * sizeof(float)));

    glBindVertexArray(0);
}

std::string MainMenu::handleMouseClick(double mouseX, double mouseY, int screenWidth, int screenHeight) {
    if (screenWidth == 0 || screenHeight == 0) return "";
    layout(screenWidth, screenHeight);

    float ndcX = (static_cast<float>(mouseX) / screenWidth) * 2.0f - 1.0f;
    float ndcY = 1.0f - (static_cast<float>(mouseY) / screenHeight) * 2.0f;

    // 0. The "new version" line (bottom left)
    if (!updateNotice.empty() && updateRect.z > 0.0f && mouseX >= updateRect.x && mouseX < updateRect.x + updateRect.z &&
        mouseY >= updateRect.y && mouseY < updateRect.y + updateRect.w) {
        return "update";
    }

    // 1. The game selector: its arrows, then its tiles
    for (int side : { -1, 1 }) {
        const glm::vec4 a = (side < 0) ? arrowLeft : arrowRight;
        const float mx = static_cast<float>(mouseX), my = static_cast<float>(mouseY);
        if (a.z > 0.0f && mx >= a.x && mx < a.x + a.z && my >= a.y && my < a.y + a.w) {
            const int others = static_cast<int>(availableGames.size()) - ((selectedGameIndex >= 0) ? 1 : 0);
            gameScroll = std::clamp(gameScroll + side, 0, std::max(0, others - gamesShown));
            return "scroll_games";
        }
    }
    for (size_t i = 0; i < availableGames.size() && i < gameTiles.size(); ++i) {
        const glm::vec4 t = gameTiles[i];
        if (t.z <= 0.0f) continue;
        const float mx = static_cast<float>(mouseX), my = static_cast<float>(mouseY);
        if (mx >= t.x - 3 && mx < t.x + t.z + 3 && my >= t.y - 3 && my < t.y + t.w + 3) {
            selectedGameIndex = static_cast<int>(i);
            g_ActiveGameFolder = availableGames[selectedGameIndex].folderName;
            std::cout << "[Menu] Switched active subgame to: " << g_ActiveGameFolder << std::endl;
            return "select_game";
        }
    }

    // 2. Center buttons
    for (auto& btn : centerButtons) {
        if (ndcX >= btn.x && ndcX <= (btn.x + btn.w) && ndcY >= btn.y && ndcY <= (btn.y + btn.h)) {
            if (btn.id == "singleplayer") return "play";
            if (btn.id == "multiplayer") return "multiplayer";
            if (btn.id == "settings") return "settings";
            if (btn.id == "credits") return "credits";
            if (btn.id == "quit") return "quit";
        }
    }

    // 3. Top-right buttons
    for (auto& btn : topRightButtons) {
        if (ndcX >= btn.x && ndcX <= (btn.x + btn.w) && ndcY >= btn.y && ndcY <= (btn.y + btn.h)) {
            if (btn.id == "mods") return "mods";
            if (btn.id == "texturepacks") return "texturepacks";
        }
    }

    return "";
}

void MainMenu::render(GLuint uiShader, GLuint texturedUIShader, GLuint fontShader, FontRenderer& fontRenderer, int screenWidth, int screenHeight) {
    if (vao == 0) return;

    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // 0. The game's panorama, if it has one (the drawn landscape otherwise, in the quads below)
    if (const SubgameInfo* g = currentGame(); g && g->hasPanorama) {
        renderPanorama(*g, screenWidth, screenHeight);
        glDisable(GL_DEPTH_TEST);
    }

    // 1. Background quads and buttons
    glUseProgram(uiShader);
    glBindVertexArray(vao);
    glDrawArrays(GL_TRIANGLES, 0, g_TotalUIVertices);
    glBindVertexArray(0);

    // 1b. The game's own button pictures, if it has them
    renderButtonImages(texturedUIShader, screenWidth, screenHeight);

    // 2. The games' icons, in their tiles (square, the tile's height)
    glUseProgram(texturedUIShader);
    for (size_t i = 0; i < availableGames.size() && i < gameTiles.size(); ++i) {
        const auto& game = availableGames[i];
        if (game.iconTextureID != 0 && gameTiles[i].z > 0.0f) {
            const glm::vec4 t = gameTiles[i];
            const float iconW = t.w / screenWidth * 2.0f, iconH = t.w / screenHeight * 2.0f;
            const float ix = t.x / screenWidth * 2.0f - 1.0f;
            const float iconY = 1.0f - (t.y + t.w) / screenHeight * 2.0f;
            float quadVerts[] = {
                ix,         iconY,         0.0f, 0.0f,
                ix + iconW, iconY,         1.0f, 0.0f,
                ix + iconW, iconY + iconH, 1.0f, 1.0f,

                ix,         iconY,         0.0f, 0.0f,
                ix + iconW, iconY + iconH, 1.0f, 1.0f,
                ix,         iconY + iconH, 0.0f, 1.0f
            };

            if (iconVAO == 0) glGenVertexArrays(1, &iconVAO);
            if (iconVBO == 0) glGenBuffers(1, &iconVBO);

            glBindVertexArray(iconVAO);
            glBindBuffer(GL_ARRAY_BUFFER, iconVBO);
            glBufferData(GL_ARRAY_BUFFER, sizeof(quadVerts), quadVerts, GL_DYNAMIC_DRAW);

            GLsizei stride = 4 * sizeof(float);
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, (void*)0);
            glEnableVertexAttribArray(1);
            glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, (void*)(2 * sizeof(float)));

            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, game.iconTextureID);
            glUniform1i(glGetUniformLocation(texturedUIShader, "u_Texture"), 0);

            glDrawArrays(GL_TRIANGLES, 0, 6);
            glBindVertexArray(0);
        }
    }

    // 2b. The other games dimmed
    if (overlayCount > 0) {
        glUseProgram(uiShader);
        glBindVertexArray(overlayVAO);
        glDrawArrays(GL_TRIANGLES, 0, overlayCount);
        glBindVertexArray(0);
    }

    // 3. Title header (image, or text fallback)
    if (selectedGameIndex < static_cast<int>(availableGames.size())) {
        const auto& game = availableGames[selectedGameIndex];

        if (game.headerTextureID != 0) {
            // Where layout() put it (pixels) -> NDC, at the picture's own proportions
            const float headerW = headerPxW / screenWidth * 2.0f;
            const float headerH = headerPxH / screenHeight * 2.0f;
            const float headerX = headerPxX / screenWidth * 2.0f - 1.0f;
            const float headerY = 1.0f - (headerPxY + headerPxH) / screenHeight * 2.0f;

            float quadVerts[] = {
                headerX,           headerY,           0.0f, 0.0f,
                headerX + headerW, headerY,           1.0f, 0.0f,
                headerX + headerW, headerY + headerH, 1.0f, 1.0f,

                headerX,           headerY,           0.0f, 0.0f,
                headerX + headerW, headerY + headerH, 1.0f, 1.0f,
                headerX,           headerY + headerH, 0.0f, 1.0f
            };

            if (headerVAO == 0) glGenVertexArrays(1, &headerVAO);
            if (headerVBO == 0) glGenBuffers(1, &headerVBO);

            glBindVertexArray(headerVAO);
            glBindBuffer(GL_ARRAY_BUFFER, headerVBO);
            glBufferData(GL_ARRAY_BUFFER, sizeof(quadVerts), quadVerts, GL_DYNAMIC_DRAW);

            GLsizei stride = 4 * sizeof(float);
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, (void*)0);
            glEnableVertexAttribArray(1);
            glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, (void*)(2 * sizeof(float)));

            glUseProgram(texturedUIShader);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, game.headerTextureID);
            glUniform1i(glGetUniformLocation(texturedUIShader, "u_Texture"), 0);

            glDrawArrays(GL_TRIANGLES, 0, 6);
            glBindVertexArray(0);
        }
    }

    // Text is sized by the height of its capital letters, in pixels, and measured with the
    // font itself, so it stays centred and in proportion at any window size.
    const Dimensions2D screenRes{screenWidth, screenHeight};
    const Dimensions2D fontBase = fontRenderer.baseSize();
    const float rawCap = std::max(fontRenderer.capHeight(1.0f, fontBase), 0.001f);   // at scale 1
    // Font scales (x, y) that make capitals `capPx` pixels tall, with square letters
    auto scalesFor = [&](float capPx) {
        const float f = capPx / rawCap;
        return glm::vec2(f * fontBase.width / std::max(1, screenWidth),
                         f * fontBase.height / std::max(1, screenHeight));
    };
    // Text with a dark copy under it, a step down and right (readable on the moving scene)
    auto shadowed = [&](const std::string& s, float x, float y, glm::vec2 sc, float capPx, glm::vec4 color) {
        const float o = std::max(1.0f, std::round(capPx / 8.0f));
        fontRenderer.renderText(fontShader, s, x + o, y + o, sc.x, sc.y,
                                glm::vec4(color.r * 0.22f, color.g * 0.22f, color.b * 0.22f, color.a * 0.9f), screenRes);
        fontRenderer.renderText(fontShader, s, x, y, sc.x, sc.y, color, screenRes);
    };

    // The game selector's text: the selected game's title and author beside its icon, and the
    // name of a game pointed at under its icon
    for (size_t i = 0; i < availableGames.size() && i < gameTiles.size(); ++i) {
        const auto& g = availableGames[i];
        const glm::vec4 t = gameTiles[i];
        if (t.z <= 0.0f) continue;    // scrolled out of sight
        if (g.iconTextureID == 0) {   // the placeholder tile's initial
            const std::string name = gameTitle(g);
            std::string letter = name.empty() ? "?" : name.substr(0, 1);
            if (!letter.empty()) letter[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(letter[0])));
            const float cap = std::round(t.w * 0.42f);
            const glm::vec2 sc = scalesFor(cap);
            const float w = fontRenderer.textWidth(letter, sc.x, screenRes);
            const float dim = (static_cast<int>(i) == selectedGameIndex || static_cast<int>(i) == hoveredGame) ? 1.0f : 0.55f;
            shadowed(letter, std::round(t.x + (t.w - w) * 0.5f), std::round(t.y + (t.w + cap) * 0.5f), sc, cap, glm::vec4(dim, dim, dim, 1.0f));
        }
        if (static_cast<int>(i) == selectedGameIndex) {
            const float capT = std::max(8.0f, std::round(t.w * 0.27f)), capS = std::max(7.0f, std::round(t.w * 0.19f));
            const glm::vec2 sT = scalesFor(capT), sS = scalesFor(capS);
            const std::string title = gameTitle(g);
            const std::string sub = g.author.empty() ? "Game" : "by " + g.author;
            const float tx = t.x + t.w + std::round(barPx * 0.16f * 1.4f);
            shadowed(title, tx, std::round(t.y + t.w * 0.47f), sT, capT, glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));
            shadowed(sub, tx, std::round(t.y + t.w * 0.86f), sS, capS, glm::vec4(0.72f, 0.74f, 0.78f, 1.0f));
            const float w = std::max(fontRenderer.textWidth(title, sT.x, screenRes), fontRenderer.textWidth(sub, sS.x, screenRes));
            selTitleW = std::ceil(w);   // (the tile fits it from the next frame on)
        } else if (static_cast<int>(i) == hoveredGame) {
            const float cap = std::max(7.0f, std::round(t.w * 0.22f));
            const glm::vec2 sc = scalesFor(cap);
            const std::string title = gameTitle(g);
            const float w = fontRenderer.textWidth(title, sc.x, screenRes);
            shadowed(title, std::round(std::max(4.0f, t.x + t.z * 0.5f - w * 0.5f)), std::round(barPx + cap + 8.0f), sc, cap,
                     glm::vec4(1.0f, 0.92f, 0.65f, 1.0f));
        }
    }
    // Textures / Mods labels, after their icons
    for (const auto& btn : topRightButtons) {
        const float bx = (btn.x + 1.0f) * 0.5f * screenWidth, bw = btn.w * 0.5f * screenWidth;
        const float by = (1.0f - (btn.y + btn.h)) * 0.5f * screenHeight, bh = btn.h * 0.5f * screenHeight;
        const float cap = std::max(7.0f, std::round(bh * 0.34f));
        const glm::vec2 sc = scalesFor(cap);
        const float iconRight = bx + bh * 0.36f + bh * 0.44f;
        const float tw = fontRenderer.textWidth(btn.label, sc.x, screenRes);
        const float tx = std::round(iconRight + ((bx + bw - bh * 0.25f) - iconRight - tw) * 0.5f);
        shadowed(btn.label, tx, std::round(by + (bh + cap) * 0.5f), sc, cap,
                 btn.isHovered ? glm::vec4(1.0f, 1.0f, 1.0f, 1.0f) : glm::vec4(0.88f, 0.89f, 0.92f, 1.0f));
    }

    // No logo picture: the game's name in its place, big and gold
    const SubgameInfo* shown = currentGame();
    if (shown && shown->headerTextureID == 0 && !shown->title.empty()) {
        float capPx = headerPxH * 0.55f;
        glm::vec2 sc = scalesFor(capPx);
        float tw = fontRenderer.textWidth(shown->title, sc.x, screenRes);
        if (tw > screenWidth * 0.86f) { capPx *= screenWidth * 0.86f / tw; sc = scalesFor(capPx); tw = fontRenderer.textWidth(shown->title, sc.x, screenRes); }
        shadowed(shown->title, std::round((screenWidth - tw) * 0.5f), std::round(headerPxY + (headerPxH + capPx) * 0.5f),
                 sc, capPx, glm::vec4(1.0f, 0.84f, 0.2f, 1.0f));
    }

    // The splash line under the logo's right end, pulsing (menu/splashes.txt)
    if (shown && !shown->splashes.empty()) {
        if (splashFor != selectedGameIndex) {
            splashFor = selectedGameIndex;
            const auto seed = static_cast<size_t>(std::chrono::steady_clock::now().time_since_epoch().count());
            splash = shown->splashes[seed % shown->splashes.size()];
        }
        const float pulse = 1.0f + 0.07f * std::fabs(std::sin(menuSeconds() * 6.2832f * 0.9f));
        float capPx = std::clamp(screenHeight * 0.022f, 9.0f, 22.0f) * pulse;
        glm::vec2 sc = scalesFor(capPx);
        float tw = fontRenderer.textWidth(splash, sc.x, screenRes);
        const float cx = std::min(headerPxX + headerPxW * 0.80f, screenWidth - tw * 0.5f - 8.0f);
        shadowed(splash, std::round(cx - tw * 0.5f), std::round(headerPxY + headerPxH + capPx * 1.6f),
                 sc, capPx, glm::vec4(1.0f, 1.0f, 0.25f, 1.0f));
    } else {
        splash.clear();
    }

    // Button labels: capitals 38% of the button's height, centred both ways on the
    // measured text. NDC spans 2 units across the screen, so sizes convert with * 0.5 * size.
    constexpr float kLabelCapOfButton = 0.42f;
    auto renderCenteredButtonText = [&](const ButtonInfo& btn) {
        if (btn.label.empty()) return;

        const float btnX = ((btn.x + 1.0f) * 0.5f) * screenWidth;
        const float btnY = ((1.0f - (btn.y + btn.h)) * 0.5f) * screenHeight;
        const float btnW = btn.w * 0.5f * screenWidth;
        const float btnH = btn.h * 0.5f * screenHeight;

        const float capPx = std::max(8.0f, btnH * kLabelCapOfButton);
        const glm::vec2 sc = scalesFor(capPx);
        const float textW = fontRenderer.textWidth(btn.label, sc.x, screenRes);

        // renderText's y is the baseline: centre the capitals' height in the button
        const float pixelX = std::round(btnX + (btnW - textW) * 0.5f);
        const float pixelY = std::round(btnY + (btnH + capPx) * 0.5f);

        glm::vec4 textColor = btn.isHovered ? glm::vec4(1.0f, 1.0f, 0.6f, 1.0f)
                                            : glm::vec4(0.9f, 0.9f, 0.9f, 1.0f);
        if (selectedGameIndex >= 0 && selectedGameIndex < static_cast<int>(availableGames.size())) {
            const SubgameInfo& g = availableGames[selectedGameIndex];
            textColor = btn.isHovered ? g.buttonTextHoverColor : g.buttonTextColor;
        }
        shadowed(btn.label, pixelX, pixelY, sc, capPx, textColor);
    };

    // 4. Center button labels
    for (const auto& btn : centerButtons) renderCenteredButtonText(btn);

    // (the top bar's labels are drawn with the game selector, above)

    // 6. Version (bottom left) and copyright (bottom right), sized to the window
    {
        const float capPx = std::clamp(screenHeight * 0.014f, 9.0f, 16.0f);
        const glm::vec2 sc = scalesFor(capPx);
        const float margin = std::round(capPx * 1.5f);
        const float baseline = std::round(screenHeight - margin);
        const glm::vec4 grey(0.88f, 0.88f, 0.88f, 1.0f);
        const std::string version = "BlockSmith " BLOCKSMITH_VERSION;
        const std::string copyright = "(c) 2010-2026 Developer Studio";
        shadowed(version, margin, baseline, sc, capPx, grey);
        // A new version: one line above it, which opens the download page when clicked
        updateRect = glm::vec4(0.0f);
        if (!updateNotice.empty()) {
            const float y = std::round(baseline - capPx * 2.4f);
            const float w = fontRenderer.textWidth(updateNotice, sc.x, screenRes);
            updateRect = glm::vec4(margin - 4.0f, y - capPx * 1.4f, w + 8.0f, capPx * 2.0f);
            const bool hover = lastMouseX >= updateRect.x && lastMouseX < updateRect.x + updateRect.z &&
                               lastMouseY >= updateRect.y && lastMouseY < updateRect.y + updateRect.w;
            const glm::vec4 colour = updateRequired ? (hover ? glm::vec4(1.0f, 0.62f, 0.58f, 1.0f) : glm::vec4(1.0f, 0.45f, 0.40f, 1.0f))
                                                    : (hover ? glm::vec4(1.0f, 0.88f, 0.55f, 1.0f) : glm::vec4(1.0f, 0.78f, 0.28f, 1.0f));
            shadowed(updateNotice, margin, y, sc, capPx, colour);
        }
        const float cw = fontRenderer.textWidth(copyright, sc.x, screenRes);
        shadowed(copyright, std::round(screenWidth - margin - cw), baseline, sc, capPx, grey);
    }

    glEnable(GL_DEPTH_TEST);
}

// The 9-slice pieces of a button picture for one button (NDC rect, button height in pixels):
// the picture's corners keep their shape (scaled with the button's height, so pixel art stays
// square), the edges stretch one way and the middle both ways.
static void appendNineSlice(std::vector<float>& v, const SubgameInfo& game, float x, float y, float w, float h,
                            int screenWidth, int screenHeight) {
    const float texW = static_cast<float>(std::max(1, game.buttonTexW));
    const float texH = static_cast<float>(std::max(1, game.buttonTexH));
    // Corner size in the picture: as set in game.conf, else about a sixth of its height
    const float border = game.buttonBorder >= 0 ? static_cast<float>(game.buttonBorder)
                                                : std::max(1.0f, std::round(std::min(texW, texH) / 6.0f));
    const float btnPxH = h * 0.5f * screenHeight;
    const float scale = btnPxH / texH;
    float bx = border * scale * 2.0f / screenWidth;
    float by = border * scale * 2.0f / screenHeight;
    bx = std::min(bx, w * 0.5f);
    by = std::min(by, h * 0.5f);
    const float bu = std::min(border / texW, 0.5f), bv = std::min(border / texH, 0.5f);
    const float xs[4] = { x, x + bx, x + w - bx, x + w };
    const float ys[4] = { y, y + by, y + h - by, y + h };
    const float us[4] = { 0.0f, bu, 1.0f - bu, 1.0f };
    const float vs[4] = { 0.0f, bv, 1.0f - bv, 1.0f };   // pictures are loaded bottom-up
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            const float q[] = { xs[i], ys[j], us[i], vs[j],          xs[i + 1], ys[j], us[i + 1], vs[j],
                                xs[i + 1], ys[j + 1], us[i + 1], vs[j + 1],
                                xs[i], ys[j], us[i], vs[j],          xs[i + 1], ys[j + 1], us[i + 1], vs[j + 1],
                                xs[i], ys[j + 1], us[i], vs[j + 1] };
            v.insert(v.end(), std::begin(q), std::end(q));
        }
    }
}

void MainMenu::drawButtonVerts(GLuint texturedUIShader, const std::vector<float>& v, GLuint tex) {
    if (v.empty() || tex == 0) return;
    if (buttonVAO == 0) glGenVertexArrays(1, &buttonVAO);
    if (buttonVBO == 0) glGenBuffers(1, &buttonVBO);
    glUseProgram(texturedUIShader);
    glActiveTexture(GL_TEXTURE0);
    glUniform1i(glGetUniformLocation(texturedUIShader, "u_Texture"), 0);
    glBindVertexArray(buttonVAO);
    glBindBuffer(GL_ARRAY_BUFFER, buttonVBO);
    glBufferData(GL_ARRAY_BUFFER, v.size() * sizeof(float), v.data(), GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glBindTexture(GL_TEXTURE_2D, tex);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(v.size() / 4));
    glBindVertexArray(0);
}

// Draws over the button picture just drawn: `add` brightens it (hover without a hover
// picture), otherwise it's multiplied by `color` (disabled = darker, danger = red)
void MainMenu::shadeButtonVerts(GLuint texturedUIShader, const std::vector<float>& v, GLuint tex, bool add,
                                const glm::vec4& color) {
    if (add) {
        glBlendColor(0.0f, 0.0f, 0.0f, color.a);
        glBlendFunc(GL_CONSTANT_ALPHA, GL_ONE);
    } else {
        glBlendColor(color.r, color.g, color.b, 1.0f);
        glBlendFunc(GL_ZERO, GL_CONSTANT_COLOR);
    }
    drawButtonVerts(texturedUIShader, v, tex);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

const SubgameInfo* MainMenu::buttonGame() const {
    if (selectedGameIndex < 0 || selectedGameIndex >= static_cast<int>(availableGames.size())) return nullptr;
    const SubgameInfo& g = availableGames[selectedGameIndex];
    return g.buttonTextureID != 0 ? &g : nullptr;
}

bool MainMenu::drawButton(GLuint texturedUIShader, float x, float y, float w, float h, int state, bool danger,
                          int screenWidth, int screenHeight) {
    const SubgameInfo* game = buttonGame();
    if (!game || w <= 0.0f || h <= 0.0f || screenWidth <= 0 || screenHeight <= 0) return false;
    // Pixels (top-left origin) -> NDC
    const float nx = x / screenWidth * 2.0f - 1.0f;
    const float ny = 1.0f - (y + h) / screenHeight * 2.0f;
    const float nw = w / screenWidth * 2.0f, nh = h / screenHeight * 2.0f;
    std::vector<float> v;
    appendNineSlice(v, *game, nx, ny, nw, nh, screenWidth, screenHeight);

    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    const bool lit = state == 1 || state == 3;   // hover, selected
    const GLuint tex = (lit && game->buttonHoverTextureID != 0) ? game->buttonHoverTextureID : game->buttonTextureID;
    drawButtonVerts(texturedUIShader, v, tex);
    if (lit && game->buttonHoverTextureID == 0) shadeButtonVerts(texturedUIShader, v, tex, true, glm::vec4(0, 0, 0, 0.35f));
    if (danger) shadeButtonVerts(texturedUIShader, v, tex, false, glm::vec4(1.0f, 0.45f, 0.42f, 1.0f));
    if (state == 2) shadeButtonVerts(texturedUIShader, v, tex, false, glm::vec4(0.5f, 0.5f, 0.5f, 1.0f));
    return true;
}

glm::vec4 MainMenu::buttonTextColor(bool hovered) const {
    const SubgameInfo* g = buttonGame();
    if (!g) return hovered ? glm::vec4(1.0f, 1.0f, 0.6f, 1.0f) : glm::vec4(0.9f, 0.9f, 0.9f, 1.0f);
    return hovered ? g->buttonTextHoverColor : g->buttonTextColor;
}

// The title screen's buttons
void MainMenu::renderButtonImages(GLuint texturedUIShader, int screenWidth, int screenHeight) {
    const SubgameInfo* game = buttonGame();
    if (!game || screenWidth <= 0 || screenHeight <= 0) return;
    std::vector<float> normal, hover, highlight;
    for (const auto* list : { &centerButtons }) {   // (the top bar's buttons are drawn flat)
        for (const auto& b : *list) {
            const bool useHover = b.isHovered && game->buttonHoverTextureID != 0;
            appendNineSlice(useHover ? hover : normal, *game, b.x, b.y, b.w, b.h, screenWidth, screenHeight);
            if (b.isHovered && game->buttonHoverTextureID == 0) {
                appendNineSlice(highlight, *game, b.x, b.y, b.w, b.h, screenWidth, screenHeight);
            }
        }
    }
    drawButtonVerts(texturedUIShader, normal, game->buttonTextureID);
    drawButtonVerts(texturedUIShader, hover, game->buttonHoverTextureID);
    if (!highlight.empty()) shadeButtonVerts(texturedUIShader, highlight, game->buttonTextureID, true, glm::vec4(0, 0, 0, 0.35f));
}
