// enet includes <windows.h> on Windows; keep it from defining min/max/near/far macros.
#ifdef _WIN32
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#endif
#include <enet/enet.h>

#include "Net.h"
#include "Sha256.h"
#include "Auth.h"
#include "ChunkData.h"
#include "Block.h"
#include "BlockDefs.h"
#include "Types.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <memory>

// ===========================================================================
// Protocol
//
// Every packet starts with a u8 message type. Integers are little-endian, floats are
// IEEE-754 32-bit, strings are u16 length + bytes. Blocks travel by NAME (not id), so
// the server and clients don't need identical block ids - only the same mods.
//
// Channel 0 (reliable, in order): everything except positions.
// Channel 1 (unreliable, sequenced): player positions.
// ===========================================================================
namespace {

enum Msg : uint8_t {
    // client -> server
    C_HELLO   = 1,    // u32 protocol, str name, str game, u8 viewDistance, u8 hasPassword
                      // (no secret: the server answers with S_CHALLENGE)
    C_AUTH    = 6,    // str response, str registerKey   (answer to S_CHALLENGE; see Auth.h)
    C_INPUTS  = 2,    // u32 firstSeq, u16 count, { u8 keys, f32 yaw, f32 pitch, f32 dt }
    C_BLOCKS  = 3,    // u16 count, { i32 x y z, str block, u8 meta }
    C_CHAT    = 4,    // str text
    C_COMMAND = 5,    // str command (no slash) - run by the server, answered with S_CHAT

    // server -> client
    S_WELCOME = 64,   // u16 yourId, u32 seed, f32 spawn x y z, str hostName
    S_KICK    = 65,   // str reason
    S_CHUNK   = 66,   // i32 cx cz, rest = chunk bytes (World::encodeChunk)
    S_UNLOAD  = 67,   // i32 cx cz
    S_BLOCKS  = 68,   // u16 count, { i32 x y z, str block, u8 meta }
    S_ADD     = 69,   // u16 id, str name
    S_REMOVE  = 70,   // u16 id
    S_PLAYERS = 71,   // f32 serverTime, u16 count, { u16 id, f32 time, f32 x y z yaw pitch }
    S_CHAT    = 72,   // str text, f32 r g b
    S_PINGS   = 73,   // u16 count, { u16 id, u16 ping ms }   (for the player list)
    S_STATUS  = 74,   // u32 protocol, str version, str game, str motd, u16 online, u16 max,
                      // u16 nameCount, { str name }    (server list; sent instead of joining)
    S_SHARED  = 76,   // u16 count, { str key, str value }   shared values (weather...)
    S_MODE    = 77,   // str gameMode, u8 isOperator   (the server decides; the game follows)
    S_CHALLENGE = 78, // str serverId, str nonce, u8 accountExists   (after C_HELLO; see Auth.h)
    S_TIME    = 79,   // u32 wholeDays, f32 timeOfDay (0..1), f32 secondsPerDay, f32 speed  (DayTime.h)
                      // (older games ignore it and keep their own sky; no protocol change needed)
    S_ACK     = 75,   // u32 lastInputSeq, f32 x y z, f32 vx vy vz, u8 grounded  (your real position)
};

constexpr int   kChunksPerUpdate     = 4;            // per client per frame
constexpr float kDayTimeInterval     = 5.0f;         // the time of day goes to everyone this often
constexpr size_t kMaxChunkBytesInFlight = 512 * 1024; // don't queue more than this per client
constexpr float kPositionInterval    = 1.0f / 30.0f; // 30 position updates a second
constexpr float kMaxReach            = 10.0f;        // blocks a player may edit from
constexpr double kInterpDelay        = 0.1;          // other players are drawn this far in the past (minimum)
constexpr double kMaxInterpDelay     = 0.35;         // ... and at most this far, on very jittery Wi-Fi
constexpr float  kMaxMoveBudget      = 2.0f;         // seconds of movement a player may be owed
constexpr size_t kMaxInputsPerPacket = 100;
constexpr double kConnectTimeoutSec  = 10.0;

const glm::vec3 kInfoColor(1.0f, 1.0f, 0.4f);
const glm::vec3 kChatColor(1.0f, 1.0f, 1.0f);

using Clock = std::chrono::steady_clock;
double nowSeconds() {
    return std::chrono::duration<double>(Clock::now().time_since_epoch()).count();
}

inline int floorDiv(int a, int b) {
    int q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0))) --q;
    return q;
}
inline int positiveMod(int a, int b) { int m = a % b; return m < 0 ? m + b : m; }

World::ChunkCoord chunkOf(const glm::vec3& p) {
    return {floorDiv(static_cast<int>(std::floor(p.x)), CHUNK_WIDTH),
            floorDiv(static_cast<int>(std::floor(p.z)), CHUNK_WIDTH)};
}

// --- Writing / reading packets ---------------------------------------------
struct Writer {
    std::vector<uint8_t> d;
    explicit Writer(uint8_t type) { d.push_back(type); }
    void u8(uint8_t v) { d.push_back(v); }
    void u16(uint16_t v) { d.push_back(v & 0xFF); d.push_back(v >> 8); }
    void u32(uint32_t v) { for (int i = 0; i < 4; ++i) d.push_back((v >> (8 * i)) & 0xFF); }
    void i32(int32_t v) { u32(static_cast<uint32_t>(v)); }
    void f32(float v) { uint32_t u; std::memcpy(&u, &v, 4); u32(u); }
    void str(const std::string& s) {
        const size_t n = std::min<size_t>(s.size(), 0xFFFF);
        u16(static_cast<uint16_t>(n));
        d.insert(d.end(), s.begin(), s.begin() + static_cast<std::ptrdiff_t>(n));
    }
    void bytes(const std::vector<uint8_t>& b) { d.insert(d.end(), b.begin(), b.end()); }
};

struct Reader {
    const uint8_t* d; size_t n; size_t p = 1; bool ok = true;   // p = 1 skips the type byte
    Reader(const uint8_t* data, size_t size) : d(data), n(size) {}
    bool need(size_t k) { if (!ok || p + k > n) { ok = false; return false; } return true; }
    uint8_t u8() { if (!need(1)) return 0; return d[p++]; }
    uint16_t u16() { if (!need(2)) return 0; uint16_t v = d[p] | (d[p + 1] << 8); p += 2; return v; }
    uint32_t u32() {
        if (!need(4)) return 0;
        uint32_t v = 0;
        for (int i = 0; i < 4; ++i) v |= static_cast<uint32_t>(d[p + i]) << (8 * i);
        p += 4;
        return v;
    }
    int32_t i32() { return static_cast<int32_t>(u32()); }
    float f32() { uint32_t u = u32(); float v; std::memcpy(&v, &u, 4); return std::isfinite(v) ? v : 0.0f; }
    std::string str() {
        const uint16_t len = u16();
        if (!need(len)) return {};
        std::string s(reinterpret_cast<const char*>(d + p), len);
        p += len;
        return s;
    }
    std::vector<uint8_t> rest() { std::vector<uint8_t> v(d + p, d + n); p = n; return v; }
    // Read in order. (Never write glm::vec3(r.f32(), r.f32(), r.f32()) - C++ doesn't say which
    // argument is evaluated first, and GCC goes right-to-left, which would swap x and z.)
    glm::vec3 vec3f() { const float x = f32(); const float y = f32(); const float z = f32(); return {x, y, z}; }
    glm::ivec3 ivec3() { const int x = i32(); const int y = i32(); const int z = i32(); return {x, y, z}; }
};

void sendTo(ENetPeer* peer, const Writer& w, bool reliable = true) {
    if (!peer) return;
    ENetPacket* pkt = enet_packet_create(w.d.data(), w.d.size(),
                                         reliable ? ENET_PACKET_FLAG_RELIABLE : 0);
    enet_peer_send(peer, reliable ? 0 : 1, pkt);
}

std::string blockName(uint8_t id) {
    return id == 0 ? std::string() : BlockRegistry::getInstance().getBlock(id).name;
}

// "" = air. Returns false for a block this game doesn't have.
bool blockIdFromName(const std::string& name, uint8_t& id) {
    if (name.empty()) { id = 0; return true; }
    id = BlockRegistry::getInstance().getBlockId(name);
    return id != 0;
}

// Same colour for the same name everywhere.
glm::vec3 colorForName(const std::string& name) {
    uint32_t h = 2166136261u;
    for (unsigned char ch : name) { h ^= ch; h *= 16777619u; }
    const float hue = static_cast<float>(h % 360) / 60.0f;
    const float x = 1.0f - std::fabs(std::fmod(hue, 2.0f) - 1.0f);
    glm::vec3 c;
    if (hue < 1) c = {1, x, 0}; else if (hue < 2) c = {x, 1, 0}; else if (hue < 3) c = {0, 1, x};
    else if (hue < 4) c = {0, x, 1}; else if (hue < 5) c = {x, 0, 1}; else c = {1, 0, x};
    return c * 0.65f + glm::vec3(0.2f);
}

// Adds a snapshot of another player (time on the server's clock).
void pushSnapshot(net::PlayerView& v, double time, const glm::vec3& p, float yaw, float pitch) {
    if (!v.snapshots.empty() && time <= v.snapshots.back().time) return;   // old / duplicate
    v.snapshots.push_back({time, p, yaw, pitch});
    while (v.snapshots.size() > 512) v.snapshots.pop_front();
    if (!v.hasPosition) {
        v.position = p;
        v.yaw = yaw;
        v.pitch = pitch;
        v.hasPosition = true;
    }
    const double now = nowSeconds();
    v.lastUpdate = now;
    // Updates per second, for /netstats
    ++v.updatesCounted;
    if (now - v.rateWindowStart >= 1.0) {
        v.updateRate = static_cast<float>(v.updatesCounted / (now - v.rateWindowStart));
        v.updatesCounted = 0;
        v.rateWindowStart = now;
    }
}

