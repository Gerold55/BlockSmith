#pragma once
// Multiplayer networking (ENet, UDP).
//
//  GameServer - runs inside the host's game ("Host Game" in the pause menu). The host's
//               World is the real one: it loads chunks around every player, streams them
//               to clients, checks and applies their block edits and sends out every change.
//  GameClient - used after joining from the Multiplayer screen. Its World is in remote mode
//               and only holds what the server sent.
//
// Neither class touches the window or OpenGL, so the same server code can later run in a
// dedicated (windowless) server.

#include <glm/glm.hpp>
#include "DayTime.h"
#include <cstdint>
#include <deque>
#include <algorithm>
#include <functional>
#include <string>
#include <utility>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "World.h"
#include "PlayerMovement.h"
#include "Entities.h"

typedef struct _ENetHost ENetHost;
typedef struct _ENetPeer ENetPeer;
typedef struct _ENetPacket ENetPacket;

namespace net {

constexpr uint16_t kDefaultPort      = 30000;
constexpr uint32_t kProtocolVersion  = 8;   // 7: the server decides each player's game mode
                                            // 8: passwords by per-server challenge-response (Auth.h)
constexpr int      kMaxClients       = 64;
constexpr int      kMaxViewDistance  = 10;   // chunks the server will send around a player

// Another player as this game sees them (positions are smoothed between updates).
// Another player as this game sees them. Drawn 100 ms in the past, smoothly between the
// two snapshots around that moment (entity interpolation), so they never stutter or guess.
struct PlayerView {
    struct Snapshot {
        double time;          // server clock
        glm::vec3 position;
        float yaw, pitch;
    };
    std::string name;
    glm::vec3 position{0.0f};     // where to draw them this frame
    float yaw = 0.0f, pitch = 0.0f;
    std::deque<Snapshot> snapshots;   // oldest first
    double lastUpdate = 0.0;      // when the last snapshot arrived (local steady clock)
    // For /netstats: position updates received per second
    int updatesCounted = 0;
    double rateWindowStart = 0.0;
    float updateRate = 0.0f;
    glm::vec3 color{1.0f};
    bool hasPosition = false;
    int ping = -1;                // ms, from the server every couple of seconds (-1 = not known yet)
};
using PlayerMap = std::unordered_map<uint16_t, PlayerView>;

// A line to show in chat, with its colour.
struct ChatLine {
    std::string text;
    glm::vec3 color{1.0f};
};

// Seconds since the program started (used as the time stamp in position updates).
float clockSeconds();

// Shown in the server list (and checked against the server's version).
constexpr const char* kVersionName = "BlockSmith 0.1";   // the engine
// ENet "connect data" meaning "just tell me your status, I'm not joining".
constexpr uint32_t kStatusQuery = 0x53544154;   // "STAT"
constexpr int kMaxMotdLength = 200;       // the description may contain {colour} tags
constexpr size_t kMaxIconBytes = 64 * 1024;   // server_icon.png (64x64 recommended)
constexpr uint16_t kLanPort = 30001;          // LAN games announce themselves on this UDP port

// What the game stores instead of the player's password (a SHA-256 of it, in settings.txt).
// It never leaves the player's PC: logins prove knowledge of it with a per-server
// challenge-response (Auth.h), so no server receives anything it could replay elsewhere.
std::string passwordToken(const std::string& password);

// What a joining player proved, handed to GameServer::canJoin (see Auth.h)
struct AuthProof {
    std::string serverId;     // this server's identity, as sent in the challenge
    std::string nonce;        // the challenge (fresh, single use)
    std::string response;     // HMAC answer; "" = the player has no password
    std::string registerKey;  // this server's key for the player: sent only when the server said
                              // it had no account for the name ("" otherwise)
    bool hasPassword() const { return !response.empty(); }
    // Does `response` answer the challenge for this stored per-server key?
    bool matches(const std::string& storedKey, const std::string& name) const;
};

// "host", "host:port" -> host + port (kDefaultPort if none). False if empty/invalid.
bool parseAddress(const std::string& text, std::string& host, uint16_t& port);

// Player names: 1-16 letters, digits, '_' or '-'.
bool validPlayerName(const std::string& name);

} // namespace net

