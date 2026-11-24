#pragma once
// Network input for client mods (Lua, in clientmods/: client.udp_*): raw UDP packets from the
// network, as data. The engine knows no protocol: a client mod reads the packets and decides what
// they mean (clientmods/artnet reads Art-Net DMX from lighting software, and passes it to the
// server's mods with client.send_to_server). Game and server mods have no network access.
//
//   * Off unless the player turns it on (Settings > Video > Network input): "This computer"
//     receives only from programs on the same PC (127.0.0.1); "Network" from any network card.
//     Mods can't turn it on.
//   * Only the ports the player allows (settings.txt: udp_ports = 6454, comma-separated; up to 4
//     at once). A client mod asks to listen on one of them: client.udp_listen(port).
//   * Data only: a packet is a string of bytes and nothing else; nothing in it is ever run.
//   * Replies (some protocols answer "who's there?" polls) only go back to an address that sent
//     this game a packet in the last 10 seconds, on the same port, small (1472 bytes at most) and
//     at most 20 a second: a mod can't use it to send to anywhere it likes.
//
// Polled once a frame from the main loop (no thread).

#include <cstdint>
#include <deque>
#include <map>
#include <set>
#include <string>
#include <vector>

class UdpInput {
public:
    enum class Mode { Off = 0, Local = 1, Network = 2 };

    struct Packet {
        std::string data;           // the bytes
        std::string from;           // the sender's address ("192.168.1.20")
        int fromPort = 0;
        double time = 0.0;          // seconds (UdpInput::now())
    };
    static double now();            // seconds since the game started (steady)

    ~UdpInput();
    // The player's setting: Off closes every port; the allowed ports are the only ones mods may use
    void setMode(Mode mode, const std::set<int>& allowedPorts);
    Mode mode() const { return m_mode; }
    const std::set<int>& allowedPorts() const { return m_allowed; }
    bool allowed(int port) const { return m_allowed.count(port) != 0; }

    // A mod asks to listen on a port: false + why if it can't (off, not allowed, in use...)
    bool listen(int port, std::string& why);
    bool listening(int port) const { return m_sockets.count(port) != 0; }
    std::vector<int> listeningPorts() const;
    void closeAll();                // (mods reloaded: they ask again)

    // Reads what has arrived on every port (a few hundred packets at most per call)
    void poll();
    // The packets that came on a port since the last call (oldest first, up to max)
    std::vector<Packet> take(int port, size_t max);
    // A reply to someone who sent a packet on that port (see above)
    bool reply(int port, const std::string& toIp, int toPort, const std::string& data, std::string& why);

    uint64_t received() const { return m_received; }
    const std::string& lastError() const { return m_error; }

private:
#ifdef _WIN32
    using Socket = uintptr_t;
#else
    using Socket = int;
#endif
    struct PortState {
        Socket socket{};
        std::deque<Packet> queue;
        std::map<std::string, double> senders;   // address -> when it last sent here
        double replyWindow = 0.0;
        int repliesInWindow = 0;
    };
    Mode m_mode = Mode::Off;
    std::set<int> m_allowed;
    std::map<int, PortState> m_sockets;
    uint64_t m_received = 0;
    std::string m_error;
};

extern UdpInput g_UdpInput;