float lerpAngle(float a, float b, float t) {
    const float d = std::fmod(b - a + 540.0f, 360.0f) - 180.0f;   // shortest way round
    return a + d * t;
}

// Entity interpolation: place every player where they were at `renderTime` (server clock),
// between the two snapshots around it. If the newest snapshot is late, carry on briefly
// in the direction they were going, then wait.
void interpolatePlayers(net::PlayerMap& views, double renderTime) {
    for (auto& [id, v] : views) {
        auto& snaps = v.snapshots;
        if (snaps.empty()) continue;
        // Forget snapshots we'll never need again (keep one before renderTime)
        while (snaps.size() > 2 && snaps[1].time < renderTime - 0.5) snaps.pop_front();

        if (renderTime <= snaps.front().time || snaps.size() == 1) {
            const auto& s = (renderTime <= snaps.front().time) ? snaps.front() : snaps.back();
            v.position = s.position; v.yaw = s.yaw; v.pitch = s.pitch;
            continue;
        }
        if (renderTime >= snaps.back().time) {
            // Past the newest: extrapolate up to 0.1 s from the last two, then hold still
            const auto& b = snaps.back();
            const auto& a = snaps[snaps.size() - 2];
            const double span = b.time - a.time;
            const double ahead = std::min(renderTime - b.time, 0.1);
            glm::vec3 p = b.position;
            if (span > 1e-4 && glm::distance(a.position, b.position) < 8.0f) {
                p += (b.position - a.position) * static_cast<float>(ahead / span);
            }
            v.position = p; v.yaw = b.yaw; v.pitch = b.pitch;
            continue;
        }
        size_t i = 1;
        while (i < snaps.size() && snaps[i].time < renderTime) ++i;
        const auto& a = snaps[i - 1];
        const auto& b = snaps[i];
        const float t = static_cast<float>((renderTime - a.time) / std::max(b.time - a.time, 1e-6));
        if (glm::distance(a.position, b.position) > 8.0f) {
            v.position = (t < 0.5f) ? a.position : b.position;   // teleported: don't slide across the map
        } else {
            v.position = glm::mix(a.position, b.position, t);
        }
        v.yaw = lerpAngle(a.yaw, b.yaw, t);
        v.pitch = a.pitch + (b.pitch - a.pitch) * t;
    }
}

// ENet slows down ("throttles") unreliable packets when the connection looks busy -
// e.g. while chunks are streaming - and silently drops positions, which makes other
// players stutter. Positions are tiny, so turn that off: always send them all.
void disableThrottle(ENetPeer* peer) {
    enet_peer_throttle_configure(peer, ENET_PEER_PACKET_THROTTLE_INTERVAL, 0, 0);
}

// enet_initialize once for the whole program.
bool ensureEnet() {
    static bool ok = false, tried = false;
    if (!tried) {
        tried = true;
        ok = enet_initialize() == 0;
        if (ok) std::atexit(enet_deinitialize);
        else std::cerr << "[Net] Could not start networking (enet_initialize failed)" << std::endl;
    }
    return ok;
}

// Chunk offsets sorted nearest-first, so chunk streaming sends close chunks first.
const std::vector<std::pair<int, int>>& ringOffsets() {
    static std::vector<std::pair<int, int>> offsets = [] {
        std::vector<std::pair<int, int>> v;
        const int r = net::kMaxViewDistance;
        for (int dx = -r; dx <= r; ++dx)
            for (int dz = -r; dz <= r; ++dz) v.push_back({dx, dz});
        std::sort(v.begin(), v.end(), [](const auto& a, const auto& b) {
            return a.first * a.first + a.second * a.second < b.first * b.first + b.second * b.second;
        });
        return v;
    }();
    return offsets;
}

} // namespace

// ===========================================================================
// Helpers
// ===========================================================================
float net::clockSeconds() {
    static const double start = nowSeconds();
    return static_cast<float>(nowSeconds() - start);
}

std::string net::passwordToken(const std::string& password) {
    if (password.empty()) return "";
    return sha256Hex("craft-and-ruin-password:" + password);   // stays on the player's PC
}

bool net::AuthProof::matches(const std::string& storedKey, const std::string& name) const {
    return auth::verify(storedKey, serverId, nonce, name, response);
}

bool net::parseAddress(const std::string& text, std::string& host, uint16_t& port) {
    std::string s = text;
    s.erase(0, s.find_first_not_of(" \t"));
    s.erase(s.find_last_not_of(" \t") + 1);
    if (s.empty()) return false;

    port = kDefaultPort;
    const size_t colon = s.rfind(':');
    if (colon != std::string::npos) {
        const std::string portText = s.substr(colon + 1);
        s = s.substr(0, colon);
        if (portText.empty() || portText.size() > 5 ||
            portText.find_first_not_of("0123456789") != std::string::npos) return false;
        const int p = std::stoi(portText);
        if (p <= 0 || p > 65535) return false;
        port = static_cast<uint16_t>(p);
    }
    host = s;
    return !host.empty();
}

bool net::validPlayerName(const std::string& name) {
    if (name.empty() || name.size() > 16) return false;
    for (char ch : name) {
        if (!(std::isalnum(static_cast<unsigned char>(ch)) || ch == '_' || ch == '-')) return false;
    }
    return true;
}

// ===========================================================================
// GameServer
// ===========================================================================
// At program exit the World may already be gone, so only close the socket here
// (call stop() first for a clean shutdown).
GameServer::~GameServer() {
    if (m_host) enet_host_destroy(m_host);
    if (m_lanSocket != -1) enet_socket_destroy(static_cast<ENetSocket>(m_lanSocket));
}

bool GameServer::start(World& world, uint16_t port, const std::string& gameFolder,
                       const std::string& hostName, std::string& error, bool hasHostPlayer) {
    if (running()) { error = "Already hosting"; return false; }
    if (!ensureEnet()) { error = "Networking could not be started"; return false; }

    ENetAddress addr;
    addr.host = ENET_HOST_ANY;
    addr.port = port;
    // Room for max players plus a few extra connections (status checks, people being
    // told the server is full). ENet allows at most 4095.
    const size_t peers = static_cast<size_t>(std::clamp(m_maxPlayers + 16, 32, 4095));
    m_host = enet_host_create(&addr, peers, 2, 0, 0);
    if (!m_host) {
        error = "Port " + std::to_string(port) + " is already in use";
        return false;
    }

    m_world = &world;
    m_port = port;
    m_game = gameFolder;
    m_hostName = hostName;
    m_hasHost = hasHostPlayer;
    m_nextId = 1;
    m_clients.clear();
    m_views.clear();
    m_chat.clear();
    m_shared.clear();
    m_world->setRecordChanges(true);
    std::cout << "[Server] Hosting on port " << port << std::endl;
    return true;
}

void GameServer::stop(const std::string& reason) {
    if (!m_host) return;
    for (auto& [id, c] : m_clients) {
        Writer w(S_KICK);
        w.str(reason);
        sendTo(c.peer, w);
        enet_peer_disconnect_later(c.peer, 0);
    }
    // Give the goodbye packets a moment to leave
    enet_host_flush(m_host);
    ENetEvent ev;
    const double until = nowSeconds() + 0.3;
    while (!m_clients.empty() && nowSeconds() < until && enet_host_service(m_host, &ev, 20) >= 0) {
        if (ev.type == ENET_EVENT_TYPE_RECEIVE) enet_packet_destroy(ev.packet);
        if (ev.type == ENET_EVENT_TYPE_DISCONNECT) {
            if (Client* c = findByPeer(ev.peer)) m_clients.erase(c->id);
        }
    }
    enet_host_destroy(m_host);
    m_host = nullptr;
    m_clients.clear();
    m_views.clear();
    if (m_lanSocket != -1) {
        enet_socket_destroy(static_cast<ENetSocket>(m_lanSocket));
        m_lanSocket = -1;
    }
    if (m_world) {
        m_world->setRecordChanges(false);
        m_world->setExtraLoadCenters({});
    }
    m_world = nullptr;
    std::cout << "[Server] Stopped" << std::endl;
}

int GameServer::playerCount() const {
    int n = m_hasHost ? 1 : 0;
    for (const auto& [id, c] : m_clients) if (c.joined) ++n;
    return n;
}

GameServer::Client* GameServer::findByPeer(ENetPeer* peer) {
    const uint16_t id = static_cast<uint16_t>(reinterpret_cast<uintptr_t>(peer->data));
    auto it = m_clients.find(id);
    return (it != m_clients.end() && it->second.peer == peer) ? &it->second : nullptr;
}

