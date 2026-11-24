#include "ArtNet.h"
#include <algorithm>
#include <cstring>
#include <iostream>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

ArtNetInput g_ArtNet;

namespace {
constexpr uint16_t kOpPoll = 0x2000, kOpPollReply = 0x2100, kOpDmx = 0x5000;
constexpr size_t kMaxUniverses = 64;          // (a flood of made-up universes can't fill memory)
const char kId[8] = { 'A', 'r', 't', '-', 'N', 'e', 't', 0 };

#ifdef _WIN32
bool g_wsaReady = false;
void ensureWsa() {
    if (g_wsaReady) return;
    WSADATA d;
    g_wsaReady = WSAStartup(MAKEWORD(2, 2), &d) == 0;
}
#endif
} // namespace

ArtNetInput::~ArtNetInput() { close(); }

void ArtNetInput::close() {
    if (m_socket == kNoSocket) return;
#ifdef _WIN32
    closesocket(static_cast<SOCKET>(m_socket));
#else
    ::close(m_socket);
#endif
    m_socket = kNoSocket;
}

bool ArtNetInput::setMode(Mode mode, int port) {
    port = std::clamp(port, 1, 65535);
    if (mode == m_mode && port == m_port && (mode == Mode::Off || listening())) return true;
    close();
    m_mode = mode;
    m_port = port;
    m_error.clear();
    if (mode == Mode::Off) {
        std::cout << "[Art-Net] Input off" << std::endl;
        return true;
    }
#ifdef _WIN32
    ensureWsa();
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET) { m_error = "can't open a network socket"; return false; }
    // shared with other Art-Net programs on this computer, broadcasts allowed, non-blocking
    BOOL yes = TRUE;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&yes), sizeof(yes));
    setsockopt(s, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char*>(&yes), sizeof(yes));
    u_long nb = 1;
    ioctlsocket(s, FIONBIO, &nb);
#else
    int s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s < 0) { m_error = "can't open a network socket"; return false; }
    int yes = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
#ifdef SO_REUSEPORT
    setsockopt(s, SOL_SOCKET, SO_REUSEPORT, &yes, sizeof(yes));
#endif
    setsockopt(s, SOL_SOCKET, SO_BROADCAST, &yes, sizeof(yes));
    fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK);
#endif
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(static_cast<uint16_t>(port));
    a.sin_addr.s_addr = htonl(mode == Mode::Local ? INADDR_LOOPBACK : INADDR_ANY);
    if (bind(s, reinterpret_cast<sockaddr*>(&a), sizeof(a)) != 0) {
#ifdef _WIN32
        closesocket(s);
#else
        ::close(s);
#endif
        m_error = "port " + std::to_string(port) + " is in use by another program";
        std::cerr << "[Art-Net] Can't listen: " << m_error << std::endl;
        return false;
    }
    m_socket = static_cast<Socket>(s);
    std::cout << "[Art-Net] Listening on " << (mode == Mode::Local ? "127.0.0.1" : "every network card")
              << ", UDP port " << port << std::endl;
    return true;
}

bool ArtNetInput::handlePacket(const uint8_t* p, size_t n, const std::string& from, double now, bool* wasPoll) {
    if (wasPoll) *wasPoll = false;
    if (n < 12 || std::memcmp(p, kId, 8) != 0) { ++m_rejected; return false; }
    const uint16_t op = static_cast<uint16_t>(p[8] | (p[9] << 8));     // (little-endian)
    if (op == kOpPoll) { if (wasPoll) *wasPoll = true; return false; }
    if (op != kOpDmx) return false;                                      // (other Art-Net packets: ignored)
    if (n < 18) { ++m_rejected; return false; }
    const int portAddress = (p[15] & 0x7F) << 8 | p[14];                 // Net (7 bits), Sub-Net + Universe
    const int length = (p[16] << 8) | p[17];                             // (big-endian)
    if (length < 1 || length > 512 || n < 18 + static_cast<size_t>(length)) { ++m_rejected; return false; }
    auto it = m_universes.find(portAddress);
    if (it == m_universes.end()) {
        if (m_universes.size() >= kMaxUniverses) {
            // full: make room by forgetting the one that's gone longest without a packet (so
            // stray or junk packets can't keep a console's real universes out)
            auto oldest = m_universes.begin();
            for (auto u = m_universes.begin(); u != m_universes.end(); ++u)
                if (u->second.lastTime < oldest->second.lastTime) oldest = u;
            m_universes.erase(oldest);
        }
        it = m_universes.emplace(portAddress, Universe{}).first;
    }
    Universe& u = it->second;
    std::memcpy(u.data.data(), p + 18, static_cast<size_t>(length));
    if (length < u.length) std::fill(u.data.begin() + length, u.data.begin() + u.length, 0);
    u.length = length;
    ++u.frames;
    u.lastTime = now;
    u.source = from;
    ++m_packets;
    return true;
}

