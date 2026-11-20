#pragma once
// ConsoleInput: the dedicated server's console prompt.
//
//   Tab              completes the command or the word you're typing (press it again to list
//                    the choices when there are several)
//   Up / Down        earlier commands
//   Left / Right, Home / End, Backspace / Delete, Ctrl+A / Ctrl+E (start / end of the line),
//   Ctrl+U (clear the line), Ctrl+W (delete the word before the cursor), Ctrl+L (redraw)
//
// Everything the server prints (std::cout / std::cerr) appears above the line you're typing,
// which stays put at the bottom. When the input isn't a terminal (a pipe, a service, a
// systemd unit, a panel that sends lines), it falls back to reading plain lines, exactly as before.
//
//   console::LineEditor editor;
//   editor.start([](const std::string& line) { ... },           // a finished line (console thread)
//                [](const std::vector<std::string>& before,      // words before the one being typed
//                   const std::string& word) -> std::vector<std::string> { ... });   // candidates
//   ...
//   editor.stop();   // gives the terminal back as it was (also done on exit)

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <cstdio>
#include <functional>
#include <iostream>
#include <mutex>
#include <streambuf>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <io.h>
#else
#include <termios.h>
#include <unistd.h>
#endif

namespace console {

using LineFn = std::function<void(const std::string&)>;
using CompleteFn = std::function<std::vector<std::string>(const std::vector<std::string>&, const std::string&)>;

inline std::string lowerAscii(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// Does `word` start with `prefix`, ignoring case?
inline bool startsWithNoCase(const std::string& word, const std::string& prefix) {
    if (prefix.size() > word.size()) return false;
    return lowerAscii(word.substr(0, prefix.size())) == lowerAscii(prefix);
}

class LineEditor;

// std::cout / std::cerr while the editor runs: each finished line is printed above the prompt,
// then the prompt and what's been typed so far are drawn again
class PromptBuf : public std::streambuf {
public:
    PromptBuf(LineEditor& ed, std::streambuf* target) : ed_(ed), target_(target) {}
    std::streambuf* target() const { return target_; }
protected:
    int overflow(int ch) override;
    std::streamsize xsputn(const char* s, std::streamsize n) override;
    int sync() override;
private:
    LineEditor& ed_;
    std::streambuf* target_;
};

class LineEditor {
public:
    std::string prompt = "> ";

    ~LineEditor() { stop(); }

    // Starts reading the console on its own thread. Returns false (and reads plain lines
    // instead) when the input isn't an interactive terminal.
    bool start(LineFn onLine, CompleteFn complete) {
        onLine_ = std::move(onLine);
        complete_ = std::move(complete);
        interactive_ = enterRawMode();
        if (!interactive_) {
            std::thread([this]() {
                std::string line;
                while (running_ && std::getline(std::cin, line)) onLine_(line);
            }).detach();
            return false;
        }
        coutBuf_ = new PromptBuf(*this, std::cout.rdbuf());
        cerrBuf_ = new PromptBuf(*this, std::cerr.rdbuf());
        std::cout.rdbuf(coutBuf_);
        std::cerr.rdbuf(cerrBuf_);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            redraw();
        }
        std::thread([this]() { readLoop(); }).detach();
        return true;
    }

    // Puts the terminal and std::cout back as they were. Safe to call more than once.
    void stop() {
        running_ = false;
        if (!interactive_) return;
        interactive_ = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            clearLine();
            std::fflush(stdout);
        }
        if (coutBuf_) { std::cout.flush(); std::cout.rdbuf(coutBuf_->target()); }
        if (cerrBuf_) { std::cerr.flush(); std::cerr.rdbuf(cerrBuf_->target()); }
        leaveRawMode();
        // (the buffers are left alive: the input thread may still hold on to them for a moment)
    }

    // Called by PromptBuf: whole lines to print above the prompt
    void printAbove(const std::string& text) {
        std::lock_guard<std::mutex> lock(mutex_);
        clearLine();
        writeRaw(text);
        redraw();
    }

    std::mutex& outputMutex() { return mutex_; }

private:
    LineFn onLine_;
    CompleteFn complete_;
    std::atomic<bool> running_{true};
    bool interactive_ = false;
    PromptBuf* coutBuf_ = nullptr;
    PromptBuf* cerrBuf_ = nullptr;

