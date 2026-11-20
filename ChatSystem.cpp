#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include "ChatSystem.h"
#include "FontRenderer.h"
#include "WebLink.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <string>
#include <vector>

namespace {

constexpr size_t kMaxInputLength    = 200;
constexpr size_t kWrapChars         = 56;
constexpr size_t kInputVisibleChars = 56;
constexpr size_t kSentHistoryMax    = 50;
constexpr size_t kLinesClosed       = 10;
constexpr size_t kLinesOpen         = 14;
constexpr size_t kMaxSuggestions    = 6;
constexpr double kVisibleSeconds    = 10.0;
constexpr double kFadeSeconds       = 1.0;

// Pixels reserved at the bottom of the screen for the hotbar (bar height plus its
// bottom margin and any posY offset). The chat sits above this line.
constexpr float kHotbarReserve = 115.0f;

constexpr float kInputHeight    = 30.0f;
constexpr float kLinePixelH     = 24.0f;
constexpr float kTextInsetX     = 8.0f;
constexpr float kLineTextDropY  = 20.0f;    // text offset from the top of each log line
constexpr float kInputTextDropY = 25.0f;   // text offset from the top of the input bar

const glm::vec3 kPlayerColor(1.0f, 1.0f, 1.0f);
const glm::vec3 kSystemColor(1.0f, 1.0f, 0.6f);
const glm::vec3 kErrorColor(1.0f, 0.55f, 0.55f);

const char* kRectVS = R"(
#version 330 core
layout (location = 0) in vec2 aPos;
layout (location = 1) in vec4 aColor;
out vec4 vColor;
void main() {
    vColor = aColor;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)";

const char* kRectFS = R"(
#version 330 core
in vec4 vColor;
out vec4 FragColor;
void main() {
    FragColor = vColor;
}
)";

GLuint compileStage(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);

    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        std::cerr << "[Chat] Shader error: " << log << std::endl;
    }
    return s;
}

std::vector<std::string> wrapText(const std::string& text, size_t maxChars) {
    std::vector<std::string> out;
    std::string rest = text;

    while (rest.size() > maxChars) {
        size_t cut = rest.rfind(' ', maxChars);
        if (cut == std::string::npos || cut == 0) cut = maxChars;
        out.push_back(rest.substr(0, cut));
        rest = rest.substr(cut);
        if (!rest.empty() && rest[0] == ' ') rest.erase(0, 1);
    }

    if (!rest.empty() || out.empty()) out.push_back(rest);
    return out;
}

std::string toLower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string trimLeft(const std::string& s) {
    const size_t p = s.find_first_not_of(' ');
    return p == std::string::npos ? std::string() : s.substr(p);
}

// "/name usage" with no trailing space when usage is empty.
std::string commandLabel(const std::string& name, const std::string& usage) {
    if (!usage.empty() && usage[0] == '/') return usage;      // mods give the whole thing: "/give <item>"
    return "/" + name + (usage.empty() ? std::string() : " " + usage);
}

} // namespace

ChatSystem::ChatSystem() {
    registerCommand("help", "[command]", "List commands, or show details for one.");
    registerCommand("clear", "", "Clear the chat log.");
}

ChatSystem::~ChatSystem() {
    if (glfwGetCurrentContext() == nullptr) return;
    if (m_rectVBO) glDeleteBuffers(1, &m_rectVBO);
    if (m_rectVAO) glDeleteVertexArrays(1, &m_rectVAO);
    if (m_rectProgram) glDeleteProgram(m_rectProgram);
}

void ChatSystem::registerCommand(const std::string& name, const std::string& usage,
                                 const std::string& description) {
    std::string key = toLower(name);
    if (!key.empty() && key[0] == '/') key.erase(0, 1);
    if (key.empty()) return;

    for (auto& c : m_commands) {
        if (c.name == key) {
            c.usage = usage;
            c.description = description;
            return;
        }
    }
    m_commands.push_back(CommandInfo{key, usage, description});
}

