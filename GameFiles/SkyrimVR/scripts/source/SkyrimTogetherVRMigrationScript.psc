ScriptName SkyrimTogetherVRMigrationScript extends ReferenceAlias

SkyrimTogetherVRMigrationXScript Property VerifyLaunchScript Auto

Event OnPlayerLoadGame()
    SkyrimTogetherVRTickBridge.ArmOnPlayerLoadGame()
    VerifyLaunchScript.VerifyLaunch()
EndEvent
