// WebLink.cpp: see WebLink.h. Kept apart from the rest of the game because opening a browser
// needs <windows.h> on Windows, whose macros (near, far, min, max...) clash with game code.
#include "WebLink.h"

#include <cctype>
#include <cstring>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#else
#include <spawn.h>
#include <thread>
#include <sys/wait.h>
extern char** environ;
#endif

namespace weblink {

namespace {

bool startsWithNoCase(const std::string& s, size_t at, const char* prefix) {
    const size_t n = std::strlen(prefix);
    if (at + n > s.size()) return false;
    for (size_t i = 0; i < n; ++i) {
        if (std::tolower(static_cast<unsigned char>(s[at + i])) != prefix[i]) return false;
    }
    return true;
}

bool urlChar(unsigned char c) {
    if (std::isalnum(c)) return true;
    return std::strchr("-._~:/?#[]@!&()*+,;=%", c) != nullptr && c != 0;
}

// The scheme's length ("https://" = 8), or 0
size_t schemeLength(const std::string& url) {
    if (startsWithNoCase(url, 0, "https://")) return 8;
    if (startsWithNoCase(url, 0, "http://")) return 7;
    return 0;
}

} // namespace

std::vector<std::pair<size_t, size_t>> find(const std::string& text) {
    std::vector<std::pair<size_t, size_t>> out;
    size_t i = 0;
    while (i < text.size()) {
        // (only at the start of a word: not the "www.site.com" in "me@www.site.com")
        const bool wordStart = i == 0 || std::isspace(static_cast<unsigned char>(text[i - 1])) ||
                               std::strchr("([<{\"'", text[i - 1]) != nullptr;
        if (wordStart && (startsWithNoCase(text, i, "https://") || startsWithNoCase(text, i, "http://") ||
                          startsWithNoCase(text, i, "www."))) {
            size_t end = i;
            while (end < text.size() && urlChar(static_cast<unsigned char>(text[end]))) ++end;
            // Punctuation at the end belongs to the sentence, not the address (a ")" stays when
            // the link has its "(": wikipedia-style addresses)
            while (end > i) {
                const char c = text[end - 1];
                if (std::strchr(".,;:!?'\"]}", c)) { --end; continue; }
                if (c == ')') {
                    int open = 0, close = 0;
                    for (size_t k = i; k < end; ++k) { open += text[k] == '('; close += text[k] == ')'; }
                    if (close > open) { --end; continue; }
                }
                break;
            }
            const std::string candidate = text.substr(i, end - i);
            if (!normalize(candidate).empty()) {
                out.push_back({i, end - i});
                i = end;
                continue;
            }
            i = end > i ? end : i + 1;
            continue;
        }
        ++i;
    }
    return out;
}

bool isSafe(const std::string& url) {
    if (url.size() > 2000) return false;
    const size_t scheme = schemeLength(url);
    if (scheme == 0) return false;
    for (unsigned char c : url) if (!urlChar(c)) return false;
    // The host: up to the first / ? or #
    size_t hostEnd = url.find_first_of("/?#", scheme);
    if (hostEnd == std::string::npos) hostEnd = url.size();
    const std::string authority = url.substr(scheme, hostEnd - scheme);
    if (authority.empty() || authority.find('@') != std::string::npos) return false;   // "real.site@other.site"
    std::string hostPart = authority, port;
    const size_t colon = authority.find(':');
    if (colon != std::string::npos) { hostPart = authority.substr(0, colon); port = authority.substr(colon + 1); }
    if (hostPart.empty() || hostPart.size() > 253) return false;
    for (unsigned char c : hostPart) if (!std::isalnum(c) && c != '-' && c != '.') return false;
    if (hostPart.front() == '.' || hostPart.front() == '-' || hostPart.back() == '-') return false;
    if (hostPart.find("..") != std::string::npos) return false;
    if (colon != std::string::npos) {
        if (port.empty() || port.size() > 5) return false;
        for (unsigned char c : port) if (!std::isdigit(c)) return false;
        if (std::stoi(port) < 1 || std::stoi(port) > 65535) return false;
    }
    return true;
}

std::string normalize(const std::string& link) {
    std::string url = link;
    if (startsWithNoCase(url, 0, "www.")) url = "https://" + url;
    if (!isSafe(url)) return "";
    // A host needs a dot ("www.", "example.com"): "http://x" in a sentence isn't a site
    const size_t scheme = schemeLength(url);
    size_t hostEnd = url.find_first_of("/?#:", scheme);
    if (hostEnd == std::string::npos) hostEnd = url.size();
    const std::string h = url.substr(scheme, hostEnd - scheme);
    if (h.find('.') == std::string::npos && h != "localhost") return "";
    if (h.back() == '.') return "";
    return url;
}

std::string host(const std::string& url) {
    const size_t scheme = schemeLength(url);
    if (scheme == 0) return "";
    size_t end = url.find_first_of("/?#", scheme);
    if (end == std::string::npos) end = url.size();
    std::string h = url.substr(scheme, end - scheme);
    for (char& c : h) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return h;
}

bool open(const std::string& url) {
    if (!isSafe(url)) return false;
#if defined(_WIN32)
    const int n = MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, nullptr, 0);
    if (n <= 0) return false;
    std::wstring wide(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, &wide[0], n);
    const HINSTANCE r = ShellExecuteW(nullptr, L"open", wide.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    return reinterpret_cast<INT_PTR>(r) > 32;
#else
#if defined(__APPLE__)
    const char* program = "open";
#else
    const char* program = "xdg-open";
#endif
    // Started directly, with the address as one argument: nothing reads it as a command line
    std::string arg = url;
    char* argv[] = { const_cast<char*>(program), &arg[0], nullptr };
    pid_t pid = 0;
    if (posix_spawnp(&pid, program, nullptr, nullptr, argv, environ) != 0) return false;
    // Collect it when it's done (xdg-open returns quickly; the browser carries on by itself)
    std::thread([pid] { int status = 0; waitpid(pid, &status, 0); }).detach();
    return true;
#endif
}

} // namespace weblink