void ChatSystem::refreshSuggestions() {
    m_sugg.clear();
    if (!isOpen) return;
    const std::string head = currentInput.substr(0, m_cursor);
    const bool command = !head.empty() && head[0] == '/';
    const size_t space = head.find_last_of(' ');
    m_wordStart = (space == std::string::npos) ? (command ? 1 : 0) : space + 1;
    const std::string word = head.substr(m_wordStart);
    std::vector<std::string> before;
    {
        const size_t from = command ? 1 : 0;
        std::string cur;
        for (size_t i = from; i < m_wordStart; ++i) {
            if (head[i] == ' ') { if (!cur.empty()) before.push_back(cur); cur.clear(); }
            else cur += head[i];
        }
        if (!cur.empty()) before.push_back(cur);
    }
    if (!command && !m_plainShown) return;            // plain chat: only after Tab
    if (!command && word.empty()) return;

    const std::string lw = toLower(word);
    auto fits = [&](const std::string& s) { return toLower(s).compare(0, lw.size(), lw) == 0 && toLower(s) != lw; };
    auto find = [&](const std::string& s) -> Suggestion* {
        for (auto& x : m_sugg) if (toLower(x.insert) == toLower(s)) return &x;
        return nullptr;
    };
    if (command && before.empty()) {                  // the command's name: the ones we know first
        for (const auto& c : m_commands) {
            if (fits(c.name)) m_sugg.push_back({c.name, commandLabel(c.name, c.usage) + (c.description.empty() ? "" : "  -  " + c.description)});
        }
    }
    if (m_completer) {
        for (auto& sg : m_completer(head, before, word, command)) {
            if (sg.insert.empty() || !fits(sg.insert)) continue;
            if (Suggestion* same = find(sg.insert)) {
                // The same command here and on the server: the server's usage is the one that counts
                // (not /help and /clear: this chat's own, which run here even online)
                const std::string n = toLower(sg.insert);
                if (!sg.label.empty() && command && before.empty() && n != "help" && n != "clear") same->label = sg.label;
                continue;
            }
            if (sg.label.empty()) sg.label = (command && before.empty()) ? "/" + sg.insert : sg.insert;
            m_sugg.push_back(std::move(sg));
            if (m_sugg.size() >= 100) break;
        }
    }
    if (command && before.empty()) {                  // command names in order
        std::sort(m_sugg.begin(), m_sugg.end(), [](const Suggestion& a, const Suggestion& b) { return a.insert < b.insert; });
    }
    m_suggestSel = m_sugg.empty() ? 0 : std::clamp(m_suggestSel, 0, static_cast<int>(m_sugg.size()) - 1);
    m_suggestTop = std::clamp(m_suggestTop, std::max(0, m_suggestSel - static_cast<int>(kMaxSuggestions) + 1), m_suggestSel);
}

void ChatSystem::acceptSuggestion() {
    if (m_sugg.empty() || m_wordStart > m_cursor) return;
    const int idx = std::clamp(m_suggestSel, 0, static_cast<int>(m_sugg.size()) - 1);
    const std::string ins = m_sugg[static_cast<size_t>(idx)].insert;
    size_t wordEnd = m_cursor;                        // the whole word, if the caret is inside it
    while (wordEnd < currentInput.size() && currentInput[wordEnd] != ' ') ++wordEnd;
    currentInput.replace(m_wordStart, wordEnd - m_wordStart, ins);
    m_cursor = m_wordStart + ins.size();
    if (m_cursor >= currentInput.size() || currentInput[m_cursor] != ' ') {
        currentInput.insert(m_cursor, " ");
    }
    ++m_cursor;
    if (currentInput.size() > kMaxInputLength) { currentInput.resize(kMaxInputLength); m_cursor = std::min(m_cursor, currentInput.size()); }
    m_suggestSel = 0;
    m_suggestTop = 0;
    m_plainShown = false;
    refreshSuggestions();
}

void ChatSystem::openChat(GLFWwindow* window, int triggerKey) {
    if (isOpen) return;
    isOpen = true;
    currentInput.clear();
    m_cursor = 0;
    m_historyIndex = -1;
    m_suggestSel = 0;
    m_suggestTop = 0;
    m_scroll = 0.0;
    m_plainShown = false;
    suppressNextChar = (triggerKey == GLFW_KEY_T);
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
}

void ChatSystem::closeChat(GLFWwindow* window) {
    isOpen = false;
    suppressNextChar = false;
    currentInput.clear();
    m_cursor = 0;
    m_historyIndex = -1;
    m_suggestSel = 0;
    m_suggestTop = 0;
    m_scroll = 0.0;
    m_sugg.clear();
    m_pendingLink.clear();
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
}

void ChatSystem::toggleChat(GLFWwindow* window) {
    if (isOpen) closeChat(window);
    else openChat(window, 0);
}

void ChatSystem::addMessage(const std::string& message) {
    addMessage(message, kSystemColor);
}

