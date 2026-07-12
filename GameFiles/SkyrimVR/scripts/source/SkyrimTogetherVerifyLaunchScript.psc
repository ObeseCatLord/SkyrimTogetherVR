ScriptName SkyrimTogetherVerifyLaunchScript extends Quest Native Hidden

Event OnInit()
    VerifyLaunch()
EndEvent

Function VerifyLaunch()
    StartTickBridge()
EndFunction

Function StartTickBridge()
    RegisterForSingleUpdate(0.05)
EndFunction

Event OnUpdate()
    If SkyrimTogetherVRTickBridge.Tick()
        RegisterForSingleUpdate(0.05)
    Else
        RegisterForSingleUpdate(1.0)
    EndIf
EndEvent
