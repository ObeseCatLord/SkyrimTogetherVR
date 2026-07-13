ScriptName SkyrimTogetherPlayerAliasScript extends ReferenceAlias

SkyrimTogetherVerifyLaunchScript Property VerifyLaunchScript Auto

Event OnPlayerLoadGame()
    SkyrimTogetherVRTickBridge.ArmOnPlayerLoadGame()
    VerifyLaunchScript.VerifyLaunch()
EndEvent
