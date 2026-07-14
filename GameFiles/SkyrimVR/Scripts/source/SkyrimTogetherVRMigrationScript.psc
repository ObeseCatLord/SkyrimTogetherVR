ScriptName SkyrimTogetherVRMigrationScript extends ReferenceAlias

SkyrimTogetherVRMigrationXScript Property VerifyLaunchScript Auto

Event OnPlayerLoadGame()
    SkyrimTogetherVRTickBridge.ArmOnPlayerLoadGame()
    VerifyLaunchScript.RearmCadence("OnPlayerLoadGame")
EndEvent

Event OnCellAttach()
    VerifyLaunchScript.RearmCadence("OnCellAttach")
EndEvent

Event OnLocationChange(Location akOldLocation, Location akNewLocation)
    VerifyLaunchScript.RearmCadence("OnLocationChange")
EndEvent