    std::mutex mutex_;              // the line below, and the terminal
    std::string buf_;               // what's been typed (UTF-8)
    size_t pos_ = 0;                // cursor, a byte offset at a character boundary
    size_t shown_ = 0;              // characters on the screen line last time (to blank the rest)
    std::vector<std::string> history_;
    size_t histPos_ = 0;            // == history_.size(): the line being typed
    std::string draft_;             // the line being typed, while looking through the history
    bool lastWasTab_ = false;

#ifdef _WIN32
    HANDLE in_ = INVALID_HANDLE_VALUE;
    DWORD oldInMode_ = 0;
    UINT oldOutCp_ = 0;
#else
    termios old_{};
#endif

    // ---- terminal ----
    bool enterRawMode() {
#ifdef _WIN32
        in_ = GetStdHandle(STD_INPUT_HANDLE);
        DWORD mode = 0;
        if (in_ == INVALID_HANDLE_VALUE || !GetConsoleMode(in_, &mode)) return false;   // not a console
        oldInMode_ = mode;
        SetConsoleMode(in_, (mode & ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT)) | ENABLE_PROCESSED_INPUT);
        oldOutCp_ = GetConsoleOutputCP();
        SetConsoleOutputCP(CP_UTF8);
        restoreAtExit();
        return true;
#else
        if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) return false;
        if (tcgetattr(STDIN_FILENO, &old_) != 0) return false;
        termios raw = old_;
        raw.c_lflag &= ~static_cast<tcflag_t>(ICANON | ECHO);   // (ISIG stays: Ctrl+C still stops the server)
        raw.c_cc[VMIN] = 1;
        raw.c_cc[VTIME] = 0;
        if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) != 0) return false;
        restoreAtExit();
        return true;
