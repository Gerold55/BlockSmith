-- Buckets: right-click a fluid source with an empty bucket to scoop it up, right-click
-- with a full bucket to pour it out again. Works like Minecraft's buckets:
--   * only full (source) blocks can be picked up, flowing fluid can't
--   * empty buckets stack, full ones don't
--   * pouring onto a plant or into flowing fluid replaces it with a source
--
-- Works with ANY fluid (see Fluids.h / engine.register_fluid). A mod adds a bucket for its
-- fluid in one of two ways:
--   1. in its fluid definition:  engine.register_fluid{ name = "lava", ...,
--                                    bucket = "lava_bucket", bucket_image = "lava_bucket.png" }
--      (picked up here if that mod loads before this one)
--   2. any time later:           if buckets then buckets.register("lava", "lava_bucket", "lava_bucket.png") end
-- The bucket picture is looked up in the registering mod's folder.

buckets = buckets or {}

local EMPTY = "bucket"
local by_fluid = {}    -- fluid name -> bucket item
local by_bucket = {}   -- bucket item -> fluid name

local function find_empty_slot()
    for i = 1, hotbar.get_slot_count() do
        if hotbar.get_slot(i) == "" then return i end
    end
    return nil
end

-- The held stack of empty buckets becomes `item`: the slot itself when it's the last
-- bucket, otherwise a free slot (false if there isn't one)
local function swap_one(pointed, item)
    if pointed.count <= 1 then
        hotbar.set_slot(pointed.slot, item, 1)
        return true
    end
    local free = find_empty_slot()
    if not free then return false end
    hotbar.set_slot(free, item, 1)
    hotbar.set_slot(pointed.slot, pointed.item, pointed.count - 1)
    return true
end

-- The block a fluid's source is (from engine.list_fluids), or nil if it doesn't exist (yet)
local function source_block_of(fluid_name)
    for _, f in ipairs(engine.list_fluids()) do
        if f.name == fluid_name then
            if engine.get_block_id(f.source) ~= 0 then return f.source end
            return nil
        end
    end
    return nil
end

-- Finds a picture in `dir` (a mod folder) or its textures/ folder. Returns the path to use,
-- and says where it was expected if it's missing (else the item shows as a white square).
local function find_image(image, dir)
    dir = dir or g_CurrentModPath or "."
    if engine.find_texture then   -- the engine knows every mod's folder (Lua has no file access)
        local found = engine.find_texture(image)
        if found then return found end
        print("[Buckets] Can't find " .. image .. " - put it in " .. dir .. "/textures/")
        return image
    end
    if not (io and io.open) then return image end
    for _, path in ipairs({ dir .. "/textures/" .. image, dir .. "/" .. image }) do
        local f = io.open(path, "rb")
        if f then f:close(); return path end
    end
    print("[Buckets] Can't find " .. image .. " - put it in " .. dir .. "/textures/")
    return image
end

local function pour(fluid_name)
    return function(pointed)
        -- Where: the fluid being looked at, the plant/fluid block itself, or the space in
        -- front of the block face
        local t = nil
        if pointed.fluid then
            t = pointed.fluid
        elseif pointed.under then
            local u = pointed.under
            if engine.is_replaceable(u.x, u.y, u.z) then t = u else t = pointed.above end
        end
        if not t or not engine.is_replaceable(t.x, t.y, t.z) then return true end
        if engine.is_fluid_source(t.x, t.y, t.z) then return true end   -- already full

        local block = source_block_of(fluid_name)
        if not block then return true end                             -- the game has no such fluid

        -- A stack (e.g. from the creative menu): pour one, the empty bucket goes elsewhere
        if pointed.count > 1 then
            if not engine.give_item(EMPTY, 1) then return true end   -- no room for it
            hotbar.set_slot(pointed.slot, pointed.item, pointed.count - 1)
        else
            hotbar.set_slot(pointed.slot, EMPTY, 1)
        end
        engine.set_block_meta(t.x, t.y, t.z, block, 0)   -- a fresh source; it flows from here
        return true
    end
end

-- buckets.register(fluid, bucket_item [, image [, folder]]): a bucket that holds `fluid`.
-- The picture is looked for in `folder` (default: the calling mod's folder).
function buckets.register(fluid_name, bucket_item, image, folder)
    if by_fluid[fluid_name] then return end
    image = find_image(image or (bucket_item .. ".png"), folder)
    engine.register_block(bucket_item, 1.0, 1.0, 1.0, image, nil, {
        inventory_image = image,
        walkable = false,
        transparent = true,
    })
    by_fluid[fluid_name] = bucket_item
    by_bucket[bucket_item] = fluid_name
    engine.register_item_use(bucket_item, pour(fluid_name))
    print("[Buckets] " .. bucket_item .. " holds " .. fluid_name)
end

-- ---------------------------------------------------------------------------
-- The empty bucket: scoop up the source being looked at
-- ---------------------------------------------------------------------------
local empty_image = find_image("bucket.png")
engine.register_block(EMPTY, 1.0, 1.0, 1.0, empty_image, nil, {
    inventory_image = empty_image,
    walkable = false,
    transparent = true,
})

engine.register_item_use(EMPTY, function(pointed)
    local f = pointed.fluid
    if not f or not f.source then return true end   -- nothing to scoop (never place the bucket)
    local item = by_fluid[f.name]
    if not item then return true end                -- no bucket for this fluid
    if swap_one(pointed, item) then
        engine.set_block(f.x, f.y, f.z, "air")
    end
    return true
end)

-- Water's bucket comes with this mod; fluids registered earlier with a `bucket` field too
buckets.register("water", "water_bucket", "water_bucket.png")
for _, f in ipairs(engine.list_fluids()) do
    if f.bucket and not by_fluid[f.name] then
        buckets.register(f.name, f.bucket, f.bucket_image, f.mod_path)
    end
end
