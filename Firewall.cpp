// Lets other PCs on the network reach a game or server hosted here. Windows Firewall
// blocks incoming connections to new programs, which is why "the LAN list shows the
// game but joining fails". This asks Windows (one "allow" prompt) to add a rule for the
// program. Other systems: nothing to do.
//
// Kept in its own file: <windows.h> clashes with OpenGL's and the standard library's names.

#include <string>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>

// Adds an inbound "allow" rule for `program` (full path) on private and domain networks
// (home / work - not public Wi-Fi). Windows shows its admin prompt; returns false if the
// person said no or it couldn't start.
bool requestFirewallRule(const std::wstring& program, const std::wstring& ruleName) {
    // Remove an old rule with the same name first (the program may have moved), then add
    const std::wstring add = L"advfirewall firewall add rule name=\"" + ruleName +
        L"\" dir=in action=allow program=\"" + program + L"\" enable=yes profile=private,domain";
    const std::wstring script = L"/c netsh advfirewall firewall delete rule name=\"" + ruleName +
        L"\" >nul 2>&1 & netsh " + add;
    SHELLEXECUTEINFOW info{};
    info.cbSize = sizeof(info);
    info.fMask = SEE_MASK_NOCLOSEPROCESS;
    info.lpVerb = L"runas";               // the "allow changes?" prompt
    info.lpFile = L"cmd.exe";
    info.lpParameters = script.c_str();
    info.nShow = SW_HIDE;
    if (!ShellExecuteExW(&info)) return false;   // e.g. they clicked No
    if (info.hProcess) {
        WaitForSingleObject(info.hProcess, 10000);
        DWORD code = 1;
        GetExitCodeProcess(info.hProcess, &code);
        CloseHandle(info.hProcess);
        return code == 0;
    }
    return true;
}

std::wstring thisProgramPath() {
    wchar_t path[MAX_PATH * 2] = {};
    GetModuleFileNameW(nullptr, path, static_cast<DWORD>(sizeof(path) / sizeof(path[0])));
    return path;
}

bool allowThroughFirewall(const std::string& programUtf8, const std::string& ruleNameUtf8) {
    auto widen = [](const std::string& s) {
        if (s.empty()) return std::wstring();
        const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
        std::wstring w(n > 0 ? n - 1 : 0, L'\0');
        if (n > 1) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
        return w;
    };
    const std::wstring program = programUtf8.empty() ? thisProgramPath() : widen(programUtf8);
    return requestFirewallRule(program, widen(ruleNameUtf8));
}
#else
bool allowThroughFirewall(const std::string&, const std::string&) { return true; }
#endif
