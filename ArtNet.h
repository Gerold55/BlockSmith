#pragma once
// Art-Net input: DMX from lighting software or a console (QLC+, grandMA onPC, Chamsys MagicQ,
// Lightkey, a real desk with an Art-Net node...) into the game, for mods to read (Lua: dmx.*).
//
//   * Off unless the player turns it on (Settings > Lighting > Art-Net input): "This computer"
//     listens on 127.0.0.1 only (software on the same PC); "Network" listens on every network
//     card (a desk or another PC; the system may ask to allow it through the firewall).
//   * UDP port 6454 (settings.txt: artnet_port). ArtDmx packets are read into the latest 512
//     channels of each universe; ArtPoll is answered (on "Network"), so consoles can find the game.
//   * Data only: packets are read as numbers and nothing else. Mods can read the channels but
//     can't open, move or turn on the listener.
//
// Polled once a frame from the main loop (no thread): poll() reads what has arrived.

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

class ArtNetInput {
public:
    enum class Mode { Off = 0, Local = 1, Network = 2 };

    ~ArtNetInput();
    // Starts (or restarts) listening, or stops (Off). Returns false if the port couldn't be
    // opened (in use by another program that doesn't share it); lastError() says why.
    bool setMode(Mode mode, int port = 6454);
    Mode mode() const { return m_mode; }
    bool listening() const { return m_socket != kNoSocket; }
    int port() const { return m_port; }
    const std::string& lastError() const { return m_error; }

    // Reads every packet that has arrived (a few hundred at most per call)
    void poll(double now);

    struct Universe {
        std::array<uint8_t, 512> data{};
        uint64_t frames = 0;        // packets received (goes up with each one)
        double lastTime = -1.0;     // when the last one came (poll's "now")
        int length = 0;             // channels in the last packet
        std::string source;         // who sent it (address)
    };
    // nullptr if nothing has come for it
    const Universe* universe(int portAddress) const;
    const std::map<int, Universe>& universes() const { return m_universes; }
    uint64_t packets() const { return m_packets; }
    uint64_t rejected() const { return m_rejected; }

    // Parses one packet (shared with tests): true if it was ArtDmx and was applied
    bool handlePacket(const uint8_t* p, size_t n, const std::string& from, double now, bool* wasPoll = nullptr);

private:
#ifdef _WIN32
    using Socket = uintptr_t;
#else
    using Socket = int;
#endif
    static constexpr Socket kNoSocket = static_cast<Socket>(~static_cast<Socket>(0));
    Socket m_socket = kNoSocket;
    Mode m_mode = Mode::Off;
    int m_port = 6454;
    std::string m_error;
    std::map<int, Universe> m_universes;
    uint64_t m_packets = 0, m_rejected = 0;
    void close();
    void replyToPoll(const void* addr, int addrLen);
};

extern ArtNetInput g_ArtNet;
