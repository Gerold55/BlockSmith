local M = {}

local floor = math.floor
local sin = math.sin

local function finite(value)
    return type(value) == "number" and value == value and value ~= math.huge and value ~= -math.huge
end

function M.install(engine)
    local biomes = {}
    local biome_indices = {}
    local world_seed = 0
    local generator_installed = false
    local mapgen_style
    local superflat_layers

    engine._basic_mapgen_biomes = biomes
    local function saved_style()
        if type(engine.get_storage) ~= "function" then return nil end
        local value = engine.get_storage("world:mapgen_style")
        if type(value) == "string" and value ~= "" then return value end
        return nil
    end

    -- A world's style is chosen once and then never changes: chunks already made would no
    -- longer fit the new ones. So a style SAVED with the world always wins. set_mapgen_style
    -- gives the mod's choice for a world that has none yet (a new world); mods call it on
    -- every load, so it must not overwrite what an existing world saved.
    -- Returns true, and a second value naming the saved style when that one is used instead.
    engine.set_mapgen_style = function(style)
        if style ~= "v6" and style ~= "v7" and style ~= "superflat" then
            return false, "mapgen style must be 'v6', 'v7' or 'superflat'"
        end
        if generator_installed then
            return false, "the simple mapgen is already installed"
        end
        local saved = saved_style()
        if saved then
            mapgen_style = saved   -- (what the installer will use: they stay in agreement)
            if saved ~= style then return true, saved end
            return true
        end
        mapgen_style = style

        -- The world generator runs in a separate Lua state. Persist the selected
        -- style so that its copy of the mods can see the world's mapgen choice.
        if type(engine.set_storage) == "function" then
            engine.set_storage("world:mapgen_style", style)
        end

        return true
    end
    engine.set_superflat_layers = function(layers)
        if type(layers) ~= "table" or #layers == 0 or #layers > 16 then
            return false, "superflat layers must contain 1 to 16 entries"
        end
        local validated, total = {}, 0
        for i, layer in ipairs(layers) do
            if type(layer) ~= "table" or type(layer.node) ~= "string" or
                not layer.node:match("^[%w_:%-%.]+$") then
                return false, "layer " .. i .. " needs a valid node name"
            end
            if not finite(layer.depth) or layer.depth < 1 or layer.depth > 256 or
                layer.depth ~= floor(layer.depth) then
                return false, "layer " .. i .. " depth must be a whole number from 1 to 256"
            end
            total = total + layer.depth
            if total > 256 then return false, "superflat layers cannot exceed 256 blocks total" end
            validated[i] = { node = layer.node, depth = layer.depth }
        end
        superflat_layers = validated
        return true
    end

    engine.register_biome = function(def)
        if type(def) ~= "table" then
            return false, "expected a biome definition table"
        end
        if type(def.name) ~= "string" or def.name == "" then
            return false, "biome name must be a non-empty string"
        end
        if type(def.node_top) ~= "string" or def.node_top == "" then
            return false, "node_top must be a non-empty block name"
        end

        local function number_field(key, default, minimum, maximum)
            local value = def[key]
            if value == nil then value = default end
            if not finite(value) or value < minimum or value > maximum then
                return nil, key .. " must be a number from " .. minimum .. " to " .. maximum
            end
            return value
        end

        local function string_field(key, default)
            local value = def[key]
            if value == nil then value = default end
            if type(value) ~= "string" or value == "" then
                return nil, key .. " must be a non-empty block name"
            end
            return value
        end

        local filler, err = string_field("node_filler", "dirt")
        if not filler then return false, err end
        local stone
        stone, err = string_field("node_stone", "stone")
        if not stone then return false, err end
        local water
        water, err = string_field("node_water", "water")
        if not water then return false, err end

        local depth_top
        depth_top, err = number_field("depth_top", 1, 1, 64)
        if not depth_top then return false, err end
        local depth_filler
        depth_filler, err = number_field("depth_filler", 3, 0, 64)
        if not depth_filler then return false, err end
        local heat
        heat, err = number_field("heat_point", 50, 0, 100)
        if not heat then return false, err end
        local humidity
        humidity, err = number_field("humidity_point", 50, 0, 100)
        if not humidity then return false, err end
        local y_min
        y_min, err = number_field("y_min", 0, 0, 255)
        if not y_min then return false, err end
        local y_max
        y_max, err = number_field("y_max", 255, 0, 255)
        if not y_max then return false, err end
        if y_min > y_max then return false, "y_min must not be greater than y_max" end

        local biome = {
            name = def.name,
            node_top = def.node_top,
            node_filler = filler,
            node_stone = stone,
            node_water = water,
            depth_top = depth_top,
            depth_filler = depth_filler,
            heat_point = heat,
            humidity_point = humidity,
            y_min = y_min,
            y_max = y_max
        }

        local index = biome_indices[biome.name]
        if index then
            biomes[index] = biome
        else
            biomes[#biomes + 1] = biome
            biome_indices[biome.name] = #biomes
        end
        return true
    end

    local function hash(x, z, salt)
        local value = sin((x + world_seed * 0.1) * 12.9898 +
                          (z + world_seed * 0.1) * 78.233 + salt) * 43758.5453
        return value - floor(value)
    end

    local function value_noise(x, z, salt)
        local ix, iz = floor(x), floor(z)
        local fx, fz = x - ix, z - iz
        fx = fx * fx * fx * (fx * (fx * 6 - 15) + 10)
        fz = fz * fz * fz * (fz * (fz * 6 - 15) + 10)
        local n00 = hash(ix, iz, salt)
        local n10 = hash(ix + 1, iz, salt)
        local n01 = hash(ix, iz + 1, salt)
        local n11 = hash(ix + 1, iz + 1, salt)
        local a = n00 + (n10 - n00) * fx
        local b = n01 + (n11 - n01) * fx
        return a + (b - a) * fz
    end

    local function surface_height(x, z)
        local broad = value_noise(x * 0.003, z * 0.003, 17) - 0.5
        local detail = value_noise(x * 0.02, z * 0.02, 39) - 0.5
        local height
        if mapgen_style == "v6" then
            height = 58 + broad * 42 + detail * 14
        else
            local continents = value_noise(x * 0.0015, z * 0.0015, 53) - 0.5
            local ridges = 1 - math.abs(value_noise(x * 0.004, z * 0.004, 71) - 0.5) * 2
            height = 62 + continents * 70 + ridges * 38 + detail * 12
        end
        return math.max(1, math.min(254, floor(height)))
    end

    local function climate(x, z)
        local heat = value_noise(x * 0.0008, z * 0.0008, 101) * 0.7 +
                     value_noise(x * 0.003, z * 0.003, 103) * 0.3
        local humidity = value_noise(x * 0.0008, z * 0.0008, 211) * 0.7 +
                         value_noise(x * 0.003, z * 0.003, 223) * 0.3
        return heat * 100, humidity * 100
    end

    local function choose_biome(x, z)
        local height = surface_height(x, z)
        local heat, humidity = climate(x, z)
        local chosen, best_score

        local function consider(biome, require_height)
            if require_height and (height < biome.y_min or height > biome.y_max) then return end
            local dh = heat - biome.heat_point
            local du = humidity - biome.humidity_point
            local score = dh * dh + du * du
            if not best_score or score < best_score then
                chosen, best_score = biome, score
            end
        end

        for _, biome in ipairs(biomes) do consider(biome, true) end
        if not chosen then
            for _, biome in ipairs(biomes) do consider(biome, false) end
        end
        return chosen
    end

    local function resolve_block(name)
        local id = engine.get_block_id(name)
        if type(id) == "number" and id > 0 then return id end
        return nil
    end

    local function read_world_option(name)
        if type(engine.get_storage) ~= "function" then return nil end
        return engine.get_storage("world:" .. name)
    end

    local function selected_superflat_layers()
        local encoded = read_world_option("mapgen_layers")
        local layers = {}
        if type(encoded) == "string" and encoded ~= "" then
            for entry in encoded:gmatch("[^,]+") do
                local node, depth = entry:match("^(.-):(%d+)$")
                if not node or not node:match("^[%w_:%-%.]+$") then
                    return nil, "invalid superflat layer '" .. entry .. "'"
                end
                layers[#layers + 1] = { node = node, depth = tonumber(depth) }
                if #layers > 16 then return nil, "superflat layers cannot contain more than 16 entries" end
            end
        elseif superflat_layers then
            for i, layer in ipairs(superflat_layers) do
                layers[i] = { node = layer.node, depth = layer.depth }
            end
        else
            layers = {
                { node = "grass", depth = 1 },
                { node = "dirt", depth = 3 },
                { node = "bedrock", depth = 1 }
            }
        end

        if #layers == 0 then return nil, "superflat needs at least one layer" end
        local total = 0
        for i, layer in ipairs(layers) do
            if not layer.depth or layer.depth < 1 or layer.depth > 256 or layer.depth ~= floor(layer.depth) then
                return nil, "layer " .. i .. " depth must be a whole number from 1 to 256"
            end
            total = total + layer.depth
            if total > 256 then return nil, "superflat layers cannot exceed 256 blocks total" end
            layer.id = resolve_block(layer.node)
            if not layer.id then
                return nil, "superflat layer " .. i .. " references missing block '" .. layer.node .. "'"
            end
        end
        return layers, total
    end

    local function install_simple_mapgen()
        -- The saved world setting has priority; otherwise the style the mod chose in this Lua
        -- state, which is then saved so the world keeps it (and the generator's own Lua
        -- state sees the same one).
        local world_style = saved_style()
        if world_style then
            mapgen_style = world_style
        elseif mapgen_style and type(engine.set_storage) == "function" then
            engine.set_storage("world:mapgen_style", mapgen_style)
            world_style = mapgen_style
        end
        local world_superflat = world_style == "superflat" or mapgen_style == "superflat"
        if world_superflat then mapgen_style = "superflat" end
        _G._basic_mapgen_disable_caves = false
        if world_superflat then
            local layers, total = selected_superflat_layers()
            if not layers then return false, total end
            _G.generate_chunk = function(chunk)
                for lx = 0, 15 do
                    for lz = 0, 15 do
                        local y = 0
                        for i = #layers, 1, -1 do
                            local layer = layers[i]
                            for _ = 1, layer.depth do
                                chunk:set_block(lx, y, lz, layer.id)
                                y = y + 1
                            end
                        end
                    end
                end
            end
            local surface = total - 1
            _G.get_surface_height = function() return surface end
            _G.get_biome = function() return "plains" end
            _G._basic_mapgen_disable_caves = true
            _G.terrain_version = _G.terrain_version or 1
            generator_installed = true
            return true
        end
        if generator_installed then return true end
        if type(_G.generate_chunk) == "function" and not world_superflat then
            return false, "a custom generate_chunk already exists"
        end
        if not mapgen_style then
            return false, "no simple mapgen style was selected"
        end
        if mapgen_style ~= "v6" and mapgen_style ~= "v7" then
            return false, "unsupported simple mapgen style '" .. tostring(mapgen_style) .. "'"
        end
        if #biomes == 0 then
            return false, "no biomes were registered"
        end

        local bedrock = resolve_block("bedrock")
        for _, biome in ipairs(biomes) do
            biome.ids = {
                top = resolve_block(biome.node_top),
                filler = resolve_block(biome.node_filler),
                stone = resolve_block(biome.node_stone),
                water = resolve_block(biome.node_water)
            }
            if not biome.ids.top then
                return false, "biome '" .. biome.name .. "' references missing node_top '" .. biome.node_top .. "'"
            end
            if not biome.ids.filler then
                return false, "biome '" .. biome.name .. "' references missing node_filler '" .. biome.node_filler .. "'"
            end
            if not biome.ids.stone then
                return false, "biome '" .. biome.name .. "' references missing node_stone '" .. biome.node_stone .. "'"
            end
        end

        local previous_seed_hook = _G.set_world_seed
        _G.set_world_seed = function(seed)
            world_seed = finite(seed) and seed or 0
            if type(previous_seed_hook) == "function" then previous_seed_hook(seed) end
        end
        _G.get_biome = _G.get_biome or function(x, z)
            local biome = choose_biome(floor(x), floor(z))
            return biome and biome.name or "plains"
        end
        _G.generate_chunk = function(chunk, chunk_x, chunk_z)
            for lx = 0, 15 do
                for lz = 0, 15 do
                    local wx, wz = chunk_x * 16 + lx, chunk_z * 16 + lz
                    local height = surface_height(wx, wz)
                    local biome = choose_biome(wx, wz)
                    local ids = biome.ids

                    for y = 0, height do
                        local id
                        if y == 0 and bedrock then
                            id = bedrock
                        elseif y > height - biome.depth_top then
                            id = ids.top
                        elseif y > height - biome.depth_top - biome.depth_filler then
                            id = ids.filler
                        else
                            id = ids.stone
                        end
                        chunk:set_block(lx, y, lz, id)
                    end

                    if ids.water and height < 62 then
                        for y = height + 1, 62 do chunk:set_block(lx, y, lz, ids.water) end
                    end
                end
            end
        end
        _G.terrain_version = _G.terrain_version or 1
        generator_installed = true
        return true
    end

    return {
        install_simple_mapgen = install_simple_mapgen,
        is_simple_mapgen_installed = function() return generator_installed end
    }
end

return M
