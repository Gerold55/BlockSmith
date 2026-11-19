#pragma once
// WebLink: web addresses in chat (a server's voting site, its forum...).
//
// The chat finds them in its lines and draws them as links; clicking one asks first ("Open this
// link?" with the address in full), and only then does the game hand it to the player's own
// browser. The game never downloads or runs anything from the link itself.
//
// Only plain http:// and https:// addresses are opened, and only ones made of ordinary URL
// characters: no spaces, quotes or anything a command line or the system could read as more
// than an address. The browser is started directly (no shell in between).

#include <string>
#include <utility>
#include <vector>

namespace weblink {

// Where links are in a line of text: { start, length } pairs. Finds "http://...", "https://..."
// and "www...."; a link ends at a space, and full stops, commas, brackets... at its end are left
// out ("Vote at https://example.com/vote." -> without the full stop).
std::vector<std::pair<size_t, size_t>> find(const std::string& text);

// The address to open for what was found: "www.example.com" becomes "https://www.example.com".
// Empty if it isn't one we'd open (see isSafe).
std::string normalize(const std::string& link);

// http(s), a plain host name (letters, digits, - and .; an optional :port; no "name@" in front, which
// would hide where it really goes), then only URL characters: letters, digits and - . _ ~ : / ? # [ ] @ ! & ( ) * + , ; = %
bool isSafe(const std::string& url);

// The host, for showing the player where the link really goes ("vote.example.com")
std::string host(const std::string& url);

// Opens the address in the player's browser. False if it isn't safe or nothing could open it.
bool open(const std::string& url);

} // namespace weblink
