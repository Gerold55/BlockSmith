print("[Craft & Ruin] Configuring enlarged hotbar UI...")

hotbar.set_size(9)
hotbar.set_slot_size(0.14)

-- posX: 0 = centered. posY: pixels above the bottom margin.
hotbar.set_position(0.0, 20)

hotbar.set_texture("hotbar_bg.png")
hotbar.set_selected_texture("hotbar_selected.png")
hotbar.set_texture_scale(1.0)