void GameServer::update(const glm::vec3& hostPos, float hostYaw, float hostPitch, float dt) {
    if (!m_host) return;
    m_hostPos = hostPos;
    m_hostYaw = hostYaw;
    m_hostPitch = hostPitch;

    // 1. Network events
    ENetEvent ev;
    while (m_host && enet_host_service(m_host, &ev, 0) > 0) {
        switch (ev.type) {
        case ENET_EVENT_TYPE_CONNECT: {
            if (ev.data == net::kStatusQuery) {
                // Server list check: answer and hang up (not a player)
                ev.peer->data = nullptr;
                sendStatus(ev.peer);
                enet_peer_disconnect_later(ev.peer, 0);
                break;
            }
            const uint16_t id = m_nextId++;
            if (m_nextId == 0) m_nextId = 1;
            Client c;
            c.peer = ev.peer;
            c.id = id;
            ev.peer->data = reinterpret_cast<void*>(static_cast<uintptr_t>(id));
            disableThrottle(ev.peer);
            m_clients[id] = std::move(c);
            break;
        }
        case ENET_EVENT_TYPE_RECEIVE:
            if (Client* c = findByPeer(ev.peer)) handlePacket(*c, ev.packet->data, ev.packet->dataLength);
            enet_packet_destroy(ev.packet);
            break;
        case ENET_EVENT_TYPE_DISCONNECT:
            if (Client* c = findByPeer(ev.peer)) dropClient(c->id);
            break;
        default:
            break;
        }
    }

    // 2. Keep chunks loaded around every joined player
    std::vector<World::LoadCenter> centers;
    for (const auto& [id, c] : m_clients) {
        if (c.joined && c.hasPosition) centers.push_back({c.position, c.viewDistance});
    }
    m_world->setExtraLoadCenters(centers);

    // 3. Block changes (player edits, water, mods) -> everyone who has that chunk
    sendBlockChanges(m_world->takeBlockChanges());

    // 4. Chunks
    for (auto& [id, c] : m_clients) {
        if (c.joined && c.hasPosition) streamChunks(c);
    }

    // The time of day, every few seconds
    if (m_hasDayClock) {
        m_dayTimer += dt;
        if (m_dayTimer >= kDayTimeInterval) {
            m_dayTimer = 0.0f;
            for (auto& [id, c] : m_clients) if (c.joined) sendDayTime(c.peer);
            m_daySent = m_dayClock;
            m_daySentAt = net::clockSeconds();
        }
    }

    // 5. Positions, 20 times a second
    m_posTimer += dt;
    if (m_posTimer >= kPositionInterval) {
        m_posTimer = std::fmod(m_posTimer, kPositionInterval);
        sendPositions();
    }

    // Let LAN players see this game, every 1.5 seconds
    m_lanTimer -= dt;
    if (m_lanTimer <= 0.0f) {
        m_lanTimer = 1.5f;
        announceLan();
    }

    // Pings for the player list (Tab), every 2 seconds
    m_pingTimer += dt;
    if (m_pingTimer >= 2.0f) {
        m_pingTimer = 0.0f;
        Writer w(S_PINGS);
        uint16_t n = 0;
        w.u16(0);
        if (m_hasHost) { w.u16(0); w.u16(0); ++n; }   // the host
        for (const auto& [id, c] : m_clients) {
            if (!c.joined) continue;
            const int ping = static_cast<int>(std::min<enet_uint32>(c.peer->roundTripTime, 65535));
            w.u16(id); w.u16(static_cast<uint16_t>(ping)); ++n;
            auto it = m_views.find(id);
            if (it != m_views.end()) it->second.ping = ping;
        }
        w.d[1] = n & 0xFF;
        w.d[2] = n >> 8;
        for (auto& [id, c] : m_clients) {
            if (c.joined) sendTo(c.peer, w);
        }
    }

    // Host view of the other players: a snapshot of the server's simulation every frame,
    // drawn 100 ms behind so bursts of inputs arriving together still look smooth.
    for (auto& [id, c] : m_clients) {
        // Up to 2 s of movement can be owed: after a Wi-Fi stall the queued movement is caught
        // up instead of thrown away (which snapped the player back up to 10 blocks). Over
        // any longer stretch nobody can move faster than real time.
        if (c.joined) c.moveBudget = std::min(c.moveBudget + dt, kMaxMoveBudget);
    }
    interpolatePlayers(m_views, net::clockSeconds() - kInterpDelay);
    enet_host_flush(m_host);
}

// Movement inputs from a client: run each one through the same movement code the client
// used, in order. Inputs already applied are skipped (the client resends unconfirmed ones).
void GameServer::handleInputs(Client& c, const uint8_t* data, size_t size) {
    Reader r(data, size);
    const uint32_t first = r.u32();
    const uint16_t count = r.u16();
    if (!r.ok || count > 400) return;
    for (uint16_t i = 0; i < count; ++i) {
        MoveInput in;
        in.keys = r.u8();
        if (c.mode != "creative") in.keys &= static_cast<uint8_t>(~MoveInput::Fly);   // flying: creative only
        in.yaw = r.f32();
        in.pitch = std::clamp(r.f32(), -90.0f, 90.0f);
        float dt = r.f32();
        if (!r.ok) break;
        const uint32_t seq = first + i;
        if (seq <= c.lastSeq) continue;
        dt = std::clamp(dt, 0.0f, kMaxMoveStep);
        // Only as much movement as real time allows (a speed hack would run out of budget)
        const glm::vec3 before = c.sim.position;
        if (c.moveBudget + 0.001f >= dt) {
            c.moveBudget -= dt;
            stepMovement(c.sim, in, dt, *m_world);
        } else {
            ++c.droppedInputs;
        }
        ++c.inputsThisSecond;
        c.movedThisSecond += glm::length(glm::vec2(c.sim.position.x - before.x, c.sim.position.z - before.z));
        c.lastSeq = seq;
        c.yaw = in.yaw;
        c.pitch = in.pitch;

        // One snapshot per input, spaced by the input's own dt
        c.simClock += dt;
        if (!c.simOffsetSet) {
            c.simOffset = net::clockSeconds() - c.simClock;
            c.simOffsetSet = true;
        }
        const double t = c.simClock + c.simOffset;
        pushSnapshot(m_views[c.id], t, c.sim.position, c.yaw, c.pitch);
        c.lastSnapTime = t;
    }
    // Keep the timeline in step with real time, gently: a sudden jump would bunch up or
    // stretch the snapshots and the player would visibly speed up or slow down.
    const double sample = net::clockSeconds() - c.simClock;
    c.simOffset += (sample - c.simOffset) * 0.05;
    c.position = c.sim.position;
    c.hasPosition = true;

    const double now = nowSeconds();
    if (now - c.statsWindow >= 1.0) {
        const float span = static_cast<float>(now - c.statsWindow);
        c.inputsPerSecond = static_cast<int>(c.inputsThisSecond / span);
        c.speed = c.movedThisSecond / span;
        c.inputsThisSecond = 0;
        c.movedThisSecond = 0.0f;
        c.statsWindow = now;
    }
}

void GameServer::handlePacket(Client& c, const uint8_t* data, size_t size) {
    if (size == 0) return;
    const uint8_t type = data[0];

    if (!c.joined) {
        if (type == C_HELLO) handleHello(c, data, size);
        else if (type == C_AUTH) handleAuth(c, data, size);
        return;
    }

    switch (type) {
    case C_INPUTS:
        handleInputs(c, data, size);
        break;
    case C_BLOCKS:
        handleBlocks(c, data, size);
        break;
    case C_CHAT: {
        Reader r(data, size);
        std::string text = r.str();
        if (!r.ok) return;
        if (text.size() > 256) text.resize(256);
        text = cleanChatText(text);
        if (text.empty() || !allowChat(c)) return;
        if (onChat) {
            const std::string name = c.name;              // (a mod may kick this player)
            const uint16_t cid = c.id;
            if (!onChat(name, text) || text.empty()) return;
            if (text.size() > 256) text.resize(256);
            text = cleanChatText(text);
            if (m_clients.find(cid) == m_clients.end()) return;
        }
        broadcastChat("<" + c.name + "> " + text, kChatColor);
        break;
    }
    case C_COMMAND: {
        Reader r(data, size);
        std::string command = r.str();
        if (!r.ok) return;
        if (command.size() > 256) command.resize(256);
        command = cleanChatText(command);
        if (command.empty() || !allowChat(c)) return;
        if (onCommand) {
            const std::string name = c.name;   // the command may kick this player (c gone after)
            onCommand(name, command);
        } else {
            messagePlayer(c.name, "This server doesn't run commands", glm::vec3(1.0f, 0.55f, 0.55f));
        }
        break;
    }
    default:
        break;
    }
}

void GameServer::handleHello(Client& c, const uint8_t* data, size_t size) {
    Reader r(data, size);
    const uint32_t protocol = r.u32();
    const std::string name = r.str();
    const std::string game = r.str();
    const int view = r.u8();
    if (!r.ok) { kick(c, "Bad hello packet"); return; }
    (void)r.u8();   // hasPassword (informational)

    if (protocol != net::kProtocolVersion) {
        kick(c, protocol < net::kProtocolVersion ? "Your game is older than the server - please update"
                                                 : "The server is running an older version");
        return;
    }
    if (!net::validPlayerName(name)) { kick(c, "Invalid name (use 1-16 letters, numbers, _ or -)"); return; }
    if (game != m_game) {
        const std::string title = m_gameTitle.empty() ? m_game : m_gameTitle;
        kick(c, "This server plays " + title + " (" + m_game + "). Select that game on the title screen first.");
        return;
    }
    if (c.challenged) { kick(c, "Bad hello packet"); return; }

    // Password challenge (Auth.h): a fresh single-use nonce bound to this server's identity.
    // Nothing secret has been sent yet; the player answers with C_AUTH.
    if (m_serverId.empty()) m_serverId = "temp-" + auth::randomHex(16);   // setServerId wasn't called
    c.challenged = true;
    c.nonce = auth::randomHex(32);
    c.helloName = name;
    c.helloView = view;
    Writer w(S_CHALLENGE);
    w.str(m_serverId);
    w.str(c.nonce);
    w.u8((hasAccount && hasAccount(name)) ? 1 : 0);
    sendTo(c.peer, w);
}

void GameServer::handleAuth(Client& c, const uint8_t* data, size_t size) {
    if (!c.challenged || c.nonce.empty()) { kick(c, "Bad login packet"); return; }
    Reader r(data, size);
    net::AuthProof proof;
    proof.response = r.str();
    proof.registerKey = r.str();
    proof.serverId = m_serverId;
    proof.nonce = c.nonce;
    c.nonce.clear();                       // single use, whatever happens next
    c.challenged = false;
    if (!r.ok || proof.response.size() > 128 || proof.registerKey.size() > 128) { kick(c, "Bad login packet"); return; }
    const std::string name = c.helloName;

    if (canJoin) {
        const std::string reason = canJoin(name, proof);   // banned, not whitelisted, wrong password...
        if (!reason.empty()) { kick(c, reason); return; }
    }
    finishJoin(c, name, c.helloView);
}

