-- OpenYAMM addition to MAW: its Nightmare confirmation is answered.
--
-- MAW's nightmare() (zzMaw-Monsters.lua, run by a timer of zzMaw-Timers.lua) asks with Question whether to make
-- Nightmare "true Nightmare" (yes: Game.BolsterAmount 300 for good; anything else: back to Hell). The original's
-- Question waits for the typed answer; here it can wait only in a coroutine, so nightmare() runs in one
-- (ModRT.async). This file loads after zzMaw-Monsters.lua and before zzMaw-Timers.lua, which takes nightmare up.

if type(nightmare) == "function" then
	nightmare = ModRT.async(nightmare)
end