#endif
    }

    // However the server ends, the terminal goes back to normal (the console's mode outlives the
    // server on Windows, and a shell left without echo is no fun)
    void restoreAtExit() {
        static LineEditor* self = nullptr;
        static bool registered = false;
        self = this;
        if (!registered) {
            registered = true;
            std::atexit([]() { if (self) self->stop(); });
        }
    }

    void leaveRawMode() {
#ifdef _WIN32
        if (in_ != INVALID_HANDLE_VALUE) SetConsoleMode(in_, oldInMode_);
        if (oldOutCp_) SetConsoleOutputCP(oldOutCp_);
#else
        tcsetattr(STDIN_FILENO, TCSANOW, &old_);
#endif
    }

    // Straight to the real stdout, past the prompt buffer
    void writeRaw(const std::string& s) {
        std::streambuf* out = coutBuf_ ? coutBuf_->target() : std::cout.rdbuf();
        out->sputn(s.data(), static_cast<std::streamsize>(s.size()));
        out->pubsync();
    }

    static size_t chars(const std::string& s, size_t from, size_t to) {    // UTF-8 characters
        size_t n = 0;
        for (size_t i = from; i < to && i < s.size(); ++i) if ((static_cast<unsigned char>(s[i]) & 0xC0) != 0x80) ++n;
        return n;
    }

    // Blanks the prompt line (with spaces, so it works on every terminal and console)
    void clearLine() {
        const size_t width = chars(prompt, 0, prompt.size()) + shown_;
        writeRaw("\r" + std::string(width, ' ') + "\r");
        shown_ = 0;
    }

    // Draws the prompt, the line and puts the cursor where it belongs
    void redraw() {
        const size_t total = chars(buf_, 0, buf_.size());
        std::string s = "\r" + prompt + buf_;
        if (shown_ > total) s += std::string(shown_ - total, ' ') + std::string(shown_ - total, '\b');
        s += std::string(chars(buf_, pos_, buf_.size()), '\b');
        shown_ = total;
        writeRaw(s);
    }

    // ---- editing ----
    size_t prevChar(size_t i) const {
        if (i == 0) return 0;
        do { --i; } while (i > 0 && (static_cast<unsigned char>(buf_[i]) & 0xC0) == 0x80);
        return i;
    }
    size_t nextChar(size_t i) const {
        if (i >= buf_.size()) return buf_.size();
        do { ++i; } while (i < buf_.size() && (static_cast<unsigned char>(buf_[i]) & 0xC0) == 0x80);
        return i;
    }

    void insert(const std::string& text) {
        buf_.insert(pos_, text);
        pos_ += text.size();
    }

    void submit() {
        const std::string line = buf_;
        buf_.clear();
        pos_ = 0;
        writeRaw("\r" + prompt + line + std::string(shown_ > chars(line, 0, line.size()) ? shown_ - chars(line, 0, line.size()) : 0, ' ') + "\n");
        shown_ = 0;
        if (!line.empty() && (history_.empty() || history_.back() != line)) {
            history_.push_back(line);
            if (history_.size() > 200) history_.erase(history_.begin());
        }
        histPos_ = history_.size();
        draft_.clear();
        redraw();
    }

    void historyMove(int dir) {
        if (history_.empty()) return;
        if (histPos_ == history_.size()) draft_ = buf_;
        if (dir < 0 && histPos_ > 0) --histPos_;
        else if (dir > 0 && histPos_ < history_.size()) ++histPos_;
        else return;
        buf_ = histPos_ == history_.size() ? draft_ : history_[histPos_];
        pos_ = buf_.size();
    }

    // Tab: complete the word before the cursor
    void completeWord() {
        if (!complete_) return;
        size_t start = buf_.rfind(' ', pos_ == 0 ? 0 : pos_ - 1);
        start = (start == std::string::npos || pos_ == 0) ? 0 : start + 1;
        if (pos_ > 0 && buf_[pos_ - 1] == ' ') start = pos_;
        const std::string word = buf_.substr(start, pos_ - start);
        std::vector<std::string> before;
        {
            std::string w;
            for (size_t i = 0; i < start; ++i) {
                if (buf_[i] == ' ') { if (!w.empty()) before.push_back(w); w.clear(); }
                else w += buf_[i];
            }
            if (!w.empty()) before.push_back(w);
        }
        std::vector<std::string> all = complete_(before, word);
        std::vector<std::string> matches;
        for (const auto& c : all) {
            if (!c.empty() && startsWithNoCase(c, word) &&
                std::find(matches.begin(), matches.end(), c) == matches.end()) matches.push_back(c);
        }
        std::sort(matches.begin(), matches.end(), [](const std::string& a, const std::string& b) {
            return lowerAscii(a) < lowerAscii(b);
        });
        if (matches.empty()) { writeRaw("\a"); lastWasTab_ = false; return; }

        auto replaceWord = [&](const std::string& with) {
            buf_.replace(start, pos_ - start, with);
            pos_ = start + with.size();
        };
        if (matches.size() == 1) {                                   // the one: complete it
            replaceWord(matches[0]);
            if (pos_ == buf_.size() || buf_[pos_] != ' ') insert(" ");
            else pos_ = nextChar(pos_);
            lastWasTab_ = false;
            return;
        }
        // Several: as far as they all agree (case from the first), then list them on a second Tab
        std::string common = matches[0];
        for (const auto& m : matches) {
            size_t n = 0;
            while (n < common.size() && n < m.size() &&
                   std::tolower(static_cast<unsigned char>(common[n])) == std::tolower(static_cast<unsigned char>(m[n]))) ++n;
            common.resize(n);
        }
        if (common.size() > word.size()) {
            replaceWord(common);
            lastWasTab_ = false;
            return;
        }
        if (!lastWasTab_) { writeRaw("\a"); lastWasTab_ = true; return; }
        // The list, in columns
        size_t widest = 0;
        for (const auto& m : matches) widest = std::max(widest, chars(m, 0, m.size()));
        const size_t colW = widest + 2;
        const size_t cols = std::max<size_t>(1, 78 / colW);
        std::string out = "\n";
        for (size_t i = 0; i < matches.size(); ++i) {
            out += matches[i];
            if ((i + 1) % cols == 0 || i + 1 == matches.size()) out += "\n";
            else out += std::string(colW - chars(matches[i], 0, matches[i].size()), ' ');
        }
        clearLine();
        writeRaw(prompt + buf_ + out);
        shown_ = 0;
        lastWasTab_ = false;
    }

    // One key; returns false at the end of input
    enum Key { K_NONE, K_CHAR, K_ENTER, K_TAB, K_BACKSPACE, K_DELETE, K_LEFT, K_RIGHT, K_UP, K_DOWN, K_HOME, K_END,
               K_CLEAR_LINE, K_DELETE_WORD, K_REDRAW, K_EOF };
    struct KeyPress { Key key = K_NONE; std::string text; };