void ArtNetInput::poll(double now) {
    if (m_socket == kNoSocket) return;
    uint8_t buf[1024];
    for (int i = 0; i < 512; ++i) {
        sockaddr_in from{};
#ifdef _WIN32
        int fromLen = sizeof(from);
        const int n = recvfrom(static_cast<SOCKET>(m_socket), reinterpret_cast<char*>(buf), sizeof(buf), 0,
                               reinterpret_cast<sockaddr*>(&from), &fromLen);
#else
        socklen_t fromLen = sizeof(from);
        const ssize_t n = recvfrom(m_socket, buf, sizeof(buf), 0, reinterpret_cast<sockaddr*>(&from), &fromLen);
#endif
        if (n <= 0) break;                                   // nothing more (or an error: try next frame)
        char ip[INET_ADDRSTRLEN] = {};
        inet_ntop(AF_INET, &from.sin_addr, ip, sizeof(ip));
        bool wasPoll = false;
        handlePacket(buf, static_cast<size_t>(n), ip, now, &wasPoll);
        if (wasPoll && m_mode == Mode::Network) replyToPoll(&from, static_cast<int>(fromLen));
    }
}

// ArtPollReply: "a node called BlockSmith, taking universes 0-3" - consoles list it, and some
// only send to nodes that answered
void ArtNetInput::replyToPoll(const void* addr, int addrLen) {
    uint8_t r[239] = {};
    std::memcpy(r, kId, 8);
    r[8] = kOpPollReply & 0xFF; r[9] = kOpPollReply >> 8;
    // IP address (10..13): left 0 - consoles use where the reply came from
    r[14] = 0x36; r[15] = 0x19;                              // port 6454, little-endian
    r[16] = 0; r[17] = 1;                                    // firmware version
    r[18] = 0; r[19] = 0;                                    // Net, Sub-Net
    r[20] = 0; r[21] = 0;                                    // OEM (unknown)
    r[23] = 0xD0;                                            // status: normal
    std::memcpy(r + 26, "BlockSmith", 10);                   // short name (18 bytes)
    const char* longName = "BlockSmith Art-Net input";
    std::memcpy(r + 44, longName, std::strlen(longName));    // long name (64 bytes)
    r[172] = 0; r[173] = 4;                                  // 4 ports
    for (int i = 0; i < 4; ++i) {
        r[174 + i] = 0x80;                                   // port type: takes DMX from the network
        r[182 + i] = 0x80;                                   // output status: data is being taken
        r[190 + i] = static_cast<uint8_t>(i);                // universes 0..3
    }
    r[200] = 0x00;                                           // style: a node
    sockaddr_in to = *static_cast<const sockaddr_in*>(addr);
    to.sin_port = htons(6454);
    (void)addrLen;
#ifdef _WIN32
    sendto(static_cast<SOCKET>(m_socket), reinterpret_cast<const char*>(r), sizeof(r), 0,
           reinterpret_cast<const sockaddr*>(&to), sizeof(to));
#else
    sendto(m_socket, r, sizeof(r), 0, reinterpret_cast<const sockaddr*>(&to), sizeof(to));
#endif
}

const ArtNetInput::Universe* ArtNetInput::universe(int portAddress) const {
    auto it = m_universes.find(portAddress);
    return it == m_universes.end() ? nullptr : &it->second;
}
