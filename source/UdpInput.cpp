#include "UdpInput.h"
#include <algorithm>
#include <chrono>
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
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

UdpInput g_UdpInput;

namespace {
constexpr size_t kMaxPorts = 4;             // ports listened on at once
constexpr size_t kQueue = 256;              // packets kept per port until a mod takes them
constexpr size_t kMaxPacket = 1500;         // bigger ones are cut (no sensible LAN packet is)
constexpr size_t kMaxReply = 1472;
constexpr double kSenderWindow = 10.0;      // replies only to who sent in the last 10 s
constexpr int kRepliesPerSecond = 20;

#ifdef _WIN32
void ensureWsa() {
    static bool ready = false;
    if (ready) return;
    WSADATA d;
    ready = WSAStartup(MAKEWORD(2, 2), &d) == 0;
}
void closeSocket(uintptr_t s) { closesocket(static_cast<SOCKET>(s)); }
#else
void closeSocket(int s) { ::close(s); }
#endif
} // namespace

UdpInput::~UdpInput() { closeAll(); }

double UdpInput::now() {
    static const auto start = std::chrono::steady_clock::now();
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

void UdpInput::closeAll() {
    for (auto& [port, st] : m_sockets) closeSocket(st.socket);
    m_sockets.clear();
}

std::vector<int> UdpInput::listeningPorts() const {
    std::vector<int> out;
    for (const auto& [port, st] : m_sockets) out.push_back(port);
    return out;
}

void UdpInput::setMode(Mode mode, const std::set<int>& allowedPorts) {
    if (mode == m_mode && allowedPorts == m_allowed) return;
    // a different setting: what mods opened is closed (they ask again under the new one)
    closeAll();
    m_mode = mode;
    m_allowed.clear();
    for (int p : allowedPorts) if (p >= 1 && p <= 65535) m_allowed.insert(p);
    m_error.clear();
    std::cout << "[Network input] " << (mode == Mode::Off ? "off" : mode == Mode::Local ? "this computer only" : "from the network")
              << std::endl;
}

bool UdpInput::listen(int port, std::string& why) {
    if (m_mode == Mode::Off) { why = "Network input is off (the player turns it on in Settings)"; return false; }
    if (!allowed(port)) { why = "port " + std::to_string(port) + " isn't one the player allows (settings.txt: udp_ports)"; return false; }
    if (m_sockets.count(port)) return true;
    if (m_sockets.size() >= kMaxPorts) { why = "already listening on " + std::to_string(kMaxPorts) + " ports"; return false; }
#ifdef _WIN32
    ensureWsa();
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET) { why = "can't open a network socket"; return false; }
    BOOL yes = TRUE;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&yes), sizeof(yes));
    setsockopt(s, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char*>(&yes), sizeof(yes));
    u_long nb = 1;
    ioctlsocket(s, FIONBIO, &nb);
    const uintptr_t sock = static_cast<uintptr_t>(s);
#else
    int s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s < 0) { why = "can't open a network socket"; return false; }
    int yes = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
#ifdef SO_REUSEPORT
    setsockopt(s, SOL_SOCKET, SO_REUSEPORT, &yes, sizeof(yes));
#endif
    setsockopt(s, SOL_SOCKET, SO_BROADCAST, &yes, sizeof(yes));
    fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK);
    const int sock = s;
#endif
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(static_cast<uint16_t>(port));
    a.sin_addr.s_addr = htonl(m_mode == Mode::Local ? INADDR_LOOPBACK : INADDR_ANY);
    if (bind(s, reinterpret_cast<sockaddr*>(&a), sizeof(a)) != 0) {
        closeSocket(sock);
        why = m_error = "port " + std::to_string(port) + " is in use by another program";
        std::cerr << "[Network input] Can't listen: " << why << std::endl;
        return false;
    }
    PortState st;
    st.socket = sock;
    m_sockets.emplace(port, std::move(st));
    std::cout << "[Network input] Listening on UDP port " << port
              << (m_mode == Mode::Local ? " (127.0.0.1)" : " (every network card)") << std::endl;
    return true;
}