// ===========================================================================
// Server
// ===========================================================================
class GameServer {
public:
    GameServer() = default;
    ~GameServer();
    GameServer(const GameServer&) = delete;
    GameServer& operator=(const GameServer&) = delete;

    // Starts listening on `port`. `world` must stay alive until stop().
    // hasHostPlayer = false for a dedicated (headless) server: nobody is "the host", and
    // hostName is only the server's display name (e.g. on the LAN list).
    bool start(World& world, uint16_t port, const std::string& gameFolder,
               const std::string& hostName, std::string& error, bool hasHostPlayer = true);
    // Kicks everyone with `reason` and closes the port.
    void stop(const std::string& reason = "The host closed the server");

    bool running() const { return m_host != nullptr; }
    uint16_t port() const { return m_port; }
    int playerCount() const;   // including the host

    // Call every frame while hosting (also while paused or loading).
    void update(const glm::vec3& hostPos, float hostYaw, float hostPitch, float dt);

    // Other players, for drawing (the host itself isn't in here).
    const net::PlayerMap& players() const { return m_views; }

    // Lines for /netstats: each player's ping and position updates per second.
    std::vector<std::string> statsLines() const;

    // What the server list shows (the owner sets these in Settings > Server).
    // Can be changed while running; maxPlayers counts the host too.
    void setInfo(const std::string& motd, int maxPlayers);
    // PNG bytes shown next to the server in other players' lists (empty = none).
    void setIcon(std::vector<uint8_t> png);
    const std::string& motd() const { return m_motd; }
    int maxPlayers() const { return m_maxPlayers; }

    // Chat from the host to everyone.
    void sendChat(const std::string& text);
    // A line to everyone as-is (server messages, "[Server] ...").
    void broadcastMessage(const std::string& text, const glm::vec3& color = glm::vec3(1.0f, 1.0f, 0.4f));

