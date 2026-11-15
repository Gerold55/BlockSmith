#pragma once
// engine.register_block(...) - shared by the game and the headless server, so both agree
// on every block's properties (solid, transparent, walkable...). Movement prediction
// depends on that: if the server thought a block was solid and the game didn't, players
// would keep getting pulled back.
//
// Textures are loaded through `loadTexture` (the game uploads them to the GPU; the server
// passes a function that does nothing and returns -1).

#include <functional>
#include <string>
#include <sol/sol.hpp>

using TextureLoader = std::function<int(const std::string& path)>;   // -> texture layer, or -1

void registerBlockDefinition(const std::string& name, float r, float g, float b,
                             sol::object texObj, sol::object overlayObj,
                             sol::optional<sol::table> propsTable, const TextureLoader& loadTexture);
