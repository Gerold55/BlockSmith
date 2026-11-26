#pragma once
// Client mods: a player's own Lua scripts (the clientmods/ folder), like Luanti's client-side
// mods. They run only in that player's game, in their own Lua state, separate from the game's
// mods: a minimap, waypoints, a coordinates HUD, chat helpers...
//
//  - Sandboxed: no files (except their own folder at load time), no os.execute, no require.
//  - They can draw (ui.*), play sounds (sound.*), read the world around the player, read
//    chat, and add ".commands". Only what the server allows (ClientModRules.h): servers can
//    switch them off, stop them sending chat, hide other players, and limit how far they can
//    read blocks. In singleplayer and as a LAN host, there are no limits.
//  - Every callback is protected: an error switches that one callback off and says so in chat;
//    the game and other client mods carry on.
//
// main.cpp makes one (after the game's own Lua state), adds the engine functions that need the
// world (client.get_block ...) with addFunction-style access through api(), then load()s.

#include "ClientModRules.h"
#include <sol/sol.hpp>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

class ClientMods {
public:
    // Creates the sandboxed state and the core API (client.register_*, storage, dofile), and
    // gives it copies of the game's ui.* and sound.* (and a few read-only engine functions)
    // that call into `gameLua`. Call once, before load().
    void init(sol::state& gameLua, const std::string& clientModsFolder);
    // The `client` table, so the engine can add functions before load()
    sol::table api();
    sol::state& state() { return *m_lua; }
    bool ready() const { return m_lua != nullptr; }

    // Runs the mods switched on in clientmods/mods.conf (load_mod_<name> = true; client mods
    // are off until the player switches them on, on the Mods screen). Alphabetical, depends =
    // ... honoured. modFolders() = the folders of those that loaded (for sounds).
    void load();
    // Called by load() once the switched-on mods are known (modFolders() is filled) and before
    // any of their init.lua runs: the place to install their folders as sound search roots, so
    // sound.exists / sound.play work while the mods start up.
    std::function<void()> onModFoldersReady;

    // ---- The Mods screen ----
    struct Info {
        std::string name, title, author, description, version, depends, path, image;
        // image is the mod.conf path, relative to this mod folder.
        std::string status;          // "loaded", "error", "not_loaded" (switched on since startup), "disabled"
        std::string error;
        int luaFiles = 0;
        bool enabled = false;        // switched on in mods.conf (takes effect on restart)
        bool restartNeeded = false;  // switched on/off since the game started
    };
    // Every client mod in the folder (works even when client mods are switched off entirely)
    std::vector<Info> list() const;
    // Switches one on or off in clientmods/mods.conf (applies on restart)
    bool setEnabled(const std::string& name, bool on, std::string& error);
    // The folder (set before init, so the Mods screen can list client mods even when off)
    void setFolder(const std::string& folder) { m_folder = folder; }
    const std::string& folder() const { return m_folder; }
    const std::vector<std::string>& modFolders() const { return m_modFolders; }
    const std::vector<std::string>& modNames() const { return m_modNames; }

    // What the server allows right now (set by main.cpp every frame)
    void setRules(const ClientModRules& r) { m_rules = r; }
    const ClientModRules& rules() const { return m_rules; }
    bool active() const { return m_lua && m_rules.allowed && !m_modNames.empty(); }

    // ---- Called by the engine (each does nothing unless active()) ----
    // `where` = "singleplayer", "lan_host" or "server" ("" = not in a world); `address` = the
    // server's address, or the world's name when it's your own world
    void update(float dt, const std::string& where, const std::string& address);
    void key(int key, int action, int mods);
    void rightClickBlock(int x, int y, int z, const std::string& block);
    void blockDug(int x, int y, int z, const std::string& block);
    void chatMessage(const std::string& text);
    // Draws every client mod's HUD (call from inside the game's HUD drawing, like render_hud)
    void hud(float W, float H);
    // ".name args" typed in chat (without the dot) -> true if a client mod has that command
    bool command(const std::string& text);

    // A bare picture name ("map_frame.png") used by a client mod -> its full path ("" = not ours)
    std::string texturePath(const std::string& name) const;

    // Shows a line in chat (set by main.cpp)
    std::function<void(const std::string&, float, float, float)> showChat;
    // client.send_to_server(channel, data): to the server's mods (or this game's own, in
    // singleplayer), like engine.send_to_server. Set by the game.
    std::function<bool(const std::string&, const std::string&)> sendToServer;

    ~ClientMods();

private:
    void bridge(sol::table from, sol::table to, const std::string& fromName, const std::string& toName);
    void loadStorage(const std::string& mod);
    void saveStorage(const std::string& mod);

    std::unique_ptr<sol::state> m_lua;
    lua_State* m_game = nullptr;
    std::string m_folder;
    std::vector<std::string> m_modFolders, m_modNames;
    std::string m_loading;                                   // mod being loaded ("" after)
    std::map<std::string, std::string> m_loadErrors;         // name -> error, for the Mods screen
    std::map<std::string, bool> m_onAtStartup;               // name -> was switched on when loading
    std::map<std::string, std::string> m_modPaths;           // name -> folder
    std::map<std::string, std::string> m_textures;           // lower-case file name -> path
    std::map<std::string, std::map<std::string, std::string>> m_storage;
    ClientModRules m_rules = ClientModRules::unlimited();
    std::string m_where, m_address;                          // for join / leave
    bool m_inCallback = false;                               // no chat feedback loops
};
