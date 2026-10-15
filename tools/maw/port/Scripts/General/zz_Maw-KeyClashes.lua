-- OpenYAMM addition to MAW: its hotkeys where they clash with OpenYAMM's own default keys.
--
-- MAW's hotkeys (its MAWSettings.lua, in mods/maw/files, which the player may edit) were chosen for the original's
-- keys. OpenYAMM's K is Fly Down, which is MAW's key to gather the map's dead monsters and crafting materials to the
-- party (teleportDeadMonstersAndCraftingKey, read once by zzMaw-Monsters.lua as it loads): that one moves to N while
-- it is MAW's default. This file loads after zz_Maw-initialize.lua, which sets the keys, and before zzMaw-Monsters.lua.
-- (G, MAW's quick health potion and its sort of every bag, is in zMaw-Bags.lua.)

local K, N = 75, 78

if teleportDeadMonstersAndCraftingKey == nil or teleportDeadMonstersAndCraftingKey == K then
    teleportDeadMonstersAndCraftingKey = N
end
