ScriptName SkyrimTogetherVRMigrationXScript extends Quest Hidden

Event OnInit()
    SkyrimTogetherVRTickBridge.ArmOnInit()
    RearmCadence("OnInit")
EndEvent

Function VerifyLaunch()
    RearmCadence("VerifyLaunch")
EndFunction

Function RearmCadence(String asReason)
    If SkyrimTogetherVRTickBridge.ClaimCadence(2)
        Debug.Trace("SkyrimTogetherVR cadence rearm owner=2 reason=" + asReason)
        UnregisterForUpdate()
        RegisterForSingleUpdate(0.05)
    EndIf
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
