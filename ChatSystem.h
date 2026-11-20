#pragma once
// Chat window: message log, text input with history, "/" commands with suggestions.
// In multiplayer, typed messages are handed to the message sender (see main.cpp),
// which sends them to the server; otherwise they're shown locally.

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <cstddef>
#include <deque>
#include <functional>
#include <string>
#include <utility>
#include <vector>

struct GLFWwindow;
class FontRenderer;

class ChatSystem {
public:
    ChatSystem();
    ~ChatSystem();

    // ---- Opening / input (called from the GLFW callbacks in main.cpp) ----
    void openChat(GLFWwindow* window, int triggerKey);
    void closeChat(GLFWwindow* window);
    void toggleChat(GLFWwindow* window);
    void handleCharInput(unsigned int codepoint);
    void handleKeyInput(int key, int scancode, int action, int mods, GLFWwindow* window);

    // ---- Messages ----
    void addMessage(const std::string& message);                          // system colour
    void addMessage(const std::string& message, const glm::vec3& color);

    // ---- Commands ----
    // Shown in /help and the suggestion list.
    void registerCommand(const std::string& name, const std::string& usage, const std::string& description);
    // Gets every "/command" first (without the slash). Return true if it was handled.
    void setCommandHandler(std::function<bool(const std::string&)> handler) { m_commandHandler = std::move(handler); }
    // Gets every ".command" (without the dot): the player's own client mods. Return true if it
    // was handled; otherwise the text is sent as a normal message.
    void setLocalCommandHandler(std::function<bool(const std::string&)> handler) { m_localCommandHandler = std::move(handler); }
    // Called with every message added to the chat (after it's shown): client mods can read chat.
    void setMessageListener(std::function<void(const std::string&)> listener) { m_messageListener = std::move(listener); }
    // Sends text as if the player typed it: "/command" runs it, anything else is a chat message
    // (".commands" are not run: client mods use this, and must not call themselves)
    void sendText(const std::string& text);

    // ---- Multiplayer ----
    // Name shown before your own messages ("<Name> hello").
    void setPlayerName(const std::string& name) { m_playerName = name; }
    const std::string& playerName() const { return m_playerName; }
    // Called with every typed (non-command) message. Return true if it was sent online
    // (the server echoes it back to everyone), false to show it locally instead.
    void setMessageSender(std::function<bool(const std::string&)> sender) { m_messageSender = std::move(sender); }

    // ---- Completion (Tab) ----
    // The word being typed gets suggestions: command names (registered ones, and whatever the
    // completer adds - a server's), then their arguments, and in plain chat (after Tab) player
    // names. The completer is asked with the text before the cursor, the words before the one
    // being typed (without the "/"), that word, and whether it's a command; it returns what
    // fits (unfiltered is fine). Answers that come later (from a server): call
    // refreshSuggestions() when they arrive.
    struct Suggestion { std::string insert; std::string label; };
    using Completer = std::function<std::vector<Suggestion>(const std::string& head, const std::vector<std::string>& before,
                                                            const std::string& word, bool command)>;
    void setCompleter(Completer c) { m_completer = std::move(c); }
    void refreshSuggestions();
    // Scrolls the log while the chat is open: + = back to older lines (the mouse wheel's up)
    void scrollLog(double lines);

    // Web addresses in chat lines are links. A click on one (chat open) asks "Open this link?"
    // first; x, y in the same pixels the chat is drawn in. True = the click was the chat's.
    bool handleMouseClick(double x, double y, GLFWwindow* window);
    bool linkPromptOpen() const { return !m_pendingLink.empty(); }
    // What opens a link the player agreed to (default: weblink::open, the system browser)
    void setLinkOpener(std::function<bool(const std::string&)> opener) { m_linkOpener = std::move(opener); }

    void renderChatOverlay(FontRenderer* fontRenderer, GLuint fontShader, int screenWidth, int screenHeight);

    bool isOpen = false;
    bool suppressNextChar = false;   // skip the 'T' that opened the chat
    std::string currentInput;
    size_t maxHistorySize = 200;     // lines kept in the log (scroll back with the wheel / Page Up)

private:
    struct ChatLine {
        std::string text;
        glm::vec3 color;
        double timeAdded;
    };
    struct CommandInfo {
        std::string name;
        std::string usage;
        std::string description;
    };

    void acceptSuggestion();
    void pushLine(const std::string& text, const glm::vec3& color);
    void insertText(const std::string& text);
    void deleteBack();
    void deleteForward();
    void recallHistory(int direction);
    void sendCurrentInput(GLFWwindow* window);
    void executeCommand(const std::string& cmd);
    void showHelp(const std::string& topic);
    void ensureRectGpu();

    std::deque<ChatLine> m_lines;
    std::deque<std::string> m_sentHistory;
    std::vector<CommandInfo> m_commands;
    std::function<bool(const std::string&)> m_commandHandler;
    std::function<bool(const std::string&)> m_localCommandHandler;
    std::function<void(const std::string&)> m_messageListener;
    std::function<bool(const std::string&)> m_messageSender;
    std::string m_playerName = "Player";

    size_t m_cursor = 0;
    int m_historyIndex = -1;
    int m_suggestSel = 0;               // index into m_sugg
    int m_suggestTop = 0;               // the first suggestion shown (the list scrolls with Up/Down)
    double m_scroll = 0.0;              // lines scrolled back from the newest

    struct LinkHit { float x0, y0, x1, y1; std::string url; };
    std::vector<LinkHit> m_linkHits;    // links drawn this frame (where to click)
    LinkHit m_linkButtons[3] = {};      // Open, Copy, Cancel in the "Open this link?" box
    std::string m_pendingLink;          // asking about this one
    std::string m_linkNote;             // "Copied..."
    std::function<bool(const std::string&)> m_linkOpener;
    void openPendingLink();
    std::vector<Suggestion> m_sugg;     // for the word being typed (refreshSuggestions)
    size_t m_wordStart = 0;             // where that word starts in currentInput
    bool m_plainShown = false;          // plain chat: names show after Tab
    Completer m_completer;

    bool m_gpuReady = false;
    GLuint m_rectProgram = 0;
    GLuint m_rectVAO = 0;
    GLuint m_rectVBO = 0;
};
