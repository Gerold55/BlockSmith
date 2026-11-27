#pragma once

#include <iostream>
#include <stdexcept>
#include <string>
#include <sol/sol.hpp>

namespace basic_mapgen {

inline void installLuaApi(sol::state& lua) {
    const auto result = lua.safe_script(
        "_basic_mapgen = require('basic_mapgen').install(engine)", sol::script_pass_on_error);
    if (!result.valid()) {
        sol::error error = result;
        throw std::runtime_error(std::string("Couldn't install basic mapgen API: ") + error.what());
    }
}

inline bool installSimpleMapgen(sol::state& lua, bool report) {
    sol::table api = lua["_basic_mapgen"];
    sol::protected_function install = api["install_simple_mapgen"];
    if (!install.valid()) {
        if (report) std::cerr << "[Mapgen] Simple mapgen API is unavailable" << std::endl;
        return false;
    }

    const auto result = install();
    if (!result.valid()) {
        sol::error error = result;
        if (report) std::cerr << "[Mapgen] Couldn't install simple mapgen: " << error.what() << std::endl;
        return false;
    }
    if (result.return_count() == 0 || result.get_type() != sol::type::boolean) {
        if (report) std::cerr << "[Mapgen] Simple mapgen returned an invalid result" << std::endl;
        return false;
    }

    const bool installed = result.get<bool>(0);
    if (!installed && report && result.return_count() > 1) {
        const std::string reason = result.get<std::string>(1);
        if (reason == "no simple mapgen style was selected") {
            std::cout << "[Mapgen] No custom generator or simple mapgen selected; new chunks will be empty"
                      << std::endl;
        } else if (reason != "a custom generate_chunk already exists") {
            std::cerr << "[Mapgen] " << reason << std::endl;
        }
    } else if (installed && report) {
        std::cout << "[Mapgen] Using selected simple biome mapgen" << std::endl;
    }
    return installed;
}

} // namespace basic_mapgen