    // Dedicated server helpers
    std::vector<std::string> playerNames() const;                              // joined players
    // Moves a player (respawn, /tp). Their game follows on the next position update.
    bool teleportPlayer(const std::string& name, const glm::vec3& pos);
    std::vector<std::pair<std::string, glm::vec3>> playerPositions() const;   // for saving
    bool kickPlayer(const std::string& name, const std::string& reason);
    // A joined player's IP address ("" if they aren't here) - for ban-ip
    std::string playerAddress(const std::string& name) const;
    // Asks a player's game to do something it decides itself: "give" (item, count), "clear",
    // "kill" (reason). Their game runs it (GameClient::takeActions; mods: on_server_action).
    bool sendAction(const std::string& name, const std::string& action,
                    const std::string& a = "", const std::string& b = "");
    // Asked first when someone joins, with their IP address: "" lets them in, else why not (ban-ip)
    std::function<std::string(const std::string& ip)> canJoinFrom;
    // Where a returning player should appear (return true + set pos), and a hook for
    // when a player leaves (to save their position). Optional.
    std::function<bool(const std::string& name, glm::vec3& pos)> spawnFor;
    std::function<void(const std::string& name, const glm::vec3& pos)> onLeave;
    // Asked before someone joins, after the password challenge: return "" to let them in, or
    // the reason they can't (banned, not on the whitelist, wrong password...). `proof` holds
    // their answer to this connection's challenge (check it with proof.matches(storedKey, name))
    // and, for a new account, the per-server key to store. Optional.
    std::function<std::string(const std::string& name, const net::AuthProof& proof)> canJoin;
    // Whether the server already has an account (a password) for this name: told to the
    // player in the challenge, so their game only sends a registration key when needed.
    std::function<bool(const std::string& name)> hasAccount;
    // This server's identity, bound into every password response (Auth.h). Set it before
    // start; it should stay the same across restarts (dedicated: server_id.txt).
    void setServerId(const std::string& id) { m_serverId = id; }
    const std::string& serverId() const { return m_serverId; }
    // A "/command" a player typed (without the slash). Reply with messagePlayer. Optional.
    std::function<void(const std::string& name, const std::string& command)> onCommand;
    // Tab in a player's chat: the text before their cursor ("/give Al" or "hello Bo") -> what
    // fits the word they're typing (the server's commands they may use, players, items...)
    std::function<std::vector<std::string>(const std::string& name, const std::string& text)> onComplete;
    // Called once someone has joined (e.g. to send them a welcome message). Optional.
    std::function<void(const std::string& name)> onJoin;
    // A player changed a block (and the server accepted it): for break/place sounds and mods
    std::function<void(const std::string& name, const glm::ivec3& pos, uint8_t oldId, uint8_t newId)> onPlayerEdit;
    // Before a player's block change is applied: "" = allowed, anything else = refused (the
    // player's game is sent the real block back). The text is shown to the player, except
    // kSilentRefusal. Optional. (Mods: can_edit_block)
    std::function<std::string(const std::string& name, const glm::ivec3& pos, uint8_t oldId, uint8_t newId)> canEdit;
    static constexpr const char* kSilentRefusal = "\x01";
    // A player's chat line, before everyone sees it: return false to drop it; `text` may be
    // changed. Optional. (Mods: on_player_chat)
    std::function<bool(const std::string& name, std::string& text)> onChat;
    // Chat and /commands from one player: a burst of kChatBurst, then one every
    // kChatRefillSeconds. More is dropped; kSpamKickStrikes drops in a row gets them kicked.
    static constexpr double kChatBurst = 6.0;
    static constexpr double kChatRefillSeconds = 0.75;
    static constexpr int kSpamKickStrikes = 25;
    // A chat line to one player only. False if they're not online.
    // ---- Game modes and operators: the server decides. Each player's game follows what it's
    //      told and won't let the player change it themselves (only the server/admins can). ----
    // Mode for a player as they join ("" = the default mode)
    std::function<std::string(const std::string& name)> modeFor;
    // Whether a player is an operator (told to their game, e.g. to show admin buttons)
    std::function<bool(const std::string& name)> isOperator;
    void setDefaultMode(const std::string& mode) { if (!mode.empty()) m_defaultMode = mode; }
    const std::string& defaultMode() const { return m_defaultMode; }
    // Changes a joined player's mode ("survival", "creative", or any mode a game defines)
    bool setPlayerMode(const std::string& name, const std::string& mode);
    std::string playerMode(const std::string& name) const;   // "" if not online
    bool setPlayerOperator(const std::string& name, bool op);

    bool messagePlayer(const std::string& name, const std::string& text,
                       const glm::vec3& color = glm::vec3(1.0f, 1.0f, 0.4f));
    // The furthest any player may see, in chunks (they can choose less).
    void setMaxViewDistance(int chunks) { m_maxView = std::clamp(chunks, 2, net::kMaxViewDistance); }
    // The game's title from its game.conf, shown in server lists ("Plays Skyblock").
    void setGameTitle(const std::string& title) { m_gameTitle = title; }
    const std::string& game() const { return m_game; }
    // Lines the host should see in chat (joins, leaves, other players' messages).
    std::vector<net::ChatLine> takeChat();

    // Shared values: small key -> text pairs the server decides and every player gets (now
    // and when they join), e.g. the weather. Lua: engine.set_shared / get_shared.
    void setShared(const std::string& key, const std::string& value);
    const std::unordered_map<std::string, std::string>& shared() const { return m_shared; }

    // Entities (mobs, dropped items): the owner's EntityManager. Players get what's near them:
    // what each one is and looks like (when it appears or changes) and where it is, 20 times a
    // second. Their games only draw them; nothing else of the entity is sent.
    void setEntities(EntityManager* entities) { m_entityManager = entities; }
    // A player hit (kind 0) or right-clicked (kind 1) an entity, holding `item`. Already checked:
    // it exists and is within their reach, and they aren't clicking faster than 10 times a second.
    std::function<void(const std::string& name, uint32_t id, int kind, const std::string& item, const glm::vec3& playerPos)> onEntityAct;
    // A player's game dropped items (a block they dug, or thrown out): checked to be next to them
    // How it was dropped: `returned` = it didn't fit in their inventory (picked up again a little
    // later); hasVelocity = thrown (Q, a death scatter) with `velocity`; pickupDelay < 0 = usual.
    struct DropInfo { bool returned = false; bool hasVelocity = false; glm::vec3 velocity{0.0f}; float pickupDelay = -1.0f; };
    std::function<void(const std::string& name, const std::string& item, int count, const glm::vec3& pos, const DropInfo& how)> onDropItem;

    // The time of day (DayTime.h). The owner (the dedicated server, or the game when hosting)
    // calls this every tick with its clock; players get it when they join, every few seconds,
    // and at once when it jumps (a "time set") or its speed changes.
    void setDayTime(const DayClock& clock);

private:
    void sendDayTime(ENetPeer* peer);
    DayClock m_dayClock;          // the owner's clock, as of the last setDayTime
    DayClock m_daySent;           // ...as last sent to everyone, and when
    double m_daySentAt = 0.0;
    bool m_hasDayClock = false;
    float m_dayTimer = 0.0f;
    void sendShared(ENetPeer* peer, const std::vector<std::pair<std::string, std::string>>& values);
    struct Client {
        ENetPeer* peer = nullptr;
        double completeWindow = 0.0;   // (completion requests: at most 20 a second)
        int completeCount = 0;
        uint16_t id = 0;
        bool joined = false;
        // Between C_HELLO and C_AUTH: the challenge we sent and what they asked for
        bool challenged = false;
        std::string nonce;
        std::string helloName;
        int helloView = 8;
        std::string mode;             // game mode the server gave them
        bool op = false;
        std::string name;
        int viewDistance = 8;
        glm::vec3 position{0.0f};     // = sim.position (kept for reach checks, chunk loading...)
        float yaw = 0.0f, pitch = 0.0f;
        bool hasPosition = false;
        MoveState sim;                // the server's own copy of this player's movement
        uint32_t lastSeq = 0;         // last input applied (sent back so the client can reconcile)
        float moveBudget = 0.2f;      // seconds of movement they may still send (stops speed hacks)
        // Timeline of their movement: each input moves simClock on by its dt. simOffset turns
        // that into server time, so snapshots are evenly spaced even when inputs arrive in bursts.
        double simClock = 0.0;
        double simOffset = 0.0;
        bool simOffsetSet = false;
        double lastSnapTime = 0.0;    // server time of their newest snapshot
        // For F3 / netstats
        int inputsThisSecond = 0, inputsPerSecond = 0;
        int droppedInputs = 0;        // refused because they sent more movement than time allowed
        // Chat flood limit (kChatBurst...): tokens left, when they were last topped up, strikes
        double chatTokens = kChatBurst;
        double chatRefillAt = 0.0;
        int spamStrikes = 0;
        double lastSpamWarning = -100.0;
        float movedThisSecond = 0.0f, speed = 0.0f;   // horizontal blocks/second on the server
        double statsWindow = 0.0;
        std::unordered_set<World::ChunkCoord, PairHash> sentChunks;
        // Entities this player's game knows about: what we last told it
        struct KnownEntity { uint32_t version = 0; glm::vec3 pos{0.0f}; float yaw = 0.0f; };
        std::unordered_map<uint32_t, KnownEntity> knownEntities;
        double entityActWindow = 0.0;     // (hits / right-clicks / drops: at most 10 / 30 a second)
        int entityActs = 0, drops = 0;
    };
    void sendEntities();
    EntityManager* m_entityManager = nullptr;
    float m_entityTimer = 0.0f;