void GameServer::finishJoin(Client& c, const std::string& name, int view) {
    if (m_hasHost && name == m_hostName) { kick(c, "Someone called '" + name + "' is already playing"); return; }
    for (const auto& [id, other] : m_clients) {
        if (other.joined && other.name == name) { kick(c, "Someone called '" + name + "' is already playing"); return; }
    }
    if (playerCount() >= m_maxPlayers) {
        kick(c, "The server is full (" + std::to_string(m_maxPlayers) + " players)");
        return;
    }

    c.name = name;
    c.viewDistance = std::clamp(view, 2, m_maxView);
    c.joined = true;

    // Spawn: where they were last time (dedicated server), else on the ground at the world
    // spawn if it's loaded, else next to the host
    glm::vec3 spawn = m_hostPos + glm::vec3(1.0f, 0.0f, 1.0f);
    const int ground = m_world->groundHeightAt(8, 8);
    if (ground >= 0) spawn = glm::vec3(8.5f, static_cast<float>(ground), 8.5f);
    else if (!m_hasHost) spawn = glm::vec3(8.5f, 100.0f, 8.5f);
    glm::vec3 saved;
    if (spawnFor && spawnFor(name, saved)) spawn = saved;
    c.position = spawn;
    c.hasPosition = true;

    Writer w(S_WELCOME);
    w.u16(c.id);
    w.u32(m_world->seed());
    w.f32(spawn.x); w.f32(spawn.y); w.f32(spawn.z);
    w.str(m_hostName);
    sendTo(c.peer, w);

    // Tell the newcomer who's here, and everyone else about the newcomer
    if (m_hasHost) {
        Writer host(S_ADD);
        host.u16(0);
        host.str(m_hostName);
        sendTo(c.peer, host);
    }
    for (const auto& [id, other] : m_clients) {
        if (!other.joined || id == c.id) continue;
        Writer a(S_ADD); a.u16(id); a.str(other.name); sendTo(c.peer, a);
        Writer b(S_ADD); b.u16(c.id); b.str(c.name); sendTo(other.peer, b);
    }

    net::PlayerView& v = m_views[c.id];
    v.name = name;
    v.color = colorForName(name);
    c.sim = MoveState{spawn, glm::vec3(0.0f), false};
    c.lastSeq = 0;
    c.moveBudget = 0.2f;
    c.simClock = 0.0;
    c.simOffsetSet = false;
    c.lastSnapTime = net::clockSeconds();
    pushSnapshot(v, c.lastSnapTime, spawn, 0.0f, 0.0f);

    // Everything shared so far (weather...), so they see the same world as everyone else
    if (!m_shared.empty()) {
        sendShared(c.peer, std::vector<std::pair<std::string, std::string>>(m_shared.begin(), m_shared.end()));
    }

    // The time of day, so their sky matches everyone's from the start
    if (m_hasDayClock) sendDayTime(c.peer);

    // Their game mode and whether they're an operator
    c.mode = modeFor ? modeFor(name) : std::string();
    if (c.mode.empty()) c.mode = m_defaultMode;
    c.op = isOperator ? isOperator(name) : false;
    sendMode(c);

    std::cout << "[Server] " << name << " joined" << std::endl;
    broadcastChat(name + " joined the game", kInfoColor);
    if (onJoin) onJoin(name);
}

void GameServer::sendMode(Client& c) {
    Writer w(S_MODE);
    w.str(c.mode);
    w.u8(c.op ? 1 : 0);
    sendTo(c.peer, w);
}

bool GameServer::setPlayerMode(const std::string& name, const std::string& mode) {
    for (auto& [id, c] : m_clients) {
        if (!c.joined || c.name != name) continue;
        c.mode = mode;
        sendMode(c);
        return true;
    }
    return false;
}

std::string GameServer::playerMode(const std::string& name) const {
    for (const auto& [id, c] : m_clients) if (c.joined && c.name == name) return c.mode;
    return "";
}

bool GameServer::setPlayerOperator(const std::string& name, bool op) {
    for (auto& [id, c] : m_clients) {
        if (!c.joined || c.name != name) continue;
        c.op = op;
        sendMode(c);
        return true;
    }
    return false;
}

bool GameServer::teleportPlayer(const std::string& name, const glm::vec3& pos) {
    for (auto& [id, c] : m_clients) {
        if (!c.joined || c.name != name) continue;
        c.sim.position = pos;
        c.sim.velocity = glm::vec3(0.0f);
        c.sim.grounded = false;
        c.position = pos;
        c.moveBudget = 0.2f;
        return true;
    }
    return false;
}

void GameServer::sendDayTime(ENetPeer* peer) {
    Writer w(S_TIME);
    const double whole = std::floor(m_dayClock.total);
    w.u32(static_cast<uint32_t>(std::min(whole, 4.0e9)));
    w.f32(static_cast<float>(m_dayClock.total - whole));
    w.f32(static_cast<float>(m_dayClock.secondsPerDay));
    w.f32(static_cast<float>(m_dayClock.speed));
    sendTo(peer, w);
}

void GameServer::setDayTime(const DayClock& clock) {
    // Where players' games think the time is now: what they were last sent, run on since
    const double now = net::clockSeconds();
    bool sendNow = !m_hasDayClock || clock.speed != m_daySent.speed || clock.secondsPerDay != m_daySent.secondsPerDay;
    if (!sendNow) {
        const double predicted = m_daySent.total + (now - m_daySentAt) * m_daySent.rate();
        sendNow = std::abs(clock.total - predicted) * clock.secondsPerDay > 2.0;   // a jump (time set / add)
    }
    m_dayClock = clock;
    m_hasDayClock = true;
    if (sendNow) {
        m_dayTimer = 0.0f;
        for (auto& [id, c] : m_clients) if (c.joined) sendDayTime(c.peer);
        m_daySent = clock;
        m_daySentAt = now;
    }
}

void GameServer::sendShared(ENetPeer* peer, const std::vector<std::pair<std::string, std::string>>& values) {
    Writer w(S_SHARED);
    w.u16(static_cast<uint16_t>(values.size()));
    for (const auto& [k, v] : values) { w.str(k); w.str(v); }
    sendTo(peer, w);
}

void GameServer::setShared(const std::string& key, const std::string& value) {
    if (key.empty()) return;
    const auto it = m_shared.find(key);
    if (it != m_shared.end() && it->second == value) return;   // nothing new
    m_shared[key] = value;
    for (auto& [id, c] : m_clients) {
        if (c.joined) sendShared(c.peer, {{key, value}});
    }
}

void GameServer::sendMessageTo(Client& c, const std::string& text, const glm::vec3& color) {
    Writer w(S_CHAT);
    w.str(text);
    w.f32(color.r); w.f32(color.g); w.f32(color.b);
    sendTo(c.peer, w);
}

// Chat from players: no control characters (they could fake new lines or confuse the chat
// box); UTF-8 letters are kept. Leading/trailing spaces go.
std::string GameServer::cleanChatText(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (unsigned char ch : in) {
        if (ch < 32 || ch == 127) { if (ch == '\t') out += ' '; continue; }
        out += static_cast<char>(ch);
    }
    const size_t a = out.find_first_not_of(' ');
    if (a == std::string::npos) return "";
    return out.substr(a, out.find_last_not_of(' ') - a + 1);
}

// Flood limit for chat and /commands (kChatBurst...). False = drop this one.
bool GameServer::allowChat(Client& c) {
    const double now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    if (c.chatRefillAt <= 0.0) c.chatRefillAt = now;
    c.chatTokens = std::min(kChatBurst, c.chatTokens + (now - c.chatRefillAt) / kChatRefillSeconds);
    c.chatRefillAt = now;
    if (c.chatTokens >= 1.0) {
        c.chatTokens -= 1.0;
        if (c.chatTokens >= kChatBurst - 1.0) c.spamStrikes = 0;   // calmed down
        return true;
    }
    if (++c.spamStrikes >= kSpamKickStrikes) {
        kick(c, "Kicked for spamming chat");
        return false;   // (c is gone)
    }
    if (now - c.lastSpamWarning > 2.0) {
        c.lastSpamWarning = now;
        sendMessageTo(c, "You're sending messages too fast - slow down.", glm::vec3(1.0f, 0.55f, 0.55f));
    }
    return false;
}

bool GameServer::messagePlayer(const std::string& name, const std::string& text, const glm::vec3& color) {
    for (auto& [id, c] : m_clients) {
        if (!c.joined || c.name != name) continue;
        Writer w(S_CHAT);
        w.str(text);
        w.f32(color.r); w.f32(color.g); w.f32(color.b);
        sendTo(c.peer, w);
        return true;
    }
    return false;
}

void GameServer::handleBlocks(Client& c, const uint8_t* data, size_t size) {
    Reader r(data, size);
    const uint16_t count = r.u16();
    std::vector<World::BlockChange> rejected;
    bool refusalSent = false;

    for (uint16_t i = 0; i < count && r.ok; ++i) {
        const glm::ivec3 pos = r.ivec3();
        const std::string name = r.str();
        const uint8_t meta = r.u8();
        if (!r.ok) break;

        uint8_t id = 0;
        const glm::vec3 eye = c.position + glm::vec3(0.0f, 1.62f, 0.0f);
        const bool inReach = glm::distance(eye, glm::vec3(pos) + glm::vec3(0.5f)) <= kMaxReach;
        const bool loaded = m_world->chunkData(chunkOf(glm::vec3(pos))) != nullptr &&
                            pos.y >= 0 && pos.y < CHUNK_HEIGHT;

        // Outside creative, blocks that can't be dug (bedrock...) can't be broken, whatever
        // the player's game says
        bool allowed = true;
        if (loaded && c.mode != "creative" && blockIdFromName(name, id) && id == 0) {
            const uint8_t was = m_world->getBlockAt(pos);
            if (was != 0 && blockHardness(BlockRegistry::getInstance().getBlock(was)) < 0.0f) allowed = false;
        }

        // Mods may refuse it (spawn protection, claims...): checked last, for real changes only
        if (allowed && canEdit && loaded && inReach && blockIdFromName(name, id)) {
            const uint8_t was = m_world->getBlockAt(pos);
            if (was != id) {
                const std::string why = canEdit(c.name, pos, was, id);
                if (!why.empty()) {
                    allowed = false;
                    if (why != kSilentRefusal && !refusalSent) {
                        sendMessageTo(c, why, glm::vec3(1.0f, 0.55f, 0.55f));
                        refusalSent = true;   // one message for a whole batch of edits
                    }
                }
            }
        }

        if (allowed && blockIdFromName(name, id) && inReach && loaded) {
            const uint8_t oldId = m_world->getBlockAt(pos);
            m_world->setBlockWithMetaAt(pos, id, meta);
            if (onPlayerEdit && oldId != id) onPlayerEdit(c.name, pos, oldId, id);
        } else if (loaded) {
            // Undo the player's prediction: send them what's really there
            rejected.push_back({pos, m_world->getBlockAt(pos), m_world->getMetadataAt(pos)});
        }
    }

    if (!rejected.empty()) {
        Writer w(S_BLOCKS);
        w.u16(static_cast<uint16_t>(rejected.size()));
        for (const auto& ch : rejected) {
            w.i32(ch.pos.x); w.i32(ch.pos.y); w.i32(ch.pos.z);
            w.str(blockName(ch.id));
            w.u8(ch.meta);
        }
        sendTo(c.peer, w);
    }
}

