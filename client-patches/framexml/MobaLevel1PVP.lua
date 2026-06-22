-- MOBA: allow the PvP/Battleground interface before vanilla level 10.
-- Was a standalone addon; now shipped via FrameXML in the patch MPQ (always on, non-disableable), wired
-- into the shared MobaUI command system ("/moba pvp").
--
-- Wrapped in pcall: as a FrameXML file an unhandled error would be a FATAL crash; on error it no-ops.

local ok, err = pcall(function()

local function EnableLevelOnePvp()
    SHOW_PVP_LEVEL = 1
    if PVPMicroButton then PVPMicroButton:Enable() end
    if UpdateMicroButtons then UpdateMicroButtons() end
end

local function OpenPvpFrame()
    EnableLevelOnePvp()
    if LoadAddOn then LoadAddOn("Blizzard_PVPUI") end
    if TogglePVPFrame then TogglePVPFrame() end
end

local frame = CreateFrame("Frame")
frame:RegisterEvent("PLAYER_LOGIN")
frame:RegisterEvent("PLAYER_LEVEL_UP")
frame:SetScript("OnEvent", function() EnableLevelOnePvp() end)

if UpdateMicroButtons then
    hooksecurefunc("UpdateMicroButtons", EnableLevelOnePvp)
end

-- Unified command: "/moba pvp"
if MobaUI then
    MobaUI.RegisterCommand("pvp", OpenPvpFrame, "ouvrir l'interface PvP")
end

EnableLevelOnePvp()

end)

if not ok and DEFAULT_CHAT_FRAME then
    DEFAULT_CHAT_FRAME:AddMessage("|cffff5555MobaLevel1PVP error:|r " .. tostring(err))
end