    void handlePacket(Client& c, const uint8_t* data, size_t size);
    void sendStatus(ENetPeer* peer);
    void handleHello(Client& c, const uint8_t* data, size_t size);
    void handleAuth(Client& c, const uint8_t* data, size_t size);
    void finishJoin(Client& c, const std::string& name, int view);
    void handleBlocks(Client& c, const uint8_t* data, size_t size);
    void handleInputs(Client& c, const uint8_t* data, size_t size);
    void kick(Client& c, const std::string& reason);
    void sendMessageTo(Client& c, const std::string& text, const glm::vec3& color);
    static std::string cleanChatText(const std::string& in);
    bool allowChat(Client& c);
    void dropClient(uint16_t id);
    void sendMode(Client& c);

    void streamChunks(Client& c);
    void sendBlockChanges(const std::vector<World::BlockChange>& changes);
    void sendFallingStarts(const std::vector<World::FallingStart>& starts);
    void sendPositions();
    void broadcastChat(const std::string& text, const glm::vec3& color);

    Client* findByPeer(ENetPeer* peer);

    ENetHost* m_host = nullptr;
    World* m_world = nullptr;
    uint16_t m_port = 0;
    std::string m_game;
    std::string m_hostName;
    bool m_hasHost = true;
    int m_maxView = net::kMaxViewDistance;
    std::string m_gameTitle;
    std::string m_motd = "A BlockSmith server";
    int m_maxPlayers = 20;
    std::vector<uint8_t> m_icon;
    uint16_t m_nextId = 1;
    // LAN announcements ("Scanning for games on your local network" on other PCs)
    void announceLan();
    intptr_t m_lanSocket = -1;   // ENetSocket, -1 = none
    float m_lanTimer = 0.0f;           // 0 is the host
    std::unordered_map<uint16_t, Client> m_clients;
    net::PlayerMap m_views;
    std::vector<net::ChatLine> m_chat;
    std::unordered_map<std::string, std::string> m_shared;
    std::string m_defaultMode = "survival";
    std::string m_serverId;       // this server's identity for password challenges (setServerId)

    glm::vec3 m_hostPos{0.0f};
    float m_hostYaw = 0.0f, m_hostPitch = 0.0f;
    float m_posTimer = 0.0f;
    float m_pingTimer = 0.0f;
};

// ===========================================================================
// Client
// ===========================================================================
class GameClient {
public:
    enum class State { Idle, Connecting, Joined };

    struct Welcome {
        uint16_t playerId = 0;
        uint32_t seed = 0;
        glm::vec3 spawn{0.0f};
        std::string hostName;
    };

    GameClient() = default;
    ~GameClient();
    GameClient(const GameClient&) = delete;
    GameClient& operator=(const GameClient&) = delete;

    // Starts connecting (returns straight away; watch takeWelcome / takeDisconnect).
    // When the server accepts, `world` is reset and switched to remote mode.
    // passwordToken: net::passwordToken(password) from the player's settings, or "". It is
    // never sent: the game answers the server's challenge with it (Auth.h).
    // knownServerId: the identity this address had last time ("" = first visit). If the server
    // now claims a different one, the game refuses to answer with the password.
    bool connect(World& world, const std::string& address, const std::string& playerName,
                 const std::string& gameFolder, int viewDistance, std::string& error,
                 const std::string& passwordToken = "", const std::string& knownServerId = "");
    // The identity of the server we joined (to remember it for next time), "" before the challenge
    const std::string& serverId() const { return m_serverId; }
    // Leaves the server (no-op when not connected).
    void disconnect();

    State state() const { return m_state; }
    bool active() const { return m_state != State::Idle; }
    const std::string& address() const { return m_addressText; }

    // Call every frame while connecting or joined (before the player moves). `local` is
    // the player's current movement state, used to check the server's corrections against.
    void update(const MoveState& local, float yaw, float pitch, float dt);

    // Client-side prediction: call right after moving the player with this input, so the
    // server can apply the same input. Inputs are numbered and kept until the server confirms.
    void recordInput(const MoveInput& input, float dt);
    // True when the server's position for us disagreed with ours: `out` is the corrected
    // state (server's answer with our newer inputs replayed on top). Apply it to the player.
    bool takeCorrection(MoveState& out);

