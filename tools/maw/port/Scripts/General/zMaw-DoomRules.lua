-- OpenYAMM addition to MAW: Doom's rules that live in Merge files the port does not run.
--
-- Followers: MAW's NPCFollowers.lua (a Merge override; OpenYAMM hires followers natively) has no one follow a Doom
-- party for hire except in houses 1466 and 1486, with its words for the hire topic and the refusal.

local FollowerHouses = {[1466] = true, [1486] = true}

function events.CanHireNPCFollower(t)
    if vars.Mode == 2 and not FollowerHouses[t.House] then
        t.Allow = false
        t.Text = "I wouldn't join in your reckless adventure, not even for all the wealth in the world."
        t.Topic = "Following you would be foolish"
    end
end
