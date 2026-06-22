-- MOBA: fully custom Instability gauge bar for the Sorcier.
-- Loaded via FrameXML (always on). NOT tied to any native power: the server pushes the gauge value (0-100)
-- as an addon message ("MOBAINST"). Standalone bar (default bottom-centre), movable via "/moba ui".
--
-- Visibility rules (the champion's resource only shows when relevant):
--   * The native player-frame power bar (the "wrong" default resource) is hidden for the Sorcier.
--   * The Instability bar shows only IN A MATCH (a PvP instance) for the Sorcier; nothing in the lobby.
--
-- Wrapped in pcall: as a FrameXML file an unhandled error would be a FATAL crash; on error it no-ops.

local ok, err = pcall(function()

local PREFIX    = "MOBAINST"
local ENTROPY_Q = 900200   -- "Decharge instable": only the Sorcier knows it
local GRAD_BASE = { 0.28, 0.06, 0.50,  0.72, 0.34, 1.00 }   -- base purple (dark bottom -> bright top)
local GRAD_FULL = { 0.80, 0.10, 0.80,  1.00, 0.55, 1.00 }   -- overload pink

local isSorcier = false
local lastPct = 0
local placing = false   -- true while the UI is unlocked for moving

local bar = CreateFrame("StatusBar", "MobaInstabilityBar", UIParent)
bar:SetWidth(240)
bar:SetHeight(18)
bar:SetPoint("BOTTOM", UIParent, "BOTTOM", 0, 175)   -- default; overridden once the user places it
bar:SetFrameStrata("MEDIUM")
bar:SetStatusBarTexture("Interface\\Buttons\\WHITE8X8")
bar:SetStatusBarColor(1, 1, 1)
bar:SetMinMaxValues(0, 100)
bar:SetValue(0)

local bg = bar:CreateTexture(nil, "BACKGROUND")
bg:SetAllPoints(bar)
bg:SetTexture(0, 0, 0, 0.6)

local border = CreateFrame("Frame", nil, bar)
border:SetPoint("TOPLEFT", -3, 3)
border:SetPoint("BOTTOMRIGHT", 3, -3)
border:SetBackdrop({ edgeFile = "Interface\\Tooltips\\UI-Tooltip-Border", edgeSize = 14 })

local label = bar:CreateFontString(nil, "OVERLAY", "GameFontHighlight")
label:SetPoint("CENTER")

bar:Hide()

local function ApplyGradient(g)
    local tex = bar:GetStatusBarTexture()
    if tex and tex.SetGradientAlpha then
        tex:SetGradientAlpha("VERTICAL", g[1], g[2], g[3], 1, g[4], g[5], g[6], 1)
    end
end

local function SetPercent(pct)
    if pct < 0 then pct = 0 elseif pct > 100 then pct = 100 end
    lastPct = pct
    bar:SetValue(pct)
    label:SetText(string.format("Instabilite %d%%", pct))
    ApplyGradient(pct >= 99 and GRAD_FULL or GRAD_BASE)
    bar:Show()
end

local function DetectSorcier()
    isSorcier = false
    local qName = GetSpellInfo(ENTROPY_Q)
    if not qName then return end
    for tab = 1, (GetNumSpellTabs() or 0) do
        local _, _, offset, numSpells = GetSpellTabInfo(tab)
        if offset and numSpells then
            for i = offset + 1, offset + numSpells do
                if GetSpellName(i, BOOKTYPE_SPELL) == qName then
                    isSorcier = true
                    return
                end
            end
        end
    end
end

local function InMatch()
    local _, instanceType = IsInInstance()
    return instanceType == "pvp" or instanceType == "arena"
end

-- Hide the native player-frame power bar (the default/"wrong" resource) for the Sorcier; restore otherwise.
local function SetNativePowerHidden(hidden)
    local a = hidden and 0 or 1
    if PlayerFrameManaBar then PlayerFrameManaBar:SetAlpha(a) end
    if PlayerFrameManaBarText then PlayerFrameManaBarText:SetAlpha(a) end
end

local function Refresh()
    SetNativePowerHidden(true)   -- champions never use the native resource bar: hide it everywhere
    if placing or (isSorcier and InMatch()) then
        SetPercent(lastPct)   -- the right resource, shown only in a match (or while placing)
    else
        bar:Hide()
    end
end

-- Movable through the shared MobaUI system (/moba ui). While unlocked, stay visible so it can be placed.
if MobaUI then
    MobaUI.RegisterMovable(bar, function(_, locked)
        placing = not locked
        Refresh()
    end)
end

local f = CreateFrame("Frame")
f:RegisterEvent("PLAYER_ENTERING_WORLD")
f:RegisterEvent("PLAYER_LOGIN")
f:RegisterEvent("SPELLS_CHANGED")
f:RegisterEvent("LEARNED_SPELL_IN_TAB")
f:RegisterEvent("UNIT_DISPLAYPOWER")
f:RegisterEvent("CHAT_MSG_ADDON")
f:SetScript("OnEvent", function(_, event, arg1, arg2)
    if event == "CHAT_MSG_ADDON" then
        local body
        if arg1 == PREFIX then
            body = arg2
        elseif type(arg1) == "string" then
            body = arg1:match("^" .. PREFIX .. "\t(.+)$")
        end
        if body then
            local v = tonumber(body)
            if v then
                isSorcier = true
                SetNativePowerHidden(true)
                if InMatch() then SetPercent(v) end
            end
        end
        return
    end
    DetectSorcier()
    Refresh()
end)

DetectSorcier()
Refresh()

end)

if not ok then
    MobaInstabilityError = err
    if DEFAULT_CHAT_FRAME then
        DEFAULT_CHAT_FRAME:AddMessage("|cffff5555MobaInstability error:|r " .. tostring(err))
    end
end