    // True once, when the server has accepted us.
    bool takeWelcome(Welcome& out);
    // True once, when the connection failed or ended (reason says why).
    bool takeDisconnect(std::string& reason);

    const net::PlayerMap& players() const { return m_views; }

    // Your ping to the server in ms (-1 when not connected), and your player id there.
    int ping() const;
    uint16_t playerId() const { return m_welcome.playerId; }

    // Lines for /netstats: ping to the server and updates per second from each player.
    std::vector<std::string> statsLines() const;

    void sendChat(const std::string& text);
    // A "/command" (without the slash) for the server to run; it answers in chat.
    void sendCommand(const std::string& command);
    // Asks the server what fits the word being typed in chat (Tab). The answer comes later:
    // completionFor(text, out) has it once it's here; takeCompletionArrived() is true once then.
    void requestCompletion(const std::string& text);
    bool completionFor(const std::string& text, std::vector<std::string>& out) const {
        if (text != m_completeText || !m_completeHave) return false;
        out = m_completeList;
        return true;
    }
    bool takeCompletionArrived() { const bool a = m_completeArrived; m_completeArrived = false; return a; }
    std::vector<net::ChatLine> takeChat();

    // Shared values from the server (see GameServer::setShared)
    std::string getShared(const std::string& key) const {
        const auto it = m_shared.find(key);
        return it == m_shared.end() ? std::string() : it->second;
    }
    // Keys that changed since the last call (in order)
    std::vector<std::string> takeSharedChanges() { std::vector<std::string> out; out.swap(m_sharedChanges); return out; }
    // What the server asked our game to do (GameServer::sendAction), oldest first
    struct Action { std::string action, a, b; };
    std::vector<Action> takeActions() { std::vector<Action> out; out.swap(m_actions); return out; }
    // Game mode the server gave us ("" = not told yet / not online) and whether we're an operator
    const std::string& gameMode() const { return m_mode; }
    bool isOperator() const { return m_isOp; }
    // True once each time the server changes our mode or operator status
    bool takeModeChange() { const bool c = m_modeChanged; m_modeChanged = false; return c; }
    // The server's time of day, once each time it arrives (older servers never send one: then
    // the game keeps its own clock)
    bool takeDayTime(DayClock& out) { if (!m_dayTimeNew) return false; out = m_dayTime; m_dayTimeNew = false; return true; }
    // Entities near us (mobs, dropped items), where they are this frame (drawn a little in the
    // past, smoothly, like other players)
    const std::unordered_map<uint32_t, EntityView>& entities() const { return m_entityViews; }
    // Hit (kind 0) or right-click (kind 1) an entity, holding `item`; the server decides what happens
    void sendEntityAct(uint32_t id, int kind, const std::string& item);
    // Drop items into the world next to us (what we dug, or threw out); the server makes them
    // returned = it was given to us but didn't fit; velocity (thrown) and pickupDelay (< 0 = usual)
    void sendDropItem(const std::string& item, int count, const glm::vec3& pos, bool returned = false,
                      const glm::vec3* velocity = nullptr, float pickupDelay = -1.0f);

    // A block changed by the server (another player, water...): for break/place sounds
    std::function<void(const glm::ivec3& pos, uint8_t oldId, uint8_t newId)> onBlockChanged;

private:
    void handlePacket(const uint8_t* data, size_t size);
    void finish(const std::string& reason);
    void sendEdits();

    std::vector<std::string> m_sharedChanges;
    std::vector<Action> m_actions;
    std::string m_completeText, m_completeAsked;   // what the last answer / question was for
    std::vector<std::string> m_completeList;
    bool m_completeHave = false, m_completeArrived = false;
    DayClock m_dayTime;
    bool m_dayTimeNew = false;
    std::string m_mode;
    bool m_isOp = false;
    bool m_modeChanged = false;
    ENetHost* m_host = nullptr;
    ENetPeer* m_peer = nullptr;
    World* m_world = nullptr;
    State m_state = State::Idle;
    std::string m_addressText;
    std::string m_name;
    std::string m_password;   // token, not the password (never sent)
    std::string m_knownServerId, m_serverId;
    std::string m_game;
    int m_viewDistance = 8;
    double m_connectStarted = 0.0;

