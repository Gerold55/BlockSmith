#pragma once
// The order mods run in, the same in the game and on the dedicated server:
//   1. global mods (mods/) before the game's mods (games/<game>/mods/)
//   2. inside each, alphabetically by folder name (any capitals), not in whatever order the
//      file system happens to list them
//   3. a mod whose mod.conf says `depends = a, b` (or `optional_depends = ...`) runs after
//      those mods, wherever they are
// Mods chain hooks (local previous = on_update ...), so a fixed order means the same mod
// always gets the last word, on every PC.

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace modorder {

namespace fs = std::filesystem;

inline std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// Sorts the files found in one mods folder (from `start` to the end) alphabetically.
inline void sortRoot(std::vector<fs::path>& files, size_t start) {
    if (start >= files.size()) return;
    std::stable_sort(files.begin() + static_cast<std::ptrdiff_t>(start), files.end(),
                     [](const fs::path& a, const fs::path& b) {
                         return lower(a.generic_string()) < lower(b.generic_string());
                     });
}

// "a, b c" -> { "a", "b", "c" }   (commas or spaces; a trailing '?' means optional, as in some Luanti mods)
inline std::vector<std::string> splitList(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    auto flush = [&] {
        while (!cur.empty() && cur.back() == '?') cur.pop_back();
        if (!cur.empty()) out.push_back(cur);
        cur.clear();
    };
    for (char c : s) {
        if (c == ',' || std::isspace(static_cast<unsigned char>(c))) flush();
        else cur += c;
    }
    flush();
    return out;
}

// depends + optional_depends from a mod folder's mod.conf
inline std::vector<std::string> readDepends(const fs::path& modFolder, std::vector<std::string>* required = nullptr) {
    std::vector<std::string> all;
    std::ifstream in(modFolder / "mod.conf");
    std::string line;
    while (std::getline(in, line)) {
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq), value = line.substr(eq + 1);
        key.erase(std::remove_if(key.begin(), key.end(), [](unsigned char c) { return std::isspace(c); }), key.end());
        if (key != "depends" && key != "optional_depends") continue;
        for (const auto& d : splitList(value)) {
            all.push_back(d);
            if (required && key == "depends") required->push_back(d);
        }
    }
    return all;
}

// Moves each mod's init.lua after the init.lua of the mods it depends on. Mods with no
// dependencies keep their place; loose .lua files are not moved. Missing dependencies and
// loops are reported in the console. A loop (b needs c, c needs b) can't be ordered: its
// members load together, in their usual order, as soon as everything else they need has
// loaded - and mods that depend on the loop (a needs b) still load after it.
inline void applyDepends(std::vector<fs::path>& files) {
    std::vector<size_t> slots;                 // where the init.lua files are in `files`
    std::vector<fs::path> inits;
    for (size_t i = 0; i < files.size(); ++i) {
        if (files[i].filename() == "init.lua") { slots.push_back(i); inits.push_back(files[i]); }
    }
    if (inits.size() < 2) return;

    std::map<std::string, size_t> byName;      // mod name -> index in inits (first one wins)
    for (size_t i = 0; i < inits.size(); ++i) byName.emplace(inits[i].parent_path().filename().string(), i);

    std::vector<std::vector<size_t>> needs(inits.size());
    for (size_t i = 0; i < inits.size(); ++i) {
        std::vector<std::string> required;
        for (const auto& d : readDepends(inits[i].parent_path(), &required)) {
            const auto it = byName.find(d);
            if (it != byName.end()) { if (it->second != i) needs[i].push_back(it->second); continue; }
            if (std::find(required.begin(), required.end(), d) != required.end()) {
                std::cout << "[Mod] " << inits[i].parent_path().filename().string() << " depends on '" << d
                          << "', which isn't installed (or is switched off)" << std::endl;
            }
        }
    }

    std::vector<fs::path> ordered;
    std::vector<bool> done(inits.size(), false);
    while (ordered.size() < inits.size()) {
        bool progress = false;
        for (size_t i = 0; i < inits.size(); ++i) {
            if (done[i]) continue;
            bool ready = true;
            for (size_t n : needs[i]) if (!done[n]) { ready = false; break; }
            if (!ready) continue;
            done[i] = true;
            ordered.push_back(inits[i]);
            progress = true;
            break;                              // restart: keeps the alphabetical order otherwise
        }
        if (!progress) {
            // Stuck: every unfinished mod waits for another unfinished one, so there is a loop.
            // Load one whole loop - and only a loop whose members need nothing else that's still
            // unfinished (one always exists: follow the dependencies far enough and you reach a
            // loop that depends only on finished mods and itself). Mods that merely depend on a
            // loop, or loops that depend on other loops, wait their turn as usual.
            const size_t n = inits.size();
            // reach[i][j]: unfinished mod i needs unfinished mod j, directly or through others
            std::vector<std::vector<char>> reach(n, std::vector<char>(n, 0));
            for (size_t i = 0; i < n; ++i) {
                if (done[i]) continue;
                std::vector<size_t> stack;
                for (size_t d : needs[i]) if (!done[d]) stack.push_back(d);
                while (!stack.empty()) {
                    const size_t x = stack.back();
                    stack.pop_back();
                    if (reach[i][x]) continue;
                    reach[i][x] = 1;
                    for (size_t d : needs[x]) if (!done[d] && !reach[i][d]) stack.push_back(d);
                }
            }
            std::vector<size_t> cycle;
            for (size_t i = 0; i < n && cycle.empty(); ++i) {
                if (done[i] || !reach[i][i]) continue;            // not in a loop
                std::vector<size_t> group;                        // i's loop: mods it needs that need it back
                for (size_t j = 0; j < n; ++j) if (!done[j] && (j == i || (reach[i][j] && reach[j][i]))) group.push_back(j);
                bool waitsOnOthers = false;                       // anything outside the loop still pending?
                for (size_t g : group) {
                    for (size_t d : needs[g]) {
                        if (!done[d] && std::find(group.begin(), group.end(), d) == group.end()) { waitsOnOthers = true; break; }
                    }
                    if (waitsOnOthers) break;
                }
                if (!waitsOnOthers) cycle = group;                // already in their usual order
            }
            if (cycle.empty()) {                                  // can't happen; never loop forever
                for (size_t i = 0; i < n; ++i) if (!done[i]) { cycle.push_back(i); break; }
            }
            std::string names;
            for (size_t k : cycle) names += (names.empty() ? "" : ", ") + inits[k].parent_path().filename().string();
            std::cout << "[Mod] Dependency loop between " << names
                      << " - loading them together, in their usual order" << std::endl;
            for (size_t k : cycle) {
                done[k] = true;
                ordered.push_back(inits[k]);
            }
        }
    }
    for (size_t k = 0; k < slots.size(); ++k) files[slots[k]] = ordered[k];
}

} // namespace modorder