void ChatSystem::addMessage(const std::string& message, const glm::vec3& color) {
    for (const auto& piece : wrapText(message, kWrapChars)) {
        pushLine(piece, color);
    }
    if (m_messageListener) m_messageListener(message);
}

void ChatSystem::pushLine(const std::string& text, const glm::vec3& color) {
    if (text.empty()) return;
    m_lines.push_back(ChatLine{text, color, glfwGetTime()});
    while (m_lines.size() > maxHistorySize) m_lines.pop_front();
    if (m_scroll > 0.0) scrollLog(1.0);               // scrolled back: what you're reading stays put
}

bool ChatSystem::handleMouseClick(double x, double y, GLFWwindow* window) {
    if (!isOpen) return false;
    const float fx = static_cast<float>(x), fy = static_cast<float>(y);
    auto inside = [&](const LinkHit& r) { return fx >= r.x0 && fx < r.x1 && fy >= r.y0 && fy < r.y1; };
    if (!m_pendingLink.empty()) {                     // the "Open this link?" box takes every click
        if (inside(m_linkButtons[0])) openPendingLink();
        else if (inside(m_linkButtons[1])) {
            if (window) glfwSetClipboardString(window, m_pendingLink.c_str());
            m_linkNote = "Copied - paste it into your browser";
        } else if (inside(m_linkButtons[2])) {
            m_pendingLink.clear();
        }
        return true;
    }
    for (const auto& hit : m_linkHits) {
        if (inside(hit) && !hit.url.empty()) {
            m_pendingLink = hit.url;
            m_linkNote.clear();
            return true;
        }
    }
    return false;
}

void ChatSystem::openPendingLink() {
    const std::string url = m_pendingLink;
    m_pendingLink.clear();
    const bool ok = m_linkOpener ? m_linkOpener(url) : weblink::open(url);
    if (!ok) addMessage("Couldn't open " + url + " - copy it into your browser instead", kErrorColor);
}

void ChatSystem::scrollLog(double lines) {
    const double most = m_lines.size() > kLinesOpen ? static_cast<double>(m_lines.size() - kLinesOpen) : 0.0;
    m_scroll = std::clamp(m_scroll + lines, 0.0, most);
}

void ChatSystem::handleCharInput(unsigned int codepoint) {
    if (!m_pendingLink.empty()) return;
    if (!isOpen) return;

    if (suppressNextChar) {
        suppressNextChar = false;
        return;
    }

    if (codepoint >= 32 && codepoint < 127) {
        insertText(std::string(1, static_cast<char>(codepoint)));
    }
}

void ChatSystem::insertText(const std::string& text) {
    for (char ch : text) {
        if (currentInput.size() >= kMaxInputLength) break;
        currentInput.insert(currentInput.begin() + static_cast<std::ptrdiff_t>(m_cursor), ch);
        ++m_cursor;
    }
    m_suggestSel = 0;
    m_suggestTop = 0;
    m_plainShown = false;
    refreshSuggestions();
}

void ChatSystem::deleteBack() {
    if (m_cursor == 0) return;
    currentInput.erase(m_cursor - 1, 1);
    --m_cursor;
    m_suggestSel = 0;
    m_suggestTop = 0;
    m_plainShown = false;
    refreshSuggestions();
}

void ChatSystem::deleteForward() {
    if (m_cursor >= currentInput.size()) return;
    currentInput.erase(m_cursor, 1);
    m_suggestSel = 0;
    refreshSuggestions();
}

