local function EnableLevelOnePvp()
    SHOW_PVP_LEVEL = 1
    if PVPMicroButton then
        PVPMicroButton:Enable()
    end
    if UpdateMicroButtons then
        UpdateMicroButtons()
    end
end

local function OpenPvpFrame()
    EnableLevelOnePvp()
    if LoadAddOn then
        LoadAddOn("Blizzard_PVPUI")
    end
    if TogglePVPFrame then
        TogglePVPFrame()
    end
end

local frame = CreateFrame("Frame")
frame:RegisterEvent("PLAYER_LOGIN")
frame:RegisterEvent("PLAYER_LEVEL_UP")
frame:RegisterEvent("ADDON_LOADED")
frame:SetScript("OnEvent", function(_, event)
    EnableLevelOnePvp()
    if event == "PLAYER_LOGIN" and DEFAULT_CHAT_FRAME then
        DEFAULT_CHAT_FRAME:AddMessage("|cff33ff99MOBA|r Interface PvP niveau 1 active. Commande: /moba pvp")
    end
end)

hooksecurefunc("UpdateMicroButtons", EnableLevelOnePvp)

SLASH_MOBALEVEL1PVP1 = "/moba"
SlashCmdList["MOBALEVEL1PVP"] = function(message)
    if message == "pvp" then
        OpenPvpFrame()
    else
        EnableLevelOnePvp()
        if DEFAULT_CHAT_FRAME then
            DEFAULT_CHAT_FRAME:AddMessage("|cff33ff99MOBA|r /moba pvp")
        end
    end
end

EnableLevelOnePvp()
