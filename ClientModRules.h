#pragma once
// What a server lets its players' client mods do (clientmods/ - a player's own scripts, like a
// minimap). The server decides; it's sent to players as the shared value "engine:client_mods",
// and each player's game holds its client mods to it. Like Luanti's CSM restrictions, this is
// kept by honest games: it can't stop a modified game from doing more.
//
//   server.conf:   client_mods = true
//                  client_mods_chat = false        may they send chat messages?
//                  client_mods_players = true      may they see where other players are?
//                  client_mods_block_range = 64    how far from the player they can read blocks
//                                                  (0 = any distance)
//                  client_mods_messages = true     may they send messages to the server's mods
//                                                  (client.send_to_server)?

#include <cctype>
#include <cstdlib>
#include <map>
#include <sstream>
#include <string>

struct ClientModRules {
    bool allowed = true;     // false = players' client mods don't run at all on this server
    bool chat = false;       // may send chat messages (and so commands) for the player
    bool players = true;     // may read other players' names and positions
    int blockRange = 64;     // blocks further than this from the player can't be read (0 = no limit)
    bool messages = true;    // may send messages to the server's mods (which check them, as any player's)

    static constexpr const char* kSharedKey = "engine:client_mods";

    // No limits at all (singleplayer, and the host of a LAN game)
    static ClientModRules unlimited() {
        ClientModRules r;
        r.chat = true;
        r.players = true;
        r.blockRange = 0;
        return r;
    }

    std::string encode() const {
        std::ostringstream o;
        o << "allowed=" << (allowed ? 1 : 0) << ";chat=" << (chat ? 1 : 0) << ";players=" << (players ? 1 : 0)
          << ";range=" << blockRange << ";msg=" << (messages ? 1 : 0);
        return o.str();
    }

    // Anything missing keeps its default (so older and newer games understand each other)
    static ClientModRules decode(const std::string& s) {
        ClientModRules r;
        std::istringstream in(s);
        std::string part;
        while (std::getline(in, part, ';')) {
            const auto eq = part.find('=');
            if (eq == std::string::npos) continue;
            const std::string k = part.substr(0, eq), v = part.substr(eq + 1);
            const int n = std::atoi(v.c_str());
            if (k == "allowed") r.allowed = n != 0;
            else if (k == "chat") r.chat = n != 0;
            else if (k == "players") r.players = n != 0;
            else if (k == "range") r.blockRange = n < 0 ? 0 : n;
            else if (k == "msg") r.messages = n != 0;
        }
        return r;
    }

    // From server.conf / settings.txt style keys, with a prefix ("client_mods" or "lan_client_mods")
    static ClientModRules fromConfig(const std::map<std::string, std::string>& kv, const std::string& prefix,
                                     const ClientModRules& defaults) {
        ClientModRules r = defaults;
        auto flag = [&](const std::string& key, bool& out) {
            const auto it = kv.find(key);
            if (it == kv.end()) return;
            std::string v = it->second;
            for (char& c : v) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            v.erase(0, v.find_first_not_of(" \t"));
            v.erase(v.find_last_not_of(" \t\r") + 1);
            if (v == "true" || v == "1" || v == "yes" || v == "on") out = true;
            else if (v == "false" || v == "0" || v == "no" || v == "off") out = false;
        };
        flag(prefix, r.allowed);
        flag(prefix + "_chat", r.chat);
        flag(prefix + "_players", r.players);
        flag(prefix + "_messages", r.messages);
        const auto it = kv.find(prefix + "_block_range");
        if (it != kv.end() && !it->second.empty()) {
            const int n = std::atoi(it->second.c_str());
            r.blockRange = n < 0 ? 0 : n;
        }
        return r;
    }
};