    uint8_t m_fromServer[256] = {};   // server block id -> our id
    uint8_t m_toServer[256] = {};     // our block id -> server block id
    bool m_haveWelcome = false;
    Welcome m_welcome;
    bool m_ended = false;
    std::string m_endReason;
    std::string m_kickReason;

    net::PlayerMap m_views;
    std::vector<net::ChatLine> m_chat;
    std::unordered_map<std::string, std::string> m_shared;
    float m_posTimer = 0.0f;

    struct EntitySnap { double time; glm::vec3 pos; float yaw; };
    std::unordered_map<uint32_t, EntityView> m_entityViews;
    std::unordered_map<uint32_t, std::deque<EntitySnap>> m_entitySnaps;
    void interpolateEntities(double renderTime);
    void syncClock(float serverTime);

    // Prediction & reconciliation
    struct PendingInput { uint32_t seq; MoveInput input; float dt; };
    void sendInputs();
    void reconcile(uint32_t ackSeq, const MoveState& server);
    std::deque<PendingInput> m_pending;   // sent but not yet confirmed by the server
    uint32_t m_nextSeq = 1;
    uint32_t m_lastAck = 0;
    MoveState m_local;
    MoveState m_correction;
    bool m_haveCorrection = false;
    int m_corrections = 0;                // for /netstats

    // Server clock estimate (for interpolating other players)
    double m_clockOffset = 0.0;           // server clock - our clock
    double m_jitter = 0.0;                // how late updates arrive, recently (seconds)
    double m_interpDelay = 0.1;           // how far in the past other players are drawn
    bool m_clockSynced = false;
};

// ===========================================================================
// Server list status ("is it online, what's it about, who's on")
// ===========================================================================
struct ServerStatus {
    enum class State { Pinging, Online, Offline } state = State::Pinging;
    std::string motd;
    std::string version;         // the server's kVersionName
    std::string game;            // game folder the server is playing
    std::string gameTitle;       // that game's title (from its game.conf), "" from older servers
    int players = 0, maxPlayers = 0;
    std::vector<std::string> names;   // up to 20 player names (hover in the list)
    std::vector<uint8_t> icon;        // PNG bytes (empty = no icon)
    int ping = -1;               // ms
    bool compatible = true;      // same protocol version
    std::string error;           // why it's offline
};

class ServerPinger {
public:
    ~ServerPinger();
    // Starts (or restarts) checking `address`. Results appear in status(address).
    void ping(const std::string& address);
    // Call every frame while the server list is open.
    void update();
    // nullptr if this address was never pinged.
    const ServerStatus* status(const std::string& address) const;
    void clear();

private:
    struct Query {
        ENetHost* host = nullptr;
        ENetPeer* peer = nullptr;
        double started = 0.0;
        double connectedAt = 0.0;
        ServerStatus result;
    };
    void finish(Query& q);
    std::unordered_map<std::string, Query> m_queries;
};

// ===========================================================================
// LAN games: hosts broadcast a small "I'm here" message every 1.5 s on kLanPort;
// the Multiplayer screen listens and lists them.
// ===========================================================================
struct LanGame {
    std::string address;    // "192.168.1.20:30000"
    std::string hostName;
    std::string motd;
    int players = 0, maxPlayers = 0;
    double lastSeen = 0.0;
};

class LanScanner {
public:
    ~LanScanner();
    void start();    // opens the listening socket (call when the Multiplayer screen opens)
    void stop();
    void update();   // reads announcements, forgets games not heard from for 5 s
    bool running() const { return m_socket != -1; }
    std::vector<LanGame> games() const;
private:
    intptr_t m_socket = -1;
    std::unordered_map<std::string, LanGame> m_games;
};