void UdpInput::poll() {
    const double now = UdpInput::now();
    uint8_t buf[2048];
    for (auto& [port, st] : m_sockets) {
        for (int i = 0; i < 512; ++i) {
            sockaddr_in from{};
#ifdef _WIN32
            int fromLen = sizeof(from);
            const int n = recvfrom(static_cast<SOCKET>(st.socket), reinterpret_cast<char*>(buf), sizeof(buf), 0,
                                   reinterpret_cast<sockaddr*>(&from), &fromLen);
#else
            socklen_t fromLen = sizeof(from);
            const ssize_t n = recvfrom(st.socket, buf, sizeof(buf), 0, reinterpret_cast<sockaddr*>(&from), &fromLen);
#endif
            if (n <= 0) break;
            char ip[INET_ADDRSTRLEN] = {};
            inet_ntop(AF_INET, &from.sin_addr, ip, sizeof(ip));
            Packet p;
            p.data.assign(reinterpret_cast<const char*>(buf), std::min(static_cast<size_t>(n), kMaxPacket));
            p.from = ip;
            p.fromPort = ntohs(from.sin_port);
            p.time = now;
            st.senders[p.from] = now;
            if (st.queue.size() >= kQueue) st.queue.pop_front();       // (nobody's reading: keep the newest)
            st.queue.push_back(std::move(p));
            ++m_received;
        }
        // forget senders gone quiet (a flood of addresses can't grow this for long)
        if (st.senders.size() > 256) {
            for (auto it = st.senders.begin(); it != st.senders.end();)
                it = (now - it->second > kSenderWindow) ? st.senders.erase(it) : std::next(it);
        }
    }
}

std::vector<UdpInput::Packet> UdpInput::take(int port, size_t max) {
    std::vector<Packet> out;
    auto it = m_sockets.find(port);
    if (it == m_sockets.end()) return out;
    auto& q = it->second.queue;
    while (!q.empty() && out.size() < max) { out.push_back(std::move(q.front())); q.pop_front(); }
    return out;
}

bool UdpInput::reply(int port, const std::string& toIp, int toPort, const std::string& data, std::string& why) {
    const double now = UdpInput::now();
    auto it = m_sockets.find(port);
    if (it == m_sockets.end()) { why = "not listening on that port"; return false; }
    PortState& st = it->second;
    auto s = st.senders.find(toIp);
    if (s == st.senders.end() || now - s->second > kSenderWindow) { why = "only to an address that sent a packet here just now"; return false; }
    if (toPort < 1 || toPort > 65535) { why = "bad port"; return false; }
    if (data.empty() || data.size() > kMaxReply) { why = "a reply is 1 to 1472 bytes"; return false; }
    if (now - st.replyWindow >= 1.0) { st.replyWindow = now; st.repliesInWindow = 0; }
    if (++st.repliesInWindow > kRepliesPerSecond) { why = "too many replies (20 a second at most)"; return false; }
    sockaddr_in to{};
    to.sin_family = AF_INET;
    to.sin_port = htons(static_cast<uint16_t>(toPort));
    if (inet_pton(AF_INET, toIp.c_str(), &to.sin_addr) != 1) { why = "bad address"; return false; }
#ifdef _WIN32
    const int n = sendto(static_cast<SOCKET>(st.socket), data.data(), static_cast<int>(data.size()), 0,
                         reinterpret_cast<const sockaddr*>(&to), sizeof(to));
#else
    const ssize_t n = sendto(st.socket, data.data(), data.size(), 0, reinterpret_cast<const sockaddr*>(&to), sizeof(to));
#endif
    if (n < 0) { why = "couldn't send"; return false; }
    return true;
}