#ifdef _WIN32
    bool readKey(KeyPress& k) {
        static wchar_t pendingHigh = 0;
        for (;;) {
            INPUT_RECORD rec;
            DWORD n = 0;
            if (!ReadConsoleInputW(in_, &rec, 1, &n) || n == 0) return false;
            if (rec.EventType != KEY_EVENT || !rec.Event.KeyEvent.bKeyDown) continue;
            const auto& e = rec.Event.KeyEvent;
            const bool ctrl = (e.dwControlKeyState & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) != 0;
            switch (e.wVirtualKeyCode) {
                case VK_RETURN: k.key = K_ENTER; return true;
                case VK_TAB: k.key = K_TAB; return true;
                case VK_BACK: k.key = K_BACKSPACE; return true;
                case VK_DELETE: k.key = K_DELETE; return true;
                case VK_LEFT: k.key = K_LEFT; return true;
                case VK_RIGHT: k.key = K_RIGHT; return true;
                case VK_UP: k.key = K_UP; return true;
                case VK_DOWN: k.key = K_DOWN; return true;
                case VK_HOME: k.key = K_HOME; return true;
                case VK_END: k.key = K_END; return true;
                case VK_ESCAPE: k.key = K_CLEAR_LINE; return true;
                default: break;
            }
            if (ctrl) {
                switch (e.wVirtualKeyCode) {
                    case 'A': k.key = K_HOME; return true;
                    case 'E': k.key = K_END; return true;
                    case 'U': k.key = K_CLEAR_LINE; return true;
                    case 'W': k.key = K_DELETE_WORD; return true;
                    case 'L': k.key = K_REDRAW; return true;
                    default: continue;
                }
            }
            const wchar_t ch = e.uChar.UnicodeChar;
            if (ch < 32) continue;
            std::wstring w;
            if (ch >= 0xD800 && ch <= 0xDBFF) { pendingHigh = ch; continue; }
            if (ch >= 0xDC00 && ch <= 0xDFFF && pendingHigh) { w += pendingHigh; pendingHigh = 0; }
            w += ch;
            char utf8[8];
            const int len = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), utf8, sizeof(utf8), nullptr, nullptr);
            if (len <= 0) continue;
            k.key = K_CHAR;
            k.text.assign(utf8, static_cast<size_t>(len));
            return true;
        }
    }
#else
    static bool readByte(unsigned char& c) {
        for (;;) {
            const ssize_t n = ::read(STDIN_FILENO, &c, 1);
            if (n == 1) return true;
            if (n < 0 && errno == EINTR) continue;
            return false;
        }
    }
    bool readKey(KeyPress& k) {
        unsigned char c;
        if (!readByte(c)) return false;
        switch (c) {
            case '\r': case '\n': k.key = K_ENTER; return true;
            case '\t': k.key = K_TAB; return true;
            case 127: case 8: k.key = K_BACKSPACE; return true;
            case 1: k.key = K_HOME; return true;          // Ctrl+A
            case 5: k.key = K_END; return true;           // Ctrl+E
            case 21: k.key = K_CLEAR_LINE; return true;   // Ctrl+U
            case 23: k.key = K_DELETE_WORD; return true;  // Ctrl+W
            case 12: k.key = K_REDRAW; return true;       // Ctrl+L
            case 4: k.key = K_EOF; return true;           // Ctrl+D
            case 2: k.key = K_LEFT; return true;          // Ctrl+B
            case 6: k.key = K_RIGHT; return true;         // Ctrl+F
            case 16: k.key = K_UP; return true;           // Ctrl+P
            case 14: k.key = K_DOWN; return true;         // Ctrl+N
            case 27: {                                    // an escape sequence: arrows, Home, End, Delete
                unsigned char a;
                if (!readByte(a)) return false;
                if (a != '[' && a != 'O') { k.key = K_NONE; return true; }
                std::string seq;
                unsigned char b;
                do {
                    if (!readByte(b)) return false;
                    seq += static_cast<char>(b);
                } while (!(b >= 0x40 && b <= 0x7E) && seq.size() < 8);
                const char last = seq.back();
                if (last == 'A') k.key = K_UP;
                else if (last == 'B') k.key = K_DOWN;
                else if (last == 'C') k.key = K_RIGHT;
                else if (last == 'D') k.key = K_LEFT;
                else if (last == 'H') k.key = K_HOME;
                else if (last == 'F') k.key = K_END;
                else if (last == '~') {
                    const std::string num = seq.substr(0, seq.size() - 1);
                    if (num == "1" || num == "7") k.key = K_HOME;
                    else if (num == "4" || num == "8") k.key = K_END;
                    else if (num == "3") k.key = K_DELETE;
                    else k.key = K_NONE;
                } else k.key = K_NONE;
                return true;
            }
            default: break;
        }
        if (c < 32) { k.key = K_NONE; return true; }
        k.key = K_CHAR;
        k.text.assign(1, static_cast<char>(c));
        // the rest of a UTF-8 character
        const int more = (c & 0xE0) == 0xC0 ? 1 : (c & 0xF0) == 0xE0 ? 2 : (c & 0xF8) == 0xF0 ? 3 : 0;
        for (int i = 0; i < more; ++i) {
            unsigned char d;
            if (!readByte(d)) return false;
            k.text += static_cast<char>(d);
        }
        return true;
    }
