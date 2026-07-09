ScriptName SkyrimTogetherVerifyLaunchScript extends Quest Native Hidden

; Not bound (not implemented) native functions return default values dictated by their return type.
; `DidLaunchSkyrimTogether()` will return `false` in the base game, however launching
; SkyrimTogetherVR.exe will make it return `true` because we bind the function (see Misc/BSScript.cpp)

bool Function DidLaunchSkyrimTogether() global native

Event OnInit()
    GrantConnectionMenuSpell()
    VerifyLaunch()
EndEvent

Function GrantConnectionMenuSpell()
    Spell menuSpell = Game.GetFormFromFile(0x00001825, "SkyrimTogether.esp") as Spell
    Actor player = Game.GetPlayer()

    If menuSpell && player
        player.AddSpell(menuSpell, false)
    EndIf
EndFunction

Function VerifyLaunch()
    If (!DidLaunchSkyrimTogether())
        Utility.Wait(1)
        Debug.MessageBox("Skyrim Together Error\n\n" \
                       + "Skyrim Together VR is not running!\n" \
                       + "To play Skyrim Together VR and access multiplayer features, " \
                       + "launch SkyrimTogetherVR.exe located in 'SkyrimVR\\Data\\SkyrimTogetherReborn'")
    EndIf
EndFunction
