ScriptName SkyrimTogetherVRMigrationXScript extends Quest Hidden

Event OnInit()
    If SkyrimTogetherVRTickBridge.ClaimCadence(2)
        SkyrimTogetherVRTickBridge.ArmOnInit()
        VerifyLaunch()
    EndIf
EndEvent

Function VerifyLaunch()
    If SkyrimTogetherVRTickBridge.ClaimCadence(2)
        StartTickBridge()
    EndIf
EndFunction

Function StartTickBridge()
    RegisterForSingleUpdate(0.05)
EndFunction

Event OnUpdate()
    If !SkyrimTogetherVRTickBridge.ClaimCadence(2)
        Return
    EndIf

    If SkyrimTogetherVRTickBridge.Tick()
        RegisterForSingleUpdate(0.05)
    Else
        RegisterForSingleUpdate(1.0)
    EndIf
EndEvent
