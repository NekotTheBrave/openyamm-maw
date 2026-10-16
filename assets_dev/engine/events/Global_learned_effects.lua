-- Effects shown after an event's hover name once the party has learned them (the event's own autonote):
-- "Orange Barrel (+2 Intellect)" after drinking from any orange barrel.

local learnedEffects = {
    {event = 269, autonote = 17, effect = "+2 Might"}, -- Red Barrel
    {event = 270, autonote = 21, effect = "+2 Accuracy"}, -- Yellow Barrel
    {event = 271, autonote = 19, effect = "+2 Personality"}, -- Blue Barrel
    {event = 272, autonote = 18, effect = "+2 Intellect"}, -- Orange Barrel
    {event = 273, autonote = 20, effect = "+2 Endurance"}, -- Green Barrel
    {event = 274, autonote = 22, effect = "+2 Speed"}, -- Purple Barrel
    {event = 275, autonote = 23, effect = "+2 Luck"}, -- White Barrel
    {event = 277, autonote = 24, effect = "+2 Fire Resistance"}, -- Steaming Cauldron
    {event = 278, autonote = 25, effect = "+2 Water Resistance"}, -- Frosty Cauldron
    {event = 279, autonote = 26, effect = "+2 Air Resistance"}, -- Shocking Cauldron
    {event = 280, autonote = 27, effect = "+2 Earth Resistance"}, -- Dirty Cauldron
}

for _, learned in ipairs(learnedEffects) do
    RegisterGlobalEventLearnedEffect(learned.event, learned.autonote, learned.effect)
end
