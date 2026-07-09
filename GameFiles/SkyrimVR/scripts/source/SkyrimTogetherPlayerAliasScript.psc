ScriptName SkyrimTogetherPlayerAliasScript extends ReferenceAlias

SkyrimTogetherVerifyLaunchScript Property VerifyLaunchScript Auto

Event OnPlayerLoadGame()
    VerifyLaunchScript.GrantConnectionMenuSpell()
    VerifyLaunchScript.VerifyLaunch()
EndEvent
