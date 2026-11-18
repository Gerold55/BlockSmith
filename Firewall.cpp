// Lets other PCs on the network reach a game or server hosted here. Windows Firewall
// blocks incoming connections to new programs, which is why "the LAN list shows the
// game but joining fails". This asks Windows (one "allow" prompt) to add a rule for the
// program. Other systems: nothing to do.
//
// The game only ever changes rules it owns: rules it created are in its own firewall group
// (kOwnedGroup) with a unique internal name. Rules made by the player or other programs - even
// ones with the same display name - are never changed or removed. Updating is safe: the owned
// rule is changed in place, or a replacement is created first; obsolete owned rules are removed
// only after that succeeded, so a failed or cancelled update leaves the working rule as it was.
//
// Kept in its own file: <windows.h> clashes with OpenGL's and the standard library's names.

#include <cstdint>
#include <string>

namespace firewall {

// The group every rule the game creates is put in: how the game recognises its own rules
constexpr const wchar_t* kOwnedGroup = L"BlockSmith (managed by the game)";

// A value inside a PowerShell single-quoted string: ' is written as ''
inline std::wstring psQuote(const std::wstring& s) {
    std::wstring out = L"'";
    for (wchar_t c : s) {
        if (c == L'\'' || c == 0x2018 || c == 0x2019 || c == 0x201A || c == 0x201B) out += c;   // PowerShell treats curly quotes as quotes too
        out += c;
    }
    return out + L"'";
}

// The script run as administrator. Exits 0 only when an owned rule for `program` is in place.
inline std::wstring updateScript(const std::wstring& program, const std::wstring& ruleName) {
    return
        L"$ErrorActionPreference = 'Stop'\n"
        L"trap { exit 1 }\n"
        L"$grp  = " + psQuote(kOwnedGroup) + L"\n"
        L"$dn   = " + psQuote(ruleName) + L"\n"
        L"$prog = " + psQuote(program) + L"\n"
        // Only rules the game owns: in its group, with this display name
        L"$owned = @(Get-NetFirewallRule -Group $grp -ErrorAction SilentlyContinue | Where-Object { $_.DisplayName -eq $dn })\n"
        L"$kept = $null\n"
        // 1. Update an owned rule in place (a failed update leaves that rule as it was)
        L"foreach ($r in $owned) {\n"
        L"  try {\n"
        L"    Set-NetFirewallRule -Name $r.Name -Direction Inbound -Action Allow -Profile Private,Domain -Enabled True -Program $prog -ErrorAction Stop\n"
        L"    $kept = $r.Name; break\n"
        L"  } catch { }\n"
        L"}\n"
        // 2. None could be updated: install a replacement first (a failure stops here: nothing removed)
        L"if (-not $kept) {\n"
        L"  $kept = 'BlockSmith.' + [guid]::NewGuid().ToString('N')\n"
        L"  New-NetFirewallRule -Name $kept -DisplayName $dn -Group $grp -Direction Inbound -Action Allow "
        L"-Profile Private,Domain -Program $prog -Enabled True -ErrorAction Stop | Out-Null\n"
        L"}\n"
        // 3. Only now: the game's other (obsolete) rules of this name, by their exact internal names
        L"foreach ($r in $owned) { if ($r.Name -ne $kept) { Remove-NetFirewallRule -Name $r.Name -ErrorAction SilentlyContinue } }\n"
        L"exit 0\n";
}

// For powershell -EncodedCommand: base64 of the script as UTF-16LE (no quoting problems at all)
inline std::wstring encodeCommand(const std::wstring& script) {
    std::string bytes;
    for (wchar_t c : script) {
        uint32_t cp = static_cast<uint32_t>(c);
        if (cp > 0xFFFF) {                             // (wide chars wider than 16 bits: as a surrogate pair)
            cp -= 0x10000;
            const uint16_t hi = static_cast<uint16_t>(0xD800 + (cp >> 10)), lo = static_cast<uint16_t>(0xDC00 + (cp & 0x3FF));
            bytes += static_cast<char>(hi & 0xFF); bytes += static_cast<char>(hi >> 8);
            bytes += static_cast<char>(lo & 0xFF); bytes += static_cast<char>(lo >> 8);
        } else {
            bytes += static_cast<char>(cp & 0xFF); bytes += static_cast<char>((cp >> 8) & 0xFF);
        }
    }
    static const char* tbl = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::wstring out;
    size_t i = 0;
    for (; i + 2 < bytes.size(); i += 3) {
        const uint32_t v = (uint8_t(bytes[i]) << 16) | (uint8_t(bytes[i + 1]) << 8) | uint8_t(bytes[i + 2]);
        out += tbl[(v >> 18) & 63]; out += tbl[(v >> 12) & 63]; out += tbl[(v >> 6) & 63]; out += tbl[v & 63];
    }
    if (i < bytes.size()) {
        uint32_t v = uint8_t(bytes[i]) << 16;
        if (i + 1 < bytes.size()) v |= uint8_t(bytes[i + 1]) << 8;
        out += tbl[(v >> 18) & 63]; out += tbl[(v >> 12) & 63];
        out += (i + 1 < bytes.size()) ? tbl[(v >> 6) & 63] : '=';
        out += '=';
    }
    return out;
}

} // namespace firewall

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>

// Makes sure the game's own inbound "allow" rule for `program` (full path) exists on private
// and domain networks (home / work - not public Wi-Fi). Windows shows its admin prompt; returns
// false if the person said no or the update failed (any existing working rule is then kept).
bool requestFirewallRule(const std::wstring& program, const std::wstring& ruleName) {
    const std::wstring params = L"-NoProfile -NonInteractive -ExecutionPolicy Bypass -WindowStyle Hidden -EncodedCommand " +
                                firewall::encodeCommand(firewall::updateScript(program, ruleName));
    SHELLEXECUTEINFOW info{};
    info.cbSize = sizeof(info);
    info.fMask = SEE_MASK_NOCLOSEPROCESS;
    info.lpVerb = L"runas";               // the "allow changes?" prompt
    info.lpFile = L"powershell.exe";
    info.lpParameters = params.c_str();
    info.nShow = SW_HIDE;
    if (!ShellExecuteExW(&info)) return false;   // e.g. they clicked No
    if (info.hProcess) {
        const DWORD waited = WaitForSingleObject(info.hProcess, 30000);   // PowerShell can be slow to start
        DWORD code = 1;
        if (waited == WAIT_OBJECT_0) GetExitCodeProcess(info.hProcess, &code);
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
