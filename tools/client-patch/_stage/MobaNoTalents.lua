-- MOBA: permanently disable the Talent UI.
-- In the MOBA mode, champions have no talent tree. The server already sets FreeTalentPoints to 0,
-- but the client still shows the TalentMicroButton (pulsing at level 9) and the PlayerTalentFrame.
-- This FrameXML module forces the talent button disabled and prevents the talent frame from opening.
--
-- Loaded via FrameXML (always on, non-disableable). Wired into the shared MobaUI command system.
--
-- Wrapped in pcall: as a FrameXML file an unhandled error would be a FATAL crash; on error it no-ops.

local ok, err = pcall(function()

-- Push the client "show talents" level beyond any reachable champion level so UpdateMicroButtons
-- always takes the disable branch, and the level-9 pulse (SHOW_TALENT_LEVEL - 1) never fires.
if SHOW_TALENT_LEVEL then
    SHOW_TALENT_LEVEL = 99999
end
if SHOW_INSCRIPTION_LEVEL then
    SHOW_INSCRIPTION_LEVEL = 99999
end

-- Belt-and-suspenders: even if another addon or a later UpdateMicroButtons call re-enables the
-- button, force it back to disabled on every micro-button refresh.
if UpdateMicroButtons then
    hooksecurefunc("UpdateMicroButtons", function()
        if TalentMicroButton then
            TalentMicroButton:Disable()
        end
    end)
end

-- Block the talent frame from opening via its toggles (keybind, micro-button click, AddOn loader).
-- PlayerTalentFrame_Toggle is the entry point used by ToggleTalentFrame (the keybind handler) and
-- by the Blizzard_TalentUI AddOn itself. Override it to a no-op so the frame never shows.
if PlayerTalentFrame_Toggle then
    hooksecurefunc("PlayerTalentFrame_Toggle", function()
        if PlayerTalentFrame and PlayerTalentFrame:IsShown() then
            PlayerTalentFrame:Hide()
        end
    end)
end

if PlayerTalentFrame_Open then
    hooksecurefunc("PlayerTalentFrame_Open", function()
        if PlayerTalentFrame and PlayerTalentFrame:IsShown() then
            PlayerTalentFrame:Hide()
        end
    end)
end

-- Also block the glyph frame (it lives inside the talent frame and opens via the GLYPH_TALENT_TAB).
if PlayerTalentFrame_ToggleGlyphFrame then
    hooksecurefunc("PlayerTalentFrame_ToggleGlyphFrame", function()
        if PlayerTalentFrame and PlayerTalentFrame:IsShown() then
            PlayerTalentFrame:Hide()
        end
    end)
end

if PlayerTalentFrame_OpenGlyphFrame then
    hooksecurefunc("PlayerTalentFrame_OpenGlyphFrame", function()
        if PlayerTalentFrame and PlayerTalentFrame:IsShown() then
            PlayerTalentFrame:Hide()
        end
    end)
end

-- Initial disable on login (the button exists once Blizzard_TalentUI loads, but UpdateMicroButtons
-- runs on PLAYER_ENTERING_WORLD and PLAYER_LEVEL_UP; this covers the earliest frames).
local frame = CreateFrame("Frame")
frame:RegisterEvent("PLAYER_LOGIN")
frame:RegisterEvent("PLAYER_LEVEL_UP")
frame:RegisterEvent("PLAYER_ENTERING_WORLD")
frame:SetScript("OnEvent", function()
    if TalentMicroButton then
        TalentMicroButton:Disable()
    end
end)

end)

if not ok and DEFAULT_CHAT_FRAME then
    DEFAULT_CHAT_FRAME:AddMessage("|cffff5555MobaNoTalents error:|r " .. tostring(err))
end