#endif

    void readLoop() {
        while (running_) {
            KeyPress k;
            if (!readKey(k)) break;
            std::string finished;
            bool haveLine = false;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (k.key != K_TAB) lastWasTab_ = false;
                switch (k.key) {
                    case K_CHAR: insert(k.text); break;
                    case K_ENTER: finished = buf_; haveLine = true; submit(); break;
                    case K_TAB: completeWord(); break;
                    case K_BACKSPACE:
                        if (pos_ > 0) { const size_t p = prevChar(pos_); buf_.erase(p, pos_ - p); pos_ = p; }
                        break;
                    case K_DELETE:
                        if (pos_ < buf_.size()) buf_.erase(pos_, nextChar(pos_) - pos_);
                        break;
                    case K_LEFT: pos_ = prevChar(pos_); break;
                    case K_RIGHT: pos_ = nextChar(pos_); break;
                    case K_UP: historyMove(-1); break;
                    case K_DOWN: historyMove(1); break;
                    case K_HOME: pos_ = 0; break;
                    case K_END: pos_ = buf_.size(); break;
                    case K_CLEAR_LINE: buf_.clear(); pos_ = 0; break;
                    case K_DELETE_WORD: {
                        size_t p = pos_;
                        while (p > 0 && buf_[p - 1] == ' ') --p;
                        while (p > 0 && buf_[p - 1] != ' ') --p;
                        buf_.erase(p, pos_ - p);
                        pos_ = p;
                        break;
                    }
                    case K_REDRAW: clearLine(); break;
                    case K_EOF:
                        if (buf_.empty()) { running_ = false; }
                        else if (pos_ < buf_.size()) buf_.erase(pos_, nextChar(pos_) - pos_);
                        break;
                    case K_NONE: break;
                }
                if (k.key != K_ENTER) redraw();
            }
            if (haveLine && onLine_) onLine_(finished);
        }
    }

    friend class PromptBuf;
    std::string pendingOut_;        // printed text not yet ending in a newline (PromptBuf)
};

inline int PromptBuf::overflow(int ch) {
    if (ch == traits_type::eof()) return traits_type::not_eof(ch);
    const char c = static_cast<char>(ch);
    xsputn(&c, 1);
    return ch;
}

inline std::streamsize PromptBuf::xsputn(const char* s, std::streamsize n) {
    std::string lines;
    {
        std::lock_guard<std::mutex> lock(ed_.outputMutex());
        ed_.pendingOut_.append(s, static_cast<size_t>(n));
        const size_t nl = ed_.pendingOut_.rfind('\n');
        if (nl == std::string::npos) return n;
        lines = ed_.pendingOut_.substr(0, nl + 1);
        ed_.pendingOut_.erase(0, nl + 1);
    }
    ed_.printAbove(lines);
    return n;
}

inline int PromptBuf::sync() { return 0; }   // (whole lines are written as they end)

} // namespace console
