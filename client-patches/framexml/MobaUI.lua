-- MOBA shared UI core (FrameXML, always on).
-- One place for all custom MOBA UI: a registry of movable frames + a single "/moba" command.
--   * New movable element?  MobaUI.RegisterMovable(frame, onLockChanged)  -> movable via "/moba ui".
--   * New command?          MobaUI.RegisterCommand("name", handler, "help")  -> "/moba name".
-- No per-feature slash command needed. Positions persist across sessions via SetUserPlaced.

MobaUI = MobaUI or {}
MobaUI.movables = MobaUI.movables or {}
MobaUI.commands = MobaUI.commands or {}
MobaUI.unlocked = false

local function trim(s)
    return (string.gsub(string.gsub(s or "", "^%s+", ""), "%s+$", ""))
end

function MobaUI.RegisterMovable(frame, onLockChanged)
    frame:SetMovable(true)
    frame:EnableMouse(false)               -- locked by default: never eats world clicks
    frame:RegisterForDrag("LeftButton")
    frame:SetScript("OnDragStart", function(self) if MobaUI.unlocked then self:StartMoving() end end)
    frame:SetScript("OnDragStop", function(self) self:StopMovingOrSizing(); self:SetUserPlaced(true) end)
    frame.mobaOnLock = onLockChanged       -- optional callback(frame, locked)
    table.insert(MobaUI.movables, frame)
end

function MobaUI.SetUnlocked(state)
    MobaUI.unlocked = state
    for _, f in ipairs(MobaUI.movables) do
        f:EnableMouse(state)
        if not state then f:SetUserPlaced(true) end   -- persist position on lock
        if f.mobaOnLock then f.mobaOnLock(f, not state) end
    end
    if DEFAULT_CHAT_FRAME then
        if state then
            DEFAULT_CHAT_FRAME:AddMessage("|cff9933ffMOBA UI|r deverrouillee - glisse tes elements, puis /moba ui pour verrouiller.")
        else
            DEFAULT_CHAT_FRAME:AddMessage("|cff9933ffMOBA UI|r verrouillee (positions sauvegardees).")
        end
    end
end

function MobaUI.RegisterCommand(name, handler, help)
    MobaUI.commands[name] = { handler = handler, help = help }
end

-- Built-in: "/moba ui" toggles move mode for every registered frame.
MobaUI.RegisterCommand("ui", function() MobaUI.SetUnlocked(not MobaUI.unlocked) end, "deplacer / verrouiller l'UI custom")

SLASH_MOBA1 = "/moba"
SlashCmdList["MOBA"] = function(msg)
    local cmd = string.lower(trim(msg)):match("^(%S*)")
    local entry = cmd and cmd ~= "" and MobaUI.commands[cmd]
    if entry then
        entry.handler(trim(msg))
    elseif DEFAULT_CHAT_FRAME then
        DEFAULT_CHAT_FRAME:AddMessage("|cff9933ffMOBA|r commandes:")
        for name, e in pairs(MobaUI.commands) do
            DEFAULT_CHAT_FRAME:AddMessage("  /moba " .. name .. (e.help and ("  - " .. e.help) or ""))
        end
    end
end