void ChatSystem::handleKeyInput(int key, int scancode, int action, int mods, GLFWwindow* window) {
    if (!isOpen) return;
    if (action != GLFW_PRESS && action != GLFW_REPEAT) return;

    suppressNextChar = false;
    const bool ctrl = (mods & GLFW_MOD_CONTROL) != 0;

    if (!m_pendingLink.empty()) {                     // "Open this link?": Enter opens, Esc cancels
        if (key == GLFW_KEY_ENTER || key == GLFW_KEY_KP_ENTER) openPendingLink();
        else if (key == GLFW_KEY_ESCAPE) m_pendingLink.clear();
        else if (key == GLFW_KEY_C && ctrl && window) {
            glfwSetClipboardString(window, m_pendingLink.c_str());
            m_linkNote = "Copied - paste it into your browser";
        }
        return;
    }

    switch (key) {
    case GLFW_KEY_ENTER:
    case GLFW_KEY_KP_ENTER:
        sendCurrentInput(window);
        break;

    case GLFW_KEY_ESCAPE:
        closeChat(window);
        break;

    case GLFW_KEY_TAB: {
        // Commands: the highlighted suggestion. Plain chat: the first Tab shows the player
        // names that fit (or completes the only one), the next takes the highlighted one.
        const bool command = !currentInput.empty() && currentInput[0] == '/';
        if (!command && !m_plainShown) {
            m_plainShown = true;
            refreshSuggestions();
            if (m_sugg.size() != 1) break;
        }
        acceptSuggestion();
        break;
    }

    case GLFW_KEY_UP:
    case GLFW_KEY_DOWN: {
        // While suggestions are showing, Up/Down move the highlight. Otherwise they browse history.
        if (!m_sugg.empty()) {
            // every suggestion, not just the ones showing: the list scrolls to keep it in view
            const int n = static_cast<int>(m_sugg.size());
            const int step = (key == GLFW_KEY_UP) ? -1 : 1;
            const int cur = std::clamp(m_suggestSel, 0, n - 1);
            m_suggestSel = (cur + step + n) % n;
            const int rows = static_cast<int>(kMaxSuggestions);
            if (m_suggestSel < m_suggestTop) m_suggestTop = m_suggestSel;
            if (m_suggestSel >= m_suggestTop + rows) m_suggestTop = m_suggestSel - rows + 1;
        } else {
            recallHistory(key == GLFW_KEY_UP ? +1 : -1);
        }
        break;
    }

    case GLFW_KEY_PAGE_UP:
        scrollLog(static_cast<double>(kLinesOpen - 1));
        break;
    case GLFW_KEY_PAGE_DOWN:
        scrollLog(-static_cast<double>(kLinesOpen - 1));
        break;

    case GLFW_KEY_BACKSPACE:
        deleteBack();
        break;

    case GLFW_KEY_DELETE:
        deleteForward();
        break;

    case GLFW_KEY_LEFT:
        if (m_cursor > 0) --m_cursor;
        refreshSuggestions();
        break;

    case GLFW_KEY_RIGHT:
        if (m_cursor < currentInput.size()) ++m_cursor;
        refreshSuggestions();
        break;

    case GLFW_KEY_HOME:
        m_cursor = 0;
        refreshSuggestions();
        break;

    case GLFW_KEY_END:
        m_cursor = currentInput.size();
        refreshSuggestions();
        break;

    case GLFW_KEY_V:
        if (ctrl) {
            if (const char* clip = glfwGetClipboardString(window)) {
                std::string pasted;
                for (const char* p = clip; *p; ++p) {
                    if (*p >= 32 && *p < 127) pasted += *p;
                }
                insertText(pasted);
            }
        }
        break;

    default:
        break;
    }
}

void ChatSystem::recallHistory(int direction) {
    if (m_sentHistory.empty()) return;

    const int last = static_cast<int>(m_sentHistory.size()) - 1;
    m_historyIndex = std::clamp(m_historyIndex + direction, -1, last);

    if (m_historyIndex < 0) {
        currentInput.clear();
    } else {
        currentInput = m_sentHistory[m_sentHistory.size() - 1 - static_cast<size_t>(m_historyIndex)];
    }
    m_cursor = currentInput.size();
    m_suggestSel = 0;
    refreshSuggestions();
}

void ChatSystem::sendCurrentInput(GLFWwindow* window) {
    std::string text = currentInput;
    text.erase(text.find_last_not_of(' ') + 1);      // (Tab leaves a space after the last word)

    if (!text.empty()) {
        if (m_sentHistory.empty() || m_sentHistory.back() != text) {
            m_sentHistory.push_back(text);
            while (m_sentHistory.size() > kSentHistoryMax) m_sentHistory.pop_front();
        }

        if (text[0] == '/') {
            executeCommand(text.substr(1));
        } else if (text.size() > 1 && text[0] == '.' && text[1] != '.' && text[1] != ' ' &&
                   m_localCommandHandler && m_localCommandHandler(text.substr(1))) {
            // A client mod's ".command": handled in this game only, never sent
        } else if (m_messageSender && m_messageSender(text)) {
            // Online: the server sends it to everyone (including us), so nothing to add here
        } else {
            const std::string name = m_playerName.empty() ? std::string("Player") : m_playerName;
            addMessage("<" + name + "> " + text, kPlayerColor);
        }
    }

    closeChat(window);
}

