#include "MainMenu.h"
#include "FontRenderer.h"
#include "Types.h"
#include <algorithm>
#include <cmath>
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

                info.headerTextureID = loadMenuTexture(headerPath);
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

void MainMenu::updateAndBuildBuffers(double mouseX, double mouseY, int screenWidth, int screenHeight) {
    if (screenWidth == 0 || screenHeight == 0) return;

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

    // 1. Dark background base color
    addQuad(-1.0f, -1.0f, 2.0f, 2.0f, glm::vec4(0.12f, 0.12f, 0.12f, 1.0f));

    // 2. Top bar background strip
    addQuad(-1.0f, 0.80f, 2.0f, 0.20f, glm::vec4(0.16f, 0.16f, 0.16f, 0.95f));

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

    // 4. Top-right header buttons
    for (auto& btn : topRightButtons) {
        btn.isHovered = (ndcX >= btn.x && ndcX <= (btn.x + btn.w) &&
                         ndcY >= btn.y && ndcY <= (btn.y + btn.h));
        if (customButtons) continue;

        glm::vec4 bgColor = btn.isHovered ? glm::vec4(0.35f, 0.35f, 0.45f, 0.95f) : glm::vec4(0.20f, 0.20f, 0.20f, 0.90f);

        float border = 0.003f;
        addQuad(btn.x - border, btn.y - border, btn.w + (border * 2.0f), btn.h + (border * 2.0f), glm::vec4(0.08f, 0.08f, 0.08f, 1.0f));
        addQuad(btn.x, btn.y, btn.w, btn.h, bgColor);
    }

    // 5. Active subgame selection highlight
    float iconStartX = -0.92f;
    float iconBaseSize = 0.08f;
    float aspect = static_cast<float>(screenHeight) / static_cast<float>(screenWidth);
    float iconW = iconBaseSize * aspect;
    float iconH = iconBaseSize;
    float iconY = 0.80f + (0.20f - iconH) * 0.5f;

    if (selectedGameIndex >= 0 && selectedGameIndex < static_cast<int>(availableGames.size())) {
        float ix = iconStartX + static_cast<float>(selectedGameIndex) * (iconW + 0.04f);
        float pad = 0.005f;
        addBorderBox(ix - pad, iconY - pad, iconW + (pad * 2.0f), iconH + (pad * 2.0f), 0.003f, glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));
    }

    g_TotalUIVertices = static_cast<int>(vertices.size() / 6);

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

    float ndcX = (static_cast<float>(mouseX) / screenWidth) * 2.0f - 1.0f;
    float ndcY = 1.0f - (static_cast<float>(mouseY) / screenHeight) * 2.0f;

    // 1. Subgame icon selection
    float iconStartX = -0.92f;
    float iconBaseSize = 0.08f;
    float aspect = static_cast<float>(screenHeight) / static_cast<float>(screenWidth);
    float iconW = iconBaseSize * aspect;
    float iconH = iconBaseSize;
    float iconY = 0.80f + (0.20f - iconH) * 0.5f;

    for (size_t i = 0; i < availableGames.size(); ++i) {
        float ix = iconStartX + static_cast<float>(i) * (iconW + 0.04f);
        if (ndcX >= ix && ndcX <= (ix + iconW) && ndcY >= iconY && ndcY <= (iconY + iconH)) {
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

    // 1. Background quads and buttons
    glUseProgram(uiShader);
    glBindVertexArray(vao);
    glDrawArrays(GL_TRIANGLES, 0, g_TotalUIVertices);
    glBindVertexArray(0);

    // 1b. The game's own button pictures, if it has them
    renderButtonImages(texturedUIShader, screenWidth, screenHeight);

    // 2. Subgame icons
    glUseProgram(texturedUIShader);
    float iconStartX = -0.92f;
    float iconBaseSize = 0.08f;
    float aspect = static_cast<float>(screenHeight) / static_cast<float>(screenWidth);
    float iconW = iconBaseSize * aspect;
    float iconH = iconBaseSize;
    float iconY = 0.80f + (0.20f - iconH) * 0.5f;

    for (size_t i = 0; i < availableGames.size(); ++i) {
        const auto& game = availableGames[i];
        if (game.iconTextureID != 0) {
            float ix = iconStartX + static_cast<float>(i) * (iconW + 0.04f);
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

    // 3. Title header (image, or text fallback)
    if (selectedGameIndex < static_cast<int>(availableGames.size())) {
        const auto& game = availableGames[selectedGameIndex];

        if (game.headerTextureID != 0) {
            float headerW = 0.65f;
            float headerH = 0.18f;
            float headerX = -headerW / 2.0f;
            float headerY = 0.40f;

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
        } else {
            std::string titleText = game.title;
            float pixelX = ((-0.3f + 1.0f) * 0.5f) * screenWidth;
            float pixelY = ((1.0f - 0.45f) * 0.5f) * screenHeight;
            fontRenderer.renderText(fontShader, titleText, pixelX, pixelY, 1.2f, 1.2f, glm::vec4(1.0f, 0.84f, 0.0f, 1.0f), Dimensions2D{screenWidth, screenHeight});
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

    // Button labels: capitals 38% of the button's height, centred both ways on the
    // measured text. NDC spans 2 units across the screen, so sizes convert with * 0.5 * size.
    constexpr float kLabelCapOfButton = 0.38f;
    auto renderCenteredButtonText = [&](const ButtonInfo& btn) {
        if (btn.label.empty()) return;

        const float btnX = ((btn.x + 1.0f) * 0.5f) * screenWidth;
        const float btnY = ((1.0f - (btn.y + btn.h)) * 0.5f) * screenHeight;
        const float btnW = btn.w * 0.5f * screenWidth;
        const float btnH = btn.h * 0.5f * screenHeight;

        const float capPx = std::max(6.0f, btnH * kLabelCapOfButton);
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
        fontRenderer.renderText(fontShader, btn.label, pixelX, pixelY, sc.x, sc.y, textColor, screenRes);
    };

    // 4. Center button labels
    for (const auto& btn : centerButtons) renderCenteredButtonText(btn);

    // 5. Top-right button labels
    for (const auto& btn : topRightButtons) renderCenteredButtonText(btn);

    // 6. Version (bottom left) and copyright (bottom right), sized to the window
    {
        const float capPx = std::max(6.0f, screenHeight * 0.011f);
        const glm::vec2 sc = scalesFor(capPx);
        const float margin = std::round(capPx * 1.5f);
        const float baseline = std::round(screenHeight - margin);
        const glm::vec4 grey(0.8f, 0.8f, 0.8f, 1.0f);
        const std::string version = "BlockSmith 5.17.0-dev";
        const std::string copyright = "(c) 2010-2026 Developer Studio";
        fontRenderer.renderText(fontShader, version, margin, baseline, sc.x, sc.y, grey, screenRes);
        const float cw = fontRenderer.textWidth(copyright, sc.x, screenRes);
        fontRenderer.renderText(fontShader, copyright, std::round(screenWidth - margin - cw), baseline,
                                sc.x, sc.y, grey, screenRes);
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
    for (const auto* list : { &centerButtons, &topRightButtons }) {
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
