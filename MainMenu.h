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
    GLuint iconTextureID = 0;
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
    const SubgameInfo* buttonGame() const;
    void drawButtonVerts(GLuint texturedUIShader, const std::vector<float>& v, GLuint tex);
    void shadeButtonVerts(GLuint texturedUIShader, const std::vector<float>& v, GLuint tex, bool add, const glm::vec4& color);

    std::vector<ButtonInfo> centerButtons;
    std::vector<ButtonInfo> topRightButtons;
    std::vector<SubgameInfo> availableGames;
    int selectedGameIndex = 0;
};

#endif // MAIN_MENU_H