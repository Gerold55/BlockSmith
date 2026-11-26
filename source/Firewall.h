#pragma once
// Windows Firewall: lets other PCs reach a game or server hosted here (Firewall.cpp).
#include <string>

enum class FirewallStatus {
    Idle,      // no update running for this rule (firewallStatus only)
    Allowed,   // the game's rule is in place
    Failed,    // declined, or the update failed (an existing working rule is kept)
    Pending,   // Windows is still applying it: ask firewallStatus later; no second update starts
};

// Asks Windows (one "allow" prompt) to let `program` ("" = this game) accept connections, under
// the game's own rule `ruleName`. If an update for this rule is still running, none is started:
// Pending is returned until it finishes.
FirewallStatus allowThroughFirewall(const std::string& program, const std::string& ruleName);
// The result of an update that was Pending: Pending while it still runs, then Allowed or Failed
// (once - after that, Idle).
FirewallStatus firewallStatus(const std::string& ruleName);
