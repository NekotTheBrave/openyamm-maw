# MAW's keys in the OpenYAMM port

MAW reads keys itself, in its scripts, so none of these appear in the game's keybind menu. The ones in the first table
are MAW's settings, kept in `mods/maw/files/MAWSettings.lua` (created on the first run; edit it to rebind, then restart
the game). Keys there are Windows virtual-key numbers; the file lists them (A = 65 ... Z = 90, 0 = 48 ... 9 = 57). The
others are fixed in MAW's scripts.

OpenYAMM's own default keys (Options > Keyboard) are laid out for WASD: W/A/S/D move, Space interacts, E Use, F attack,
Q quick cast, C cast, R rest, J/K fly up/down, L map book, U always run, Tab next character, 1-5 party members, 5-8
followers. Where MAW's keys meet them, the "Notes" column says what happens.

Port changes are marked **(port)**.

## MAW's settings (`MAWSettings.lua`)

| Key | Setting | Where | What it does | Notes |
|---|---|---|---|---|
| R | `currentBagSortKey` | Inventory page | Sort the open bag | **(port)** crafting materials go to the crafting bags |
| T | `partyBagSortKey` | Inventory page | Sort every party member's open bag | **(port)** crafting materials go to the crafting bags |
| C | `multiBagSortKey` | Inventory page | Sort all five bags of the character together | **(port)** crafting bags filled first with crafting materials. OpenYAMM's Cast is off on the inventory page |
| G | `partyMultyBagSortKey` | Inventory page | Sort every character's bags (MAW's "giga sort") | G is also the quick health potion: **(port)** the potion keys do nothing on the inventory page |
| E | `AlchemyBagKey` | Inventory page | Make the current character the one who takes alchemy items when sorting (again to clear) | OpenYAMM's Use is off on the inventory page |
| G | `healthPotionKey` | Anywhere but the inventory page **(port)** | Drink the best red potion (party of one only); 15 s cooldown | |
| V | `manaPotionKey` | Anywhere but the inventory page **(port)** | Drink the best blue potion (party of one only); 15 s cooldown | |
| E | `chargeKey` | World | Knight charge: a party of one Knight (classes 16-19) charges the monster under the mouse 500-3000 away and stuns it; 25 s cooldown | Also OpenYAMM's Use. **(port)** works since the hovered monster counts as "under the mouse" |
| R | `removeBuffsKey` | Spellbook open | Remove all buffs | OpenYAMM's Rest is off in the spellbook |
| N **(port)** | `teleportDeadMonstersAndCraftingKey` | World | Gather up to 30 dead monsters and 30 crafting materials (and maps) lying on the map to the party | MAW's default is K, OpenYAMM's Fly Down: **(port)** moved to N while it is K |

## Fixed keys

| Key | Where | What it does | Notes |
|---|---|---|---|
| Shift+1 ... Shift+5 | Inventory page | Switch to bag 1-5 (party of more than one) | **(port)** Shift+number no longer also switches the character shown |
| 1 ... 5 | Inventory page | Switch to bag 1-5 (party of one) | |
| Shift+1 ... Shift+5 | Inventory page | Switch to bag group (tab) 1-5 (party of one) | |
| Bag buttons | Character screen | The same, with the mouse (top row bags, bottom row tabs, party of one only) | **(port)** buttons moved into the panel right of the portraits |
| Shift+B | Inventory page | Mark the open bag as a crafting bag (again to clear); crafting materials picked up go to it | |
| U, then U | Inventory page | Combine three crafting gems of a kind into one of the next tier (the first U shows what, the second does it) | |
| P | Skills page | Cover on/off for the character | |
| M | Skills page | Mana Shield on/off for the character | M also opens the map in OpenYAMM; whether it does so over the character screen has not been checked |
| R | Skills page | Elementalist rotation on/off (Elementalists only) | |
| R, then Y | Stats page | Reset the damage figures of the map | |
| Tab | Spellbook | Next school of magic | OpenYAMM's next character is off in the spellbook |
| O | World | Bow off/on (a challenge: melee only) | |
| L | World | Unstuck: lift the party a little; once every 5 game minutes | Also OpenYAMM's Map Book: both happen |
| R | A shop's goods or special goods page | Pay to make the shop's goods again, stronger with the continent's progress, of the kinds it sells | **(port)** works since the shop's goods are bound; OpenYAMM's Rest is off in a house |

MAW's Ctrl+F1 and Alt+F1 only note the use of its console, and its F9 boss dump is switched off in MAW itself.