void GameServer::kick(Client& c, const std::string& reason) {
    Writer w(S_KICK);
    w.str(reason);
    sendTo(c.peer, w);
    enet_peer_disconnect_later(c.peer, 0);
    std::cout << "[Server] Kicked " << (c.name.empty() ? "a player" : c.name) << ": " << reason << std::endl;
    // Out of the game straight away (they'd otherwise count as online until their
    // connection timed out, and couldn't rejoin). ENet still finishes the disconnect.
    // Callers must not use `c` after this.
    dropClient(c.id);
}

void GameServer::dropClient(uint16_t id) {
    auto it = m_clients.find(id);
    if (it == m_clients.end()) return;
    const bool wasJoined = it->second.joined;
    const std::string name = it->second.name;
    if (wasJoined && onLeave) onLeave(name, it->second.position);   // e.g. save where they were
    m_clients.erase(it);
    m_views.erase(id);

    if (wasJoined) {
        for (auto& [oid, other] : m_clients) {
            if (!other.joined) continue;
            Writer w(S_REMOVE);
            w.u16(id);
            sendTo(other.peer, w);
        }
        std::cout << "[Server] " << name << " left" << std::endl;
        broadcastChat(name + " left the game", kInfoColor);
    }
}

void GameServer::streamChunks(Client& c) {
    const World::ChunkCoord center = chunkOf(c.position);
    const int view = c.viewDistance;

    // Drop chunks that are now too far away
    for (auto it = c.sentChunks.begin(); it != c.sentChunks.end();) {
        const int dx = it->first - center.first, dz = it->second - center.second;
        if (std::max(std::abs(dx), std::abs(dz)) > view + 1) {
            Writer w(S_UNLOAD);
            w.i32(it->first); w.i32(it->second);
            sendTo(c.peer, w);
            it = c.sentChunks.erase(it);
        } else {
            ++it;
        }
    }

    // Send the nearest loaded chunks they don't have yet
    int sent = 0;
    for (const auto& [dx, dz] : ringOffsets()) {
        if (sent >= kChunksPerUpdate) break;
        if (c.peer->reliableDataInTransit > kMaxChunkBytesInFlight) break;
        if (std::max(std::abs(dx), std::abs(dz)) > view) continue;

        const World::ChunkCoord cc{center.first + dx, center.second + dz};
        if (c.sentChunks.count(cc)) continue;
        const ChunkData* data = m_world->chunkData(cc);
        if (!data) continue;   // not generated yet - next time

        Writer w(S_CHUNK);
        w.i32(cc.first); w.i32(cc.second);
        w.bytes(World::encodeChunk(*data));
        sendTo(c.peer, w);
        c.sentChunks.insert(cc);
        ++sent;
    }
}

void GameServer::sendBlockChanges(const std::vector<World::BlockChange>& changes) {
    if (changes.empty()) return;
    for (auto& [id, c] : m_clients) {
        if (!c.joined) continue;
        std::vector<const World::BlockChange*> mine;
        for (const auto& ch : changes) {
            if (c.sentChunks.count(chunkOf(glm::vec3(ch.pos)))) mine.push_back(&ch);
        }
        // Several packets if a lot changed at once
        for (size_t start = 0; start < mine.size(); start += 4096) {
            const size_t end = std::min(mine.size(), start + 4096);
            Writer w(S_BLOCKS);
            w.u16(static_cast<uint16_t>(end - start));
            for (size_t i = start; i < end; ++i) {
                w.i32(mine[i]->pos.x); w.i32(mine[i]->pos.y); w.i32(mine[i]->pos.z);
                w.str(blockName(mine[i]->id));
                w.u8(mine[i]->meta);
            }
            sendTo(c.peer, w);
        }
    }
}

void GameServer::sendPositions() {
    struct Entry { uint16_t id; float time; glm::vec3 p; float yaw, pitch; };
    const float serverTime = net::clockSeconds();
    std::vector<Entry> all;
    if (m_hasHost) all.push_back({0, serverTime, m_hostPos, m_hostYaw, m_hostPitch});
    for (const auto& [id, c] : m_clients) {
        if (c.joined && c.hasPosition) {
            all.push_back({id, static_cast<float>(c.lastSnapTime), c.position, c.yaw, c.pitch});
        }
    }

    for (auto& [id, c] : m_clients) {
        if (!c.joined) continue;

        // Your own real position, and the last input it includes (for reconciliation)
        Writer ack(S_ACK);
        ack.u32(c.lastSeq);
        ack.f32(c.sim.position.x); ack.f32(c.sim.position.y); ack.f32(c.sim.position.z);
        ack.f32(c.sim.velocity.x); ack.f32(c.sim.velocity.y); ack.f32(c.sim.velocity.z);
        ack.u8(c.sim.grounded ? 1 : 0);
        sendTo(c.peer, ack, false);

        // Everyone else you can see
        const float range = (c.viewDistance + 1) * static_cast<float>(CHUNK_WIDTH);
        Writer w(S_PLAYERS);
        w.f32(serverTime);
        const size_t countAt = w.d.size();
        w.u16(0);   // count, filled in below
        uint16_t n = 0;
        for (const Entry& e : all) {
            if (e.id == id) continue;
            const glm::vec2 d(e.p.x - c.position.x, e.p.z - c.position.z);
            if (glm::length(d) > range) continue;   // too far to see
            w.u16(e.id);
            w.f32(e.time);
            w.f32(e.p.x); w.f32(e.p.y); w.f32(e.p.z);
            w.f32(e.yaw); w.f32(e.pitch);
            ++n;
        }
        if (n == 0) continue;
        w.d[countAt] = n & 0xFF;
        w.d[countAt + 1] = n >> 8;
        sendTo(c.peer, w, false);
    }
}

void GameServer::broadcastChat(const std::string& text, const glm::vec3& color) {
    m_chat.push_back({text, color});
    for (auto& [id, c] : m_clients) {
        if (!c.joined) continue;
        Writer w(S_CHAT);
        w.str(text);
        w.f32(color.r); w.f32(color.g); w.f32(color.b);
        sendTo(c.peer, w);
    }
}

void GameServer::setInfo(const std::string& motd, int maxPlayers) {
    m_motd = motd.substr(0, net::kMaxMotdLength);
    m_maxPlayers = std::clamp(maxPlayers, 2, 4000);
}

void GameServer::sendStatus(ENetPeer* peer) {
    Writer w(S_STATUS);
    w.u32(net::kProtocolVersion);
    w.str(net::kVersionName);
    w.str(m_game);
    w.str(m_motd);
    w.u16(static_cast<uint16_t>(playerCount()));
    w.u16(static_cast<uint16_t>(m_maxPlayers));
    std::vector<std::string> names;
    if (m_hasHost) names.push_back(m_hostName);
    for (const auto& [id, c] : m_clients) {
        if (c.joined && names.size() < 20) names.push_back(c.name);
    }
    w.u16(static_cast<uint16_t>(names.size()));
    for (const auto& n : names) w.str(n);
    // Icon, then the game's title: added last, so older clients that stop reading earlier still work
    w.u32(static_cast<uint32_t>(m_icon.size()));
    w.bytes(m_icon);
    w.str(m_gameTitle);
    sendTo(peer, w);
}

void GameServer::setIcon(std::vector<uint8_t> png) {
    if (png.size() > net::kMaxIconBytes) png.clear();
    m_icon = std::move(png);
}

// Tells everyone on the local network that this game is open to join.
// Message: "CRLAN1\n<port>\n<host name>\n<players>\n<max>\n<description>"
void GameServer::announceLan() {
    if (m_lanSocket == -1) {
        ENetSocket sock = enet_socket_create(ENET_SOCKET_TYPE_DATAGRAM);
        if (sock == ENET_SOCKET_NULL) return;
        enet_socket_set_option(sock, ENET_SOCKOPT_BROADCAST, 1);
        enet_socket_set_option(sock, ENET_SOCKOPT_NONBLOCK, 1);
        m_lanSocket = static_cast<intptr_t>(sock);
    }
    const std::string msg = "CRLAN1\n" + std::to_string(m_port) + "\n" + m_hostName + "\n" +
                            std::to_string(playerCount()) + "\n" + std::to_string(m_maxPlayers) + "\n" + m_motd;
    ENetBuffer buf;
    buf.data = const_cast<char*>(msg.data());
    buf.dataLength = msg.size();
    ENetAddress to;
    to.port = net::kLanPort;
    to.host = ENET_HOST_BROADCAST;                       // every PC on the network
    enet_socket_send(static_cast<ENetSocket>(m_lanSocket), &to, &buf, 1);
    enet_address_set_host(&to, "127.0.0.1");             // and this PC (some systems don't loop broadcasts back)
    to.port = net::kLanPort;
    enet_socket_send(static_cast<ENetSocket>(m_lanSocket), &to, &buf, 1);
}

