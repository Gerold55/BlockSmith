#ifndef MAIN_MENU_H
#define MAIN_MENU_H

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <string>
#include <vector>

// Forward declarations
class FontRenderer;
struct Dimensions2D;

struct ButtonInfo {
    std::string id;
    std::string label;
    float x, y, w, h;
    bool isHovered = false;
};

struct SubgameInfo {
    std::string folderName;
    std::string title;
    std::string author;
    std::string description;
    GLuint headerTextureID = 0;
    int headerTexW = 0, headerTexH = 0;     // drawn at the picture's own proportions
    GLuint iconTextureID = 0;
    std::vector<std::string> splashes;      // menu/splashes.txt: one is shown under the logo
    // menu/panorama_0..5.png: six pictures around one spot, turning slowly behind the menu
    // (0 front, 1 right, 2 back, 3 left, 4 up, 5 down; each taken with a 90 degree view)
    GLuint panorama[6] = { 0, 0, 0, 0, 0, 0 };
    bool hasPanorama = false;
    // Button pictures (menu/button.png, menu/button_hover.png). Drawn "9-slice": the corners
    // stay as drawn, the edges and middle stretch, so one picture fits any button size.
    GLuint buttonTextureID = 0;
    GLuint buttonHoverTextureID = 0;
    int buttonTexW = 0, buttonTexH = 0;
    int buttonBorder = -1;                  // corner size in the picture's pixels (-1 = guess)
    bool buttonIsBuiltIn = false;           // the engine's own button (no picture found)
    glm::vec4 buttonTextColor{0.9f, 0.9f, 0.9f, 1.0f};
    glm::vec4 buttonTextHoverColor{1.0f, 1.0f, 0.6f, 1.0f};
};

class MainMenu {
public:
    MainMenu();
    ~MainMenu();

    void scanSubgames();
    void updateAndBuildBuffers(double mouseX, double mouseY, int screenWidth, int screenHeight);
    std::string handleMouseClick(double mouseX, double mouseY, int screenWidth, int screenHeight);
    void render(GLuint uiShader, GLuint texturedUIShader, GLuint fontShader, FontRenderer& fontRenderer, int screenWidth, int screenHeight);
    // Just the selected game's panorama, behind another title-screen page (Settings). False if
    // the game has none (the page draws its own background then).
    bool renderBackdrop(int screenWidth, int screenHeight);

    // Getters and setters for subgame integration & Lua bindings
    const std::vector<SubgameInfo>& getAvailableGames() const { return availableGames; }
    int getSelectedGameIndex() const { return selectedGameIndex; }
    void setSelectedGameIndex(int index) { selectedGameIndex = index; }

    // The current game's button picture (menu/button.png, else the base mod's, else the
    // engine's built-in one), drawn 9-slice at x, y, w, h in screen pixels from the top left.
    // state: 0 normal, 1 hover, 2 disabled, 3 selected. danger tints it red. Used by
    // ui.draw_button, so the Lua menus look like the title screen. False if nothing was drawn.
    bool drawButton(GLuint texturedUIShader, float x, float y, float w, float h, int state, bool danger,
                    int screenWidth, int screenHeight);
    glm::vec4 buttonTextColor(bool hovered) const;

private:
    GLuint vao = 0;
    GLuint vbo = 0;
    GLuint headerVAO = 0;
    GLuint headerVBO = 0;
    GLuint iconVAO = 0;
    GLuint iconVBO = 0;
    GLuint buttonVAO = 0;
    GLuint buttonVBO = 0;
    void renderButtonImages(GLuint texturedUIShader, int screenWidth, int screenHeight);
    void renderPanorama(const SubgameInfo& game, int screenWidth, int screenHeight);
    GLuint panoramaProgram = 0, panoramaVAO = 0, panoramaVBO = 0;
    // Places the buttons for this window size (in pixels, so they stay a usable size in small
    // windows instead of shrinking with them)
    void layout(int screenWidth, int screenHeight);
    const SubgameInfo* currentGame() const {
        return (selectedGameIndex >= 0 && selectedGameIndex < static_cast<int>(availableGames.size()))
             ? &availableGames[selectedGameIndex] : nullptr;
    }
    const SubgameInfo* buttonGame() const;
    void drawButtonVerts(GLuint texturedUIShader, const std::vector<float>& v, GLuint tex);
    void shadeButtonVerts(GLuint texturedUIShader, const std::vector<float>& v, GLuint tex, bool add, const glm::vec4& color);

    // The top bar (pixels from the top left), worked out by layout(): its height, and each
    // game's tile in the game selector on its left (x, y, w, h)
    float barPx = 56.0f;
    std::vector<glm::vec4> gameTiles;
    float selTitleW = 120.0f;               // the selected game's title width (measured when drawn)
    // More games than fit: only some show, with arrows either side to scroll through them
    int gameScroll = 0;                     // the first game shown
    int gamesShown = 0;
    glm::vec4 arrowLeft{0.0f}, arrowRight{0.0f};   // x, y, w, h; w = 0 when there are no arrows
    int hoveredArrow = 0;                   // -1 left, 1 right
    int hoveredGame = -1;
    GLuint overlayVAO = 0, overlayVBO = 0;  // drawn over the game icons (dimming the others)
    int overlayCount = 0;

    // The logo's place (pixels from the top left), worked out by layout()
    float headerPxX = 0.0f, headerPxY = 0.0f, headerPxW = 0.0f, headerPxH = 0.0f;
    std::string splash;                     // the splash line showing now
    int splashFor = -1;                     // ...picked for this game

    std::vector<ButtonInfo> centerButtons;
    std::vector<ButtonInfo> topRightButtons;
    std::vector<SubgameInfo> availableGames;
    int selectedGameIndex = 0;
};

#endif // MAIN_MENU_H