void ChatSystem::sendText(const std::string& text) {
    if (text.empty()) return;
    if (text[0] == '/') {
        executeCommand(text.substr(1));
    } else if (m_messageSender && m_messageSender(text)) {
        // sent online
    } else {
        const std::string name = m_playerName.empty() ? std::string("Player") : m_playerName;
        addMessage("<" + name + "> " + text, kPlayerColor);
    }
}

// Lua gets the first look. Built-ins are the fallback.
void ChatSystem::executeCommand(const std::string& rawCmd) {
    // "/netstats " (Tab completion adds a space) must work the same as "/netstats"
    std::string cmd = trimLeft(rawCmd);
    while (!cmd.empty() && std::isspace(static_cast<unsigned char>(cmd.back()))) cmd.pop_back();
    if (m_commandHandler && m_commandHandler(cmd)) return;

    const size_t sp = cmd.find(' ');
    const std::string name = toLower(cmd.substr(0, sp));
    const std::string args = (sp == std::string::npos) ? std::string() : trimLeft(cmd.substr(sp + 1));

    if (name.empty()) return;

    if (name == "help") {
        showHelp(args);
        return;
    }
    if (name == "clear") {
        m_lines.clear();
        return;
    }

    addMessage("Unknown command: /" + name + " (type /help for a list)", kErrorColor);
}

void ChatSystem::showHelp(const std::string& topicRaw) {
    const std::string topic = toLower(topicRaw);

    if (topic.empty()) {
        addMessage("Type / to see suggestions. Press Tab to complete, Up/Down to choose.", kSystemColor);

        std::vector<const CommandInfo*> sorted;
        for (const auto& c : m_commands) sorted.push_back(&c);
        std::sort(sorted.begin(), sorted.end(),
                  [](const CommandInfo* a, const CommandInfo* b) { return a->name < b->name; });

        for (const CommandInfo* c : sorted) {
            addMessage(commandLabel(c->name, c->usage) + "  -  " + c->description, kSystemColor);
        }
        addMessage("Use /help <command> for details.", kSystemColor);
        return;
    }

    std::string key = topic;
    if (!key.empty() && key[0] == '/') key.erase(0, 1);

    for (const auto& c : m_commands) {
        if (c.name == key) {
            addMessage(commandLabel(c.name, c.usage), kSystemColor);
            addMessage(c.description, kSystemColor);
            return;
        }
    }
    addMessage("No command named '" + key + "'.", kErrorColor);
}

void ChatSystem::ensureRectGpu() {
    if (m_gpuReady) return;
    m_gpuReady = true;

    GLuint vs = compileStage(GL_VERTEX_SHADER, kRectVS);
    GLuint fs = compileStage(GL_FRAGMENT_SHADER, kRectFS);
    m_rectProgram = glCreateProgram();
    glAttachShader(m_rectProgram, vs);
    glAttachShader(m_rectProgram, fs);
    glLinkProgram(m_rectProgram);
    glDeleteShader(vs);
    glDeleteShader(fs);

    glGenVertexArrays(1, &m_rectVAO);
    glGenBuffers(1, &m_rectVBO);
    glBindVertexArray(m_rectVAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_rectVBO);
    glBufferData(GL_ARRAY_BUFFER, 36 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);
}

