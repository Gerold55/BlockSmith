#pragma once
// Player passwords: per-server challenge-response (protocol 8).
//
// The problem it solves: before, the game sent every server the same token (a hash of the
// password). Any server you joined - or anyone watching the network - could replay that token
// to log in as you on every other server.
//
// Now:
//   playerKey  = PBKDF2-HMAC-SHA256(token, "blocksmith-player-v1:" + lower(name), kPbkdf2Rounds)
//                computed on the player's PC from the locally stored token; never sent anywhere.
//                (Slow on purpose: a server that knows a serverKey can't cheaply guess passwords.)
//   serverKey  = HMAC(playerKey, "blocksmith-server-key-v1\n" + serverId)
//                one per server: serverId is that server's own random identity. This is the only
//                secret a server ever gets (once, when the account is created), and it is useless
//                on any other server, whose serverId differs.
//   response   = HMAC(serverKey, "blocksmith-login-v1\n" + serverId + "\n" + nonce + "\n" + lower(name))
//                sent at each login, answering a fresh random nonce the server just made: it
//                proves the player knows serverKey without sending it, and it can't be replayed
//                (the nonce is single-use) or used on another server (serverId and serverKey differ).
//
// Handshake: C_HELLO (no secret) -> S_CHALLENGE { serverId, nonce, accountExists }
//            -> C_AUTH { response, registerKey } -> S_WELCOME or S_KICK.
// registerKey (= serverKey) is only sent when the server has no account for the name yet.
//
// Limits (no public-key crypto here): the first registration sends that server's key once, and
// the connection isn't encrypted, so someone watching it could take over that one account on
// that one server - never others. Players' games remember each server's identity (per address)
// and won't answer a server whose identity changed, which stops a different server answering at
// a known address from collecting responses for it.

#include "Sha256.h"
#include <cstdint>
#include <random>
#include <string>

namespace auth {

constexpr int kPbkdf2Rounds = 60000;

inline std::string toLower(std::string s) {
    for (char& c : s) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}

inline std::string toHex(const std::string& bytes) {
    static const char* hex = "0123456789abcdef";
    std::string out;
    out.reserve(bytes.size() * 2);
    for (unsigned char c : bytes) { out.push_back(hex[c >> 4]); out.push_back(hex[c & 15]); }
    return out;
}

// "" if `hex` isn't an even-length hex string
inline std::string fromHex(const std::string& hex) {
    if (hex.size() % 2) return "";
    auto val = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    std::string out;
    out.reserve(hex.size() / 2);
    for (size_t i = 0; i < hex.size(); i += 2) {
        const int a = val(hex[i]), b = val(hex[i + 1]);
        if (a < 0 || b < 0) return "";
        out.push_back(static_cast<char>(a * 16 + b));
    }
    return out;
}

inline std::string sha256Raw(const std::string& data) { return fromHex(sha256Hex(data)); }

// HMAC-SHA256 (RFC 2104), raw 32 bytes
inline std::string hmac(const std::string& keyIn, const std::string& msg) {
    std::string key = keyIn.size() > 64 ? sha256Raw(keyIn) : keyIn;
    key.resize(64, '\0');
    std::string ipad(64, '\0'), opad(64, '\0');
    for (int i = 0; i < 64; ++i) {
        ipad[i] = static_cast<char>(key[i] ^ 0x36);
        opad[i] = static_cast<char>(key[i] ^ 0x5c);
    }
    return sha256Raw(opad + sha256Raw(ipad + msg));
}

// PBKDF2-HMAC-SHA256 (RFC 8018), one 32-byte block
inline std::string pbkdf2(const std::string& password, const std::string& salt, int rounds) {
    std::string u = hmac(password, salt + std::string("\x00\x00\x00\x01", 4));
    std::string out = u;
    for (int i = 1; i < rounds; ++i) {
        u = hmac(password, u);
        for (size_t k = 0; k < out.size(); ++k) out[k] = static_cast<char>(out[k] ^ u[k]);
    }
    return out;
}

// Compares without stopping at the first difference (no timing hints)
inline bool equalConstantTime(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    unsigned char diff = 0;
    for (size_t i = 0; i < a.size(); ++i) diff |= static_cast<unsigned char>(a[i] ^ b[i]);
    return diff == 0;
}

// `bytes` random bytes as hex (std::random_device: the OS's random source)
inline std::string randomHex(int bytes) {
    std::random_device rd;
    std::string raw;
    for (int i = 0; i < bytes; i += 4) {
        const uint32_t v = rd();
        for (int k = 0; k < 4 && i + k < bytes; ++k) raw.push_back(static_cast<char>((v >> (k * 8)) & 0xff));
    }
    return toHex(raw);
}

// --- The scheme (see the top of this file) ---

// From the locally stored token (net::passwordToken(password)); slow - compute once per join.
inline std::string playerKey(const std::string& token, const std::string& name) {
    return pbkdf2(token, "blocksmith-player-v1:" + toLower(name), kPbkdf2Rounds);
}
// What one server stores for an account (hex)
inline std::string serverKey(const std::string& playerKeyRaw, const std::string& serverId) {
    return toHex(hmac(playerKeyRaw, "blocksmith-server-key-v1\n" + serverId));
}
// The answer to one challenge (hex)
inline std::string response(const std::string& serverKeyHex, const std::string& serverId,
                            const std::string& nonce, const std::string& name) {
    return toHex(hmac(fromHex(serverKeyHex),
                      "blocksmith-login-v1\n" + serverId + "\n" + nonce + "\n" + toLower(name)));
}
// Server side: is `resp` the right answer for this stored key and this challenge?
inline bool verify(const std::string& serverKeyHex, const std::string& serverId, const std::string& nonce,
                   const std::string& name, const std::string& resp) {
    if (serverKeyHex.size() != 64 || resp.size() != 64 || nonce.empty() || serverId.empty()) return false;
    return equalConstantTime(response(serverKeyHex, serverId, nonce, name), toLower(resp));
}

} // namespace auth
