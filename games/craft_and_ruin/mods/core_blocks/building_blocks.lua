-- Building blocks: slabs and stairs of every plank and stone, and signs (the base mod's
-- signs library). The engine does their shapes, placing, corners and waterlogging.

local function slab_and_stairs(material, desc, texture, def)
    def = def or {}
    local common = { walkable = true, behavior = "solid", tool = def.tool, hardness = def.hardness }
    local slab = { pattern = "slab", description = desc .. " Slab" }
    local stairs = { pattern = "stairs", description = desc .. " Stairs" }
    for k, v in pairs(common) do slab[k] = v; stairs[k] = v end
    engine.register_block("slab_" .. material, 1.0, 1.0, 1.0, texture, nil, slab)
    engine.register_block("stairs_" .. material, 1.0, 1.0, 1.0, texture, nil, stairs)
end

local woods = {
    { "oak", "Oak", "textures/planks_oak.png" },
    { "birch", "Birch", "textures/planks_birch.png" },
    { "spruce", "Spruce", "textures/cw_spruce_planks.png" },
    { "jungle", "Jungle", "textures/planks_jungle.png" },
}
for _, w in ipairs(woods) do
    slab_and_stairs(w[1], w[2], w[3], { tool = "axe", hardness = 2 })
    if signs and signs.register then
        signs.register("sign_" .. w[1], {
            texture = w[3],
            inventory_image = "textures/sign_" .. w[1] .. ".png",
            description = w[2] .. " Sign",
        })
    end
end
slab_and_stairs("stone", "Stone", "textures/cw_stone.png", { tool = "pickaxe", hardness = 1.5 })
slab_and_stairs("andesite", "Andesite", "textures/andesite.png", { tool = "pickaxe", hardness = 1.5 })