void ChatSystem::renderChatOverlay(FontRenderer* fontRenderer, GLuint fontShader, int screenWidth, int screenHeight) {
    if (!fontRenderer || fontShader == 0) return;
    ensureRectGpu();

    // Use the framebuffer size for text, since it may differ from the logical size.
    GLFWwindow* currentWindow = glfwGetCurrentContext();
    int fbWidth = screenWidth, fbHeight = screenHeight;
    if (currentWindow) {
        glfwGetFramebufferSize(currentWindow, &fbWidth, &fbHeight);
    }
    const Dimensions2D screen{fbWidth, fbHeight};

    const float W = static_cast<float>(screenWidth);
    const float H = static_cast<float>(screenHeight);

    const float boxLeft  = W * 0.05f;
    const float boxWidth = W * 0.90f;

    // The chat's bottom edge sits just above the hotbar.
    const float chatBottom = H - kHotbarReserve;
    const float inputTopY  = chatBottom - kInputHeight;

    struct RectTask { float x0, y0, x1, y1; glm::vec4 color; };
    struct TextTask { std::string text; float x, y; glm::vec4 color; };

    std::vector<RectTask> rectTasks;
    std::vector<TextTask> textTasks;

    // Pixel rectangle (top-left origin) converted to NDC.
    auto addRectTask = [&](float px, float py, float pw, float ph, glm::vec4 col) {
        const float x0 = (px / W) * 2.0f - 1.0f;
        const float x1 = ((px + pw) / W) * 2.0f - 1.0f;
        const float y0 = 1.0f - ((py + ph) / H) * 2.0f;
        const float y1 = 1.0f - (py / H) * 2.0f;
        rectTasks.push_back({x0, y0, x1, y1, col});
    };

    const double now = glfwGetTime();

    // Input bar and caret.
    if (isOpen) {
        addRectTask(boxLeft, inputTopY, boxWidth, kInputHeight, glm::vec4(0.0f, 0.0f, 0.0f, 0.5f));

        const size_t start = m_cursor > kInputVisibleChars ? m_cursor - kInputVisibleChars : 0;
        std::string display = currentInput.substr(start, kInputVisibleChars);
        const size_t caretPos = std::min(m_cursor - start, display.size());

        const bool caretOn = std::fmod(now, 1.0) < 0.5;
        display.insert(caretPos, caretOn ? "_" : " ");

        textTasks.push_back({display, boxLeft + kTextInsetX, inputTopY + kInputTextDropY, glm::vec4(1.0f)});
    }

    // Suggestion list, stacked directly above the input bar.
    const int total = isOpen ? static_cast<int>(m_sugg.size()) : 0;
    if (total > 0) {
        m_suggestSel = std::clamp(m_suggestSel, 0, total - 1);
        m_suggestTop = std::clamp(m_suggestTop, 0, std::max(0, total - static_cast<int>(kMaxSuggestions)));
        m_suggestTop = std::clamp(m_suggestTop, std::max(0, m_suggestSel - static_cast<int>(kMaxSuggestions) + 1), m_suggestSel);
    }
    const int shown = std::min(total, static_cast<int>(kMaxSuggestions));
    const float suggestTop = inputTopY - static_cast<float>(shown) * kLinePixelH;

    for (int i = 0; i < shown; ++i) {
        const int index = m_suggestTop + i;
        const Suggestion& sg = m_sugg[static_cast<size_t>(index)];
        const bool selected = (index == m_suggestSel);
        const float y = suggestTop + static_cast<float>(i) * kLinePixelH;

        addRectTask(boxLeft, y, boxWidth, kLinePixelH,
                    selected ? glm::vec4(0.25f, 0.25f, 0.35f, 0.9f) : glm::vec4(0.0f, 0.0f, 0.0f, 0.6f));

        std::string label = sg.label;
        const int above = m_suggestTop, below = total - (m_suggestTop + shown);
        if (i == 0 && above > 0) label += "   (" + std::to_string(above) + " above)";
        if (i == shown - 1 && below > 0) label += "   (+" + std::to_string(below) + " more: Down or keep typing)";
        textTasks.push_back({label, boxLeft + kTextInsetX, y + kLineTextDropY,
                             selected ? glm::vec4(1.0f, 1.0f, 0.6f, 1.0f) : glm::vec4(0.8f, 0.8f, 0.8f, 1.0f)});
    }

    // Where the mouse is, in the same units as everything here (for hovering over links)
    const float textScaleX = (W > 0.0f) ? static_cast<float>(fbWidth) / W : 1.0f;
    float mouseX = -1.0f, mouseY = -1.0f;
    if (currentWindow && isOpen) {
        double cx = 0.0, cy = 0.0;
        int ww = 0, wh = 0;
        glfwGetCursorPos(currentWindow, &cx, &cy);
        glfwGetWindowSize(currentWindow, &ww, &wh);
        if (ww > 0 && wh > 0) {
            mouseX = static_cast<float>(cx * W / ww);
            mouseY = static_cast<float>(cy * H / wh);
        }
    }
    m_linkHits.clear();

    // Log lines stack upward from the suggestions when open, or from the hotbar line when closed.
    const float baseY = isOpen ? suggestTop : chatBottom;
    const size_t maxLines = isOpen ? kLinesOpen : kLinesClosed;
    size_t row = 0;
    // Scrolled back (wheel / Page Up): start that many lines up from the newest
    const size_t skip = isOpen ? std::min(static_cast<size_t>(m_scroll + 0.5), m_lines.size()) : 0;
    if (isOpen && m_lines.size() > maxLines) {
        // a scrollbar at the right edge of the log: where you are in it
        const float trackH = static_cast<float>(maxLines) * kLinePixelH;
        const float trackTop = baseY - trackH;
        const float frac = static_cast<float>(maxLines) / static_cast<float>(m_lines.size());
        const float thumbH = std::max(kLinePixelH * 0.5f, trackH * frac);
        const float back = static_cast<float>(skip) / static_cast<float>(m_lines.size() - maxLines);
        const float thumbTop = trackTop + (trackH - thumbH) * (1.0f - back);
        addRectTask(boxLeft + boxWidth - 4.0f, trackTop, 4.0f, trackH, glm::vec4(0.0f, 0.0f, 0.0f, 0.35f));
        addRectTask(boxLeft + boxWidth - 4.0f, thumbTop, 4.0f, thumbH, glm::vec4(0.8f, 0.8f, 0.85f, 0.8f));
    }

    for (auto it = m_lines.rbegin() + static_cast<std::ptrdiff_t>(skip); it != m_lines.rend() && row < maxLines; ++it, ++row) {
        const double age = now - it->timeAdded;

        float alpha = 1.0f;
        if (!isOpen) {
            if (age >= kVisibleSeconds) break;
            if (age > kVisibleSeconds - kFadeSeconds) {
                alpha = static_cast<float>((kVisibleSeconds - age) / kFadeSeconds);
            }
        }

        const float lineTop = baseY - static_cast<float>(row + 1) * kLinePixelH;
        const float bgAlpha = (isOpen ? 0.5f : 0.35f) * alpha;

        addRectTask(boxLeft, lineTop, boxWidth, kLinePixelH, glm::vec4(0.0f, 0.0f, 0.0f, bgAlpha));
        const auto links = weblink::find(it->text);
        if (links.empty()) {
            textTasks.push_back({it->text, boxLeft + kTextInsetX, lineTop + kLineTextDropY,
                                 glm::vec4(it->color, alpha)});
            continue;
        }
        // A line with web addresses: drawn in pieces, the links blue and underlined (and
        // clickable while the chat is open)
        float x = boxLeft + kTextInsetX;
        size_t from = 0;
        auto piece = [&](const std::string& text, const glm::vec4& color) {
            if (text.empty()) return 0.0f;
            textTasks.push_back({text, x, lineTop + kLineTextDropY, color});
            const float w = fontRenderer->textWidth(text, 1.0f, screen) / textScaleX;
            x += w;
            return w;
        };
        for (const auto& [start, length] : links) {
            piece(it->text.substr(from, start - from), glm::vec4(it->color, alpha));
            const std::string shownLink = it->text.substr(start, length);
            const std::string url = weblink::normalize(shownLink);
            const float x0 = x;
            const float w = fontRenderer->textWidth(shownLink, 1.0f, screen) / textScaleX;
            const bool hover = isOpen && m_pendingLink.empty() && mouseX >= x0 && mouseX < x0 + w &&
                               mouseY >= lineTop && mouseY < lineTop + kLinePixelH;
            piece(shownLink, hover ? glm::vec4(0.65f, 0.85f, 1.0f, alpha) : glm::vec4(0.35f, 0.65f, 1.0f, alpha));
            addRectTask(x0, lineTop + kLineTextDropY + 2.0f, w, hover ? 2.0f : 1.0f,
                        hover ? glm::vec4(0.65f, 0.85f, 1.0f, alpha) : glm::vec4(0.35f, 0.65f, 1.0f, alpha));
            if (isOpen) m_linkHits.push_back({x0, lineTop, x0 + w, lineTop + kLinePixelH, url});
            from = start + length;
        }
        piece(it->text.substr(from), glm::vec4(it->color, alpha));
    }

    // Save state we change so the rest of the frame is unaffected.
    const GLboolean depthWasOn = glIsEnabled(GL_DEPTH_TEST);
    const GLboolean cullWasOn  = glIsEnabled(GL_CULL_FACE);

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    const float scaleX = (W > 0.0f) ? static_cast<float>(fbWidth) / W : 1.0f;
    const float scaleY = (H > 0.0f) ? static_cast<float>(fbHeight) / H : 1.0f;

    // Background rectangles, then the text on them. FontRenderer takes pixel coordinates, top-left origin.
    auto drawPass = [&](const std::vector<RectTask>& rects, const std::vector<TextTask>& texts) {
        glDisable(GL_DEPTH_TEST);                     // (drawing text turns it back on)
        glDisable(GL_CULL_FACE);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glUseProgram(m_rectProgram);
        glBindVertexArray(m_rectVAO);
        glBindBuffer(GL_ARRAY_BUFFER, m_rectVBO);
        for (const auto& rect : rects) {
            const float v[36] = {
                rect.x0, rect.y0, rect.color.r, rect.color.g, rect.color.b, rect.color.a,
                rect.x1, rect.y0, rect.color.r, rect.color.g, rect.color.b, rect.color.a,
                rect.x1, rect.y1, rect.color.r, rect.color.g, rect.color.b, rect.color.a,
                rect.x0, rect.y0, rect.color.r, rect.color.g, rect.color.b, rect.color.a,
                rect.x1, rect.y1, rect.color.r, rect.color.g, rect.color.b, rect.color.a,
                rect.x0, rect.y1, rect.color.r, rect.color.g, rect.color.b, rect.color.a,
            };
            glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(v), v);
            glDrawArrays(GL_TRIANGLES, 0, 6);
        }
        glBindVertexArray(0);
        glUseProgram(fontShader);
        for (const auto& task : texts) {
            fontRenderer->renderText(fontShader, task.text, task.x * scaleX, task.y * scaleY,
                                     1.0f, 1.0f, task.color, screen);
        }
    };
    drawPass(rectTasks, textTasks);

    // "Open this link?" on top of everything
    if (isOpen && !m_pendingLink.empty()) {
        rectTasks.clear();
        textTasks.clear();
        auto width = [&](const std::string& t) { return fontRenderer->textWidth(t, 1.0f, screen) / textScaleX; };
        // long addresses: the middle left out, so the start (the site) and the end both show
        const float boxW = std::min(W - 40.0f, 760.0f);
        std::string shownUrl = m_pendingLink;
        if (width(shownUrl) > boxW - 40.0f) {
            const std::string tail = m_pendingLink.substr(m_pendingLink.size() - std::min<size_t>(24, m_pendingLink.size() / 2));
            std::string head = m_pendingLink.substr(0, m_pendingLink.size() - tail.size());
            while (head.size() > 10 && width(head + "..." + tail) > boxW - 40.0f) head.pop_back();
            shownUrl = head + "..." + tail;
        }
        const float boxH = 200.0f;
        const float bx = (W - boxW) * 0.5f, by = (H - boxH) * 0.5f;
        addRectTask(0.0f, 0.0f, W, H, glm::vec4(0.0f, 0.0f, 0.0f, 0.55f));
        addRectTask(bx, by, boxW, boxH, glm::vec4(0.08f, 0.08f, 0.1f, 0.95f));
        addRectTask(bx, by, boxW, 2.0f, glm::vec4(0.35f, 0.65f, 1.0f, 1.0f));
        auto centred = [&](const std::string& t, float y, const glm::vec4& c) {
            textTasks.push_back({t, bx + (boxW - width(t)) * 0.5f, y, c});
        };
        centred("Open this link in your browser?", by + 34.0f, glm::vec4(1.0f));
        centred("Website: " + weblink::host(m_pendingLink), by + 66.0f, glm::vec4(1.0f, 0.9f, 0.5f, 1.0f));
        centred(shownUrl, by + 94.0f, glm::vec4(0.35f, 0.65f, 1.0f, 1.0f));
        centred(m_linkNote.empty() ? "Only open links from people and servers you trust." : m_linkNote,
                by + 124.0f, glm::vec4(0.7f, 0.7f, 0.7f, 1.0f));
        const char* labels[3] = { "Open (Enter)", "Copy", "Cancel (Esc)" };
        const float bw = (boxW - 4.0f * 16.0f) / 3.0f, bh = 32.0f, btnY = by + boxH - bh - 16.0f;
        for (int i = 0; i < 3; ++i) {
            const float x0 = bx + 16.0f + static_cast<float>(i) * (bw + 16.0f);
            m_linkButtons[i] = {x0, btnY, x0 + bw, btnY + bh, ""};
            const bool hover = mouseX >= x0 && mouseX < x0 + bw && mouseY >= btnY && mouseY < btnY + bh;
            addRectTask(x0, btnY, bw, bh, hover ? glm::vec4(0.3f, 0.32f, 0.42f, 1.0f) : glm::vec4(0.2f, 0.2f, 0.25f, 1.0f));
            textTasks.push_back({labels[i], x0 + (bw - width(labels[i])) * 0.5f, btnY + 22.0f, glm::vec4(1.0f)});
        }
        drawPass(rectTasks, textTasks);
    }

    if (cullWasOn) glEnable(GL_CULL_FACE);
    if (depthWasOn) glEnable(GL_DEPTH_TEST);
}