void GameServer::sendChat(const std::string& text) {
    if (!running() || text.empty()) return;
    broadcastChat("<" + m_hostName + "> " + text, kChatColor);
}

void GameServer::broadcastMessage(const std::string& text, const glm::vec3& color) {
    if (!running() || text.empty()) return;
    broadcastChat(text, color);
}

std::vector<std::string> GameServer::playerNames() const {
    std::vector<std::string> out;
    for (const auto& [id, c] : m_clients) if (c.joined) out.push_back(c.name);
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<std::pair<std::string, glm::vec3>> GameServer::playerPositions() const {
    std::vector<std::pair<std::string, glm::vec3>> out;
    for (const auto& [id, c] : m_clients) if (c.joined) out.push_back({c.name, c.position});
    return out;
}

bool GameServer::kickPlayer(const std::string& name, const std::string& reason) {
    for (auto& [id, c] : m_clients) {
        if (c.joined && c.name == name) {
            kick(c, reason.empty() ? std::string("Kicked by the server") : reason);
            return true;
        }
    }
    return false;
}

std::vector<net::ChatLine> GameServer::takeChat() {
    std::vector<net::ChatLine> out;
    out.swap(m_chat);
    return out;
}

// ===========================================================================
// GameClient
// ===========================================================================
GameClient::~GameClient() {
    if (m_host) enet_host_destroy(m_host);
}

bool GameClient::connect(World& world, const std::string& address, const std::string& playerName,
                         const std::string& gameFolder, int viewDistance, std::string& error,
                         const std::string& passwordToken, const std::string& knownServerId) {
    disconnect();
    m_ended = false;
    m_endReason.clear();
    m_kickReason.clear();
    m_haveWelcome = false;

    if (!net::validPlayerName(playerName)) {
        error = "Pick a name first (1-16 letters, numbers, _ or -)";
        return false;
    }
    std::string hostText;
    uint16_t port = 0;
    if (!net::parseAddress(address, hostText, port)) {
        error = "That isn't a valid address (example: 192.168.1.20 or myserver.net:30000)";
        return false;
    }
    if (!ensureEnet()) { error = "Networking could not be started"; return false; }

    ENetAddress addr;
    if (enet_address_set_host(&addr, hostText.c_str()) != 0) {
        error = "Could not find '" + hostText + "'";
        return false;
    }
    addr.port = port;

    m_host = enet_host_create(nullptr, 1, 2, 0, 0);
    if (!m_host) { error = "Could not open a network socket"; return false; }
    m_peer = enet_host_connect(m_host, &addr, 2, 0);
    if (!m_peer) {
        enet_host_destroy(m_host);
        m_host = nullptr;
        error = "Could not start connecting";
        return false;
    }

    m_world = &world;
    m_addressText = hostText + ":" + std::to_string(port);
    m_name = playerName;
    m_password = passwordToken;
    m_knownServerId = knownServerId;
    m_serverId.clear();
    m_game = gameFolder;
    m_viewDistance = std::clamp(viewDistance, 2, net::kMaxViewDistance);
    m_connectStarted = nowSeconds();
    m_state = State::Connecting;
    m_views.clear();
    m_chat.clear();
    m_shared.clear();
    m_sharedChanges.clear();
    std::cout << "[Client] Connecting to " << m_addressText << " as " << m_name << std::endl;
    return true;
}

void GameClient::disconnect() {
    if (!m_host) { m_state = State::Idle; return; }
    if (m_peer) {
        enet_peer_disconnect(m_peer, 0);
        ENetEvent ev;
        const double until = nowSeconds() + 0.3;
        bool done = false;
        while (!done && nowSeconds() < until && enet_host_service(m_host, &ev, 20) >= 0) {
            if (ev.type == ENET_EVENT_TYPE_RECEIVE) enet_packet_destroy(ev.packet);
            if (ev.type == ENET_EVENT_TYPE_DISCONNECT) done = true;
        }
        if (!done) enet_peer_reset(m_peer);
    }
    enet_host_destroy(m_host);
    m_host = nullptr;
    m_peer = nullptr;
    m_state = State::Idle;
    m_views.clear();
    m_mode.clear(); m_isOp = false; m_modeChanged = true;   // our own game decides again
    std::cout << "[Client] Disconnected" << std::endl;
}

void GameClient::finish(const std::string& reason) {
    const bool wasJoined = m_state == State::Joined;
    if (m_host) {
        enet_host_destroy(m_host);
        m_host = nullptr;
        m_peer = nullptr;
    }
    m_state = State::Idle;
    m_views.clear();
    if (!m_mode.empty() || m_isOp) { m_mode.clear(); m_isOp = false; m_modeChanged = true; }
    m_ended = true;
    if (!m_kickReason.empty()) m_endReason = m_kickReason;
    else m_endReason = wasJoined ? reason : "Could not connect to " + m_addressText +
                                            ". Is it running? If it's on another PC, that PC's firewall"
                                            " may be blocking the game (allow UDP port " +
                                            m_addressText.substr(m_addressText.rfind(':') + 1) + ").";
    std::cout << "[Client] " << m_endReason << std::endl;
}

void GameClient::update(const MoveState& local, float yaw, float pitch, float dt) {
    (void)yaw; (void)pitch;
    if (!m_host) return;
    m_local = local;

    ENetEvent ev;
    while (m_host && enet_host_service(m_host, &ev, 0) > 0) {
        switch (ev.type) {
        case ENET_EVENT_TYPE_CONNECT: {
            disableThrottle(m_peer);
            Writer w(C_HELLO);
            w.u32(net::kProtocolVersion);
            w.str(m_name);
            w.str(m_game);
            w.u8(static_cast<uint8_t>(m_viewDistance));
            w.u8(m_password.empty() ? 0 : 1);   // the password itself is never sent (S_CHALLENGE)
            sendTo(m_peer, w);
            break;
        }
        case ENET_EVENT_TYPE_RECEIVE:
            handlePacket(ev.packet->data, ev.packet->dataLength);
            enet_packet_destroy(ev.packet);
            break;
        case ENET_EVENT_TYPE_DISCONNECT:
            finish("Lost connection to the server");
            return;
        default:
            break;
        }
    }
    if (!m_host) return;

    if (m_state == State::Connecting && nowSeconds() - m_connectStarted > kConnectTimeoutSec) {
        finish("Timed out");
        return;
    }

    if (m_state == State::Joined) {
        sendEdits();
        m_posTimer += dt;
        if (m_posTimer >= kPositionInterval) {
            m_posTimer = std::fmod(m_posTimer, kPositionInterval);
            sendInputs();
        }
        // Other players: 100 ms behind the server's clock
        // Draw other players far enough in the past to cover how unevenly their updates arrive
        // (100 ms on a good network, more on jittery Wi-Fi), easing between values.
        // m_jitter = how stale a player's newest known position gets before the next one
        // arrives (their Wi-Fi, the server and ours all add to it). It jumps up at once and
        // settles down over several seconds, so a bad patch of Wi-Fi stays covered.
        if (m_clockSynced) {
            const double serverNow = net::clockSeconds() + m_clockOffset;
            double stalest = 0.0;
            for (const auto& [id, v] : m_views) {
                if (!v.snapshots.empty()) stalest = std::max(stalest, serverNow - v.snapshots.back().time);
            }
            stalest = std::min(stalest, 1.0);   // someone standing still / gone isn't Wi-Fi
            if (stalest > m_jitter) m_jitter = stalest;
            else m_jitter -= (m_jitter - stalest) * std::min(1.0, static_cast<double>(dt) * 0.3);
        }
        {
            const double target = std::clamp(m_jitter + 0.04, kInterpDelay, kMaxInterpDelay);
            // Change it gradually: a jump would make other players stop (delay going up) or
            // skip ahead (going down). This way they play at 75%..110% speed for a moment.
            const double step = static_cast<double>(dt);
            m_interpDelay += std::clamp(target - m_interpDelay, -step * 0.10, step * 0.25);
        }
        if (m_clockSynced) interpolatePlayers(m_views, net::clockSeconds() + m_clockOffset - m_interpDelay);
    }
    if (m_host) enet_host_flush(m_host);
}

void GameClient::sendEdits() {
    const auto changes = m_world->takeBlockChanges();
    if (changes.empty()) return;
    for (size_t start = 0; start < changes.size(); start += 4096) {
        const size_t end = std::min(changes.size(), start + 4096);
        Writer w(C_BLOCKS);
        w.u16(static_cast<uint16_t>(end - start));
        for (size_t i = start; i < end; ++i) {
            const auto& ch = changes[i];
            w.i32(ch.pos.x); w.i32(ch.pos.y); w.i32(ch.pos.z);
            w.str(blockName(ch.id));
            w.u8(ch.meta);
        }
        sendTo(m_peer, w);
    }
}

void GameClient::handlePacket(const uint8_t* data, size_t size) {
    if (size == 0) return;
    Reader r(data, size);

    switch (data[0]) {
    case S_WELCOME: {
        Welcome wel;
        wel.playerId = r.u16();
        wel.seed = r.u32();
        wel.spawn = r.vec3f();
        wel.hostName = r.str();
        if (!r.ok) return;
        // Fresh world that only holds what the server sends
        m_world->reset();
        m_world->setSaveDirectory("");
        m_world->setSeed(wel.seed);
        m_world->setRemote(true);
        m_welcome = wel;
        m_haveWelcome = true;
        m_pending.clear();
        m_nextSeq = 1;
        m_lastAck = 0;
        m_haveCorrection = false;
        m_corrections = 0;
        m_clockSynced = false;
        m_state = State::Joined;
        std::cout << "[Client] Joined " << m_addressText << std::endl;
        break;
    }
    case S_CHALLENGE: {
        // Prove we know the password without sending anything replayable (Auth.h)
        const std::string serverId = r.str();
        const std::string nonce = r.str();
        const bool accountExists = r.u8() != 0;
        if (!r.ok || serverId.empty() || nonce.empty() || serverId.size() > 128 || nonce.size() > 128) {
            m_kickReason = "The server sent a bad login challenge";   // (shown as is, not "could not connect")
            finish(m_kickReason);
            break;
        }
        m_serverId = serverId;
        Writer w(C_AUTH);
        if (m_password.empty()) {
            w.str(""); w.str("");                          // no password: nothing to prove
        } else {
            if (!m_knownServerId.empty() && serverId != m_knownServerId) {
                // A different server answering at this address: don't hand it a response
                m_kickReason = "This server's identity changed since you last joined, so your password was not "
                               "used. If its owner reinstalled it, remove the line 'server_id." + m_addressText +
                               "' from settings.txt and join again.";
                finish(m_kickReason);
                break;
            }
            // The slow part (PBKDF2): once per (token, name), then remembered
            static std::string cachedFor, cachedKey;
            const std::string cacheId = m_password + "\n" + auth::toLower(m_name);
            if (cachedFor != cacheId) { cachedKey = auth::playerKey(m_password, m_name); cachedFor = cacheId; }
            const std::string serverKey = auth::serverKey(cachedKey, serverId);
            w.str(auth::response(serverKey, serverId, nonce, m_name));
            w.str(accountExists ? std::string() : serverKey);   // only to create the account
        }
        sendTo(m_peer, w);
        break;
    }
    case S_KICK:
        // The server is dropping us anyway - leave now with its reason
        m_kickReason = r.str();
        if (m_kickReason.empty()) m_kickReason = "Disconnected by the server";
        finish(m_kickReason);
        break;
    case S_CHUNK: {
        if (m_state != State::Joined) return;
        const int cx = r.i32(), cz = r.i32();
        if (!r.ok) return;
        auto chunk = std::make_shared<ChunkData>();
        if (World::decodeChunk(r.rest(), *chunk)) m_world->insertChunk(cx, cz, std::move(chunk));
        break;
    }
    case S_UNLOAD: {
        const int cx = r.i32(), cz = r.i32();
        if (r.ok && m_state == State::Joined) m_world->removeChunk(cx, cz);
        break;
    }
    case S_MODE: {
        Reader r(data, size);
        const std::string mode = r.str();
        const uint8_t op = r.u8();
        if (!r.ok) break;
        m_mode = mode;
        m_isOp = op != 0;
        m_modeChanged = true;
        break;
    }
    case S_TIME: {
        const uint32_t whole = r.u32();
        const float frac = r.f32();
        const float secondsPerDay = r.f32();
        const float speed = r.f32();
        if (!r.ok) break;
        m_dayTime.total = static_cast<double>(whole) + std::clamp(static_cast<double>(frac), 0.0, 0.999999);
        m_dayTime.secondsPerDay = std::clamp(static_cast<double>(secondsPerDay), 0.0, 1.0e7);
        m_dayTime.speed = std::clamp(static_cast<double>(speed), 0.0, 1000.0);
        m_dayTimeNew = true;
        break;
    }
    case S_SHARED: {
        const uint16_t count = r.u16();
        for (uint16_t i = 0; i < count && r.ok; ++i) {
            std::string key = r.str();
            std::string value = r.str();
            if (!r.ok) break;
            m_shared[key] = value;
            m_sharedChanges.push_back(key);
        }
        break;
    }
    case S_BLOCKS: {
        if (m_state != State::Joined) return;
        const uint16_t count = r.u16();
        for (uint16_t i = 0; i < count && r.ok; ++i) {
            const glm::ivec3 pos = r.ivec3();
            const std::string name = r.str();
            const uint8_t meta = r.u8();
            if (!r.ok) break;
            uint8_t id = 0;
            if (!blockIdFromName(name, id)) id = 0;   // block we don't have -> air
            const uint8_t oldId = m_world->getBlockAt(pos);
            m_world->applyNetworkBlock(pos, id, meta);
            if (onBlockChanged && oldId != id) onBlockChanged(pos, oldId, id);
        }
        break;
    }
    case S_ADD: {
        const uint16_t id = r.u16();
        const std::string name = r.str();
        if (!r.ok) return;
        auto& v = m_views[id];
        v.name = name;
        v.color = colorForName(name);
        break;
    }
    case S_REMOVE: {
        const uint16_t id = r.u16();
        if (r.ok) m_views.erase(id);
        break;
    }
    case S_PLAYERS: {
        const float serverTime = r.f32();
        const uint16_t count = r.u16();
        if (!r.ok) return;
        // Estimate the server's clock. A packet can't arrive before it was sent, so the
        // largest (server time - our time) seen is the best estimate; drift down slowly.
        const double sample = serverTime - net::clockSeconds();
        if (!m_clockSynced || sample > m_clockOffset) m_clockOffset = sample;
        else m_clockOffset += (sample - m_clockOffset) * 0.01;
        m_clockSynced = true;

        for (uint16_t i = 0; i < count && r.ok; ++i) {
            const uint16_t id = r.u16();
            const float time = r.f32();
            const glm::vec3 p = r.vec3f();
            const float yaw = r.f32();
            const float pitch = r.f32();
            if (!r.ok) break;
            auto it = m_views.find(id);
            if (it != m_views.end()) pushSnapshot(it->second, time, p, yaw, pitch);
        }
        break;
    }
    case S_ACK: {
        const uint32_t seq = r.u32();
        MoveState server;
        server.position = r.vec3f();
        server.velocity = r.vec3f();
        server.grounded = r.u8() != 0;
        if (r.ok && m_state == State::Joined) reconcile(seq, server);
        break;
    }
    case S_PINGS: {
        const uint16_t count = r.u16();
        for (uint16_t i = 0; i < count && r.ok; ++i) {
            const uint16_t id = r.u16();
            const uint16_t ping = r.u16();
            if (!r.ok) break;
            auto it = m_views.find(id);
            if (it != m_views.end()) it->second.ping = ping;
        }
        break;
    }
    case S_CHAT: {
        net::ChatLine line;
        line.text = r.str();
        line.color = r.vec3f();
        if (r.ok) m_chat.push_back(line);
        break;
    }
    default:
        break;
    }
}

void GameClient::recordInput(const MoveInput& input, float dt) {
    if (m_state != State::Joined) return;
    dt = std::min(dt, kMaxMoveStep);
    if (dt <= 0.0f) return;
    m_pending.push_back({m_nextSeq++, input, dt});
    while (m_pending.size() > 2000) m_pending.pop_front();   // server unreachable for a long time
}

// Sends every input the server hasn't confirmed yet (up to a limit). Resending them each
// time means a lost packet costs nothing - the next one carries the same inputs.
void GameClient::sendInputs() {
    if (m_pending.empty()) return;
    const size_t n = std::min(m_pending.size(), kMaxInputsPerPacket);
    Writer w(C_INPUTS);
    w.u32(m_pending.front().seq);
    w.u16(static_cast<uint16_t>(n));
    for (size_t i = 0; i < n; ++i) {
        const auto& p = m_pending[i];
        w.u8(p.input.keys);
        w.f32(p.input.yaw);
        w.f32(p.input.pitch);
        w.f32(p.dt);
    }
    sendTo(m_peer, w, false);
}

// The server says: "after your input #ackSeq you were at `server`". Start from there,
// replay the inputs it hasn't seen yet, and correct our player only if we ended up
// somewhere different (another player pushed a block in the way, lag, cheating...).
void GameClient::reconcile(uint32_t ackSeq, const MoveState& server) {
    if (ackSeq < m_lastAck) return;   // older than one we already used
    m_lastAck = ackSeq;
    while (!m_pending.empty() && m_pending.front().seq <= ackSeq) m_pending.pop_front();

    MoveState s = server;
    for (const auto& p : m_pending) stepMovement(s, p.input, p.dt, *m_world);

    const float posError = glm::distance(s.position, m_local.position);
    const float velError = glm::length(s.velocity - m_local.velocity);
    if (posError > 0.02f || velError > 0.5f) {
        m_correction = s;
        m_haveCorrection = true;
        m_local = s;
        ++m_corrections;
    }
}

bool GameClient::takeCorrection(MoveState& out) {
    if (!m_haveCorrection) return false;
    m_haveCorrection = false;
    out = m_correction;
    return true;
}

bool GameClient::takeWelcome(Welcome& out) {
    if (!m_haveWelcome) return false;
    m_haveWelcome = false;
    out = m_welcome;
    return true;
}

bool GameClient::takeDisconnect(std::string& reason) {
    if (!m_ended) return false;
    m_ended = false;
    reason = m_endReason;
    return true;
}

void GameClient::sendCommand(const std::string& command) {
    if (m_state != State::Joined || command.empty()) return;
    Writer w(C_COMMAND);
    w.str(command);
    sendTo(m_peer, w);
}

void GameClient::sendChat(const std::string& text) {
    if (m_state != State::Joined || text.empty()) return;
    Writer w(C_CHAT);
    w.str(text);
    sendTo(m_peer, w);
}

std::vector<net::ChatLine> GameClient::takeChat() {
    std::vector<net::ChatLine> out;
    out.swap(m_chat);
    return out;
}

// ===========================================================================
// /netstats
// ===========================================================================
namespace {
std::string rateText(const net::PlayerView& v) {
    // No update for over a second = the rate shown would be stale
    const double age = nowSeconds() - v.lastUpdate;
    const float rate = age > 1.5 ? 0.0f : v.updateRate;
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.0f updates/s", rate);
    return buf;
}
}

std::vector<std::string> GameServer::statsLines() const {
    std::vector<std::string> out;
    out.push_back("Hosting on port " + std::to_string(m_port) + ", " + std::to_string(playerCount()) + " players");
    for (const auto& [id, c] : m_clients) {
        if (!c.joined) continue;
        char buf[200];
        std::snprintf(buf, sizeof(buf), "%s: ping %u ms, %d inputs/s, moving %.1f blocks/s, refused %d",
                      c.name.c_str(), c.peer->roundTripTime, c.inputsPerSecond, c.speed, c.droppedInputs);
        out.push_back(buf);
        auto it = m_views.find(id);
        if (it != m_views.end()) {
            const auto& v = it->second;
            const double behind = v.snapshots.empty() ? 0.0
                                : (net::clockSeconds() - v.snapshots.back().time) * 1000.0;
            std::snprintf(buf, sizeof(buf), "   drawn from %s, newest %.0f ms old", rateText(v).c_str(), behind);
            out.push_back(buf);
        }
    }
    return out;
}

int GameClient::ping() const {
    if (m_state != State::Joined || !m_peer) return -1;
    return static_cast<int>(m_peer->roundTripTime);
}

std::vector<std::string> GameClient::statsLines() const {
    std::vector<std::string> out;
    if (m_state != State::Joined || !m_peer) {
        out.push_back("Not connected to a server");
        return out;
    }
    out.push_back("Server " + m_addressText + ": ping " + std::to_string(m_peer->roundTripTime) + " ms");
    out.push_back("Your position corrected by the server " + std::to_string(m_corrections) +
                  " times, " + std::to_string(m_pending.size()) + " inputs waiting");
    {
        char jb[160];
        std::snprintf(jb, sizeof(jb), "Other players' updates arrive up to %.0f ms apart - drawn %.0f ms behind",
                      m_jitter * 1000.0, m_interpDelay * 1000.0);
        out.push_back(jb);
    }
    for (const auto& [id, v] : m_views) {
        const double behind = v.snapshots.empty() ? 0.0
                            : (net::clockSeconds() + m_clockOffset - v.snapshots.back().time) * 1000.0;
        char buf[160];
        std::snprintf(buf, sizeof(buf), "%s: drawn from %s, newest %.0f ms old", v.name.c_str(),
                      rateText(v).c_str(), behind);
        out.push_back(buf);
    }
    return out;
}

// ===========================================================================
// ServerPinger
// ===========================================================================
namespace {
constexpr double kStatusTimeoutSec = 5.0;
}

ServerPinger::~ServerPinger() { clear(); }

void ServerPinger::clear() {
    for (auto& [addr, q] : m_queries) {
        if (q.host) enet_host_destroy(q.host);
    }
    m_queries.clear();
}

void ServerPinger::finish(Query& q) {
    if (q.peer) enet_peer_reset(q.peer);
    if (q.host) enet_host_destroy(q.host);
    q.host = nullptr;
    q.peer = nullptr;
}

void ServerPinger::ping(const std::string& address) {
    Query& q = m_queries[address];
    finish(q);
    q.result = ServerStatus{};

    std::string hostText;
    uint16_t port = 0;
    ENetAddress addr;
    if (!net::parseAddress(address, hostText, port)) {
        q.result.state = ServerStatus::State::Offline;
        q.result.error = "Invalid address";
        return;
    }
    if (!ensureEnet() || enet_address_set_host(&addr, hostText.c_str()) != 0) {
        q.result.state = ServerStatus::State::Offline;
        q.result.error = "Unknown host";
        return;
    }
    addr.port = port;
    q.host = enet_host_create(nullptr, 1, 2, 0, 0);
    if (q.host) q.peer = enet_host_connect(q.host, &addr, 2, net::kStatusQuery);
    if (!q.host || !q.peer) {
        finish(q);
        q.result.state = ServerStatus::State::Offline;
        q.result.error = "Can't connect to server";
        return;
    }
    q.started = nowSeconds();
}

void ServerPinger::update() {
    for (auto& [address, q] : m_queries) {
        if (!q.host) continue;
        ENetEvent ev;
        while (q.host && enet_host_service(q.host, &ev, 0) > 0) {
            if (ev.type == ENET_EVENT_TYPE_CONNECT) {
                q.connectedAt = nowSeconds();
            } else if (ev.type == ENET_EVENT_TYPE_RECEIVE) {
                Reader r(ev.packet->data, ev.packet->dataLength);
                if (ev.packet->dataLength > 0 && ev.packet->data[0] == S_STATUS) {
                    ServerStatus st;
                    const uint32_t protocol = r.u32();
                    st.version = r.str();
                    st.game = r.str();
                    st.motd = r.str();
                    st.players = r.u16();
                    st.maxPlayers = r.u16();
                    const uint16_t n = r.u16();
                    for (uint16_t i = 0; i < n && r.ok; ++i) st.names.push_back(r.str());
                    if (r.ok) {
                        // Optional extras (older servers don't send them): icon, then game title
                        Reader extra = r;
                        const uint32_t len = extra.u32();
                        if (extra.ok && len <= net::kMaxIconBytes && extra.need(len)) {
                            if (len > 0) st.icon.assign(ev.packet->data + extra.p, ev.packet->data + extra.p + len);
                            extra.p += len;
                            const std::string title = extra.str();
                            if (extra.ok) st.gameTitle = title;
                        }
                    }
                    if (r.ok) {
                        st.state = ServerStatus::State::Online;
                        st.compatible = protocol == net::kProtocolVersion;
                        // The server answers the moment the handshake finishes: one round trip
                        st.ping = static_cast<int>((nowSeconds() - (q.connectedAt > 0 ? q.connectedAt : q.started)) * 1000.0);
                        q.result = st;
                    }
                }
                enet_packet_destroy(ev.packet);
                if (q.result.state == ServerStatus::State::Online) { finish(q); break; }
            } else if (ev.type == ENET_EVENT_TYPE_DISCONNECT) {
                if (q.result.state != ServerStatus::State::Online) {
                    q.result.state = ServerStatus::State::Offline;
                    q.result.error = "Can't connect to server";
                }
                finish(q);
                break;
            }
        }
        if (q.host && nowSeconds() - q.started > kStatusTimeoutSec) {
            finish(q);
            q.result.state = ServerStatus::State::Offline;
            q.result.error = "Can't connect to server";
        }
    }
}

const ServerStatus* ServerPinger::status(const std::string& address) const {
    auto it = m_queries.find(address);
    return it != m_queries.end() ? &it->second.result : nullptr;
}

// ===========================================================================
// LanScanner
// ===========================================================================
LanScanner::~LanScanner() { stop(); }

void LanScanner::start() {
    if (m_socket != -1 || !ensureEnet()) return;
    ENetSocket sock = enet_socket_create(ENET_SOCKET_TYPE_DATAGRAM);
    if (sock == ENET_SOCKET_NULL) return;
    enet_socket_set_option(sock, ENET_SOCKOPT_REUSEADDR, 1);   // several games on one PC can all listen
    enet_socket_set_option(sock, ENET_SOCKOPT_NONBLOCK, 1);
    enet_socket_set_option(sock, ENET_SOCKOPT_BROADCAST, 1);
    ENetAddress addr;
    addr.host = ENET_HOST_ANY;
    addr.port = net::kLanPort;
    if (enet_socket_bind(sock, &addr) != 0) {
        std::cerr << "[LAN] Could not listen on UDP port " << net::kLanPort << std::endl;
        enet_socket_destroy(sock);
        return;
    }
    m_socket = static_cast<intptr_t>(sock);
}

void LanScanner::stop() {
    if (m_socket != -1) enet_socket_destroy(static_cast<ENetSocket>(m_socket));
    m_socket = -1;
    m_games.clear();
}

void LanScanner::update() {
    if (m_socket == -1) return;
    const double now = nowSeconds();
    char data[1024];
    for (int guard = 0; guard < 64; ++guard) {
        ENetAddress from;
        ENetBuffer buf;
        buf.data = data;
        buf.dataLength = sizeof(data) - 1;
        const int n = enet_socket_receive(static_cast<ENetSocket>(m_socket), &from, &buf, 1);
        if (n <= 0) break;
        const std::string msg(data, static_cast<size_t>(n));

        // Split into lines; the description is everything after the 5th line break
        std::vector<std::string> parts;
        size_t start = 0;
        while (parts.size() < 5) {
            const size_t nl = msg.find('\n', start);
            if (nl == std::string::npos) break;
            parts.push_back(msg.substr(start, nl - start));
            start = nl + 1;
        }
        if (parts.size() < 5 || parts[0] != "CRLAN1") continue;

        char ip[64] = {};
        if (enet_address_get_host_ip(&from, ip, sizeof(ip)) != 0) continue;
        LanGame g;
        try {
            g.address = std::string(ip) + ":" + std::to_string(std::stoi(parts[1]));
            g.players = std::stoi(parts[3]);
            g.maxPlayers = std::stoi(parts[4]);
        } catch (...) { continue; }
        g.hostName = parts[2].substr(0, 32);
        g.motd = msg.substr(start, net::kMaxMotdLength);
        g.lastSeen = now;
        m_games[g.address] = g;
    }
    for (auto it = m_games.begin(); it != m_games.end();) {
        if (now - it->second.lastSeen > 5.0) it = m_games.erase(it); else ++it;
    }
}

std::vector<LanGame> LanScanner::games() const {
    std::vector<LanGame> out;
    auto portOf = [](const std::string& a) { return a.substr(a.rfind(':') + 1); };
    for (const auto& [addr, g] : m_games) {
        // A game on this PC can arrive twice (broadcast + 127.0.0.1): keep the network address
        if (addr.rfind("127.0.0.1:", 0) == 0) {
            bool dup = false;
            for (const auto& [other, og] : m_games) {
                if (other != addr && portOf(other) == portOf(addr) && og.hostName == g.hostName) dup = true;
            }
            if (dup) continue;
        }
        out.push_back(g);
    }
    std::sort(out.begin(), out.end(), [](const LanGame& a, const LanGame& b) { return a.address < b.address; });
    return out;
}
