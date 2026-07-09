Scriptname SkyrimTogetherVRConnectionMenu

; VR control surface for quest, book, lesser-power, or MCM bindings.
; The VR ESP spell-effect entry point calls ToggleConfigured().

string Function DefaultEndpoint() global
    Return SkyrimTogetherUtils.GetSkyrimTogetherConfiguredEndpoint()
EndFunction

string Function DefaultPassword() global
    Return SkyrimTogetherUtils.GetSkyrimTogetherConfiguredPassword()
EndFunction

string Function LocalhostEndpoint() global
    Return "127.0.0.1:10578"
EndFunction

bool Function Configure(string endpoint = "127.0.0.1:10578", string password = "") global
    bool accepted = SkyrimTogetherUtils.SetSkyrimTogetherConnectionConfig(endpoint, password)

    If accepted
        Debug.MessageBox("Skyrim Together VR\n\n" \
                       + "Connection endpoint saved.\n" \
                       + "Endpoint: " + endpoint)
    Else
        Debug.MessageBox("Skyrim Together VR\n\n" \
                       + "Connection endpoint was not saved.\n" \
                       + "Endpoint: " + endpoint)
    EndIf

    Return accepted
EndFunction

bool Function ConfigureLocalhost() global
    Return Configure(LocalhostEndpoint(), "")
EndFunction

bool Function ConfigureAndConnect(string endpoint = "127.0.0.1:10578", string password = "") global
    If !Configure(endpoint, password)
        Return False
    EndIf

    Return ConnectConfigured()
EndFunction

Function ShowStatus() global
    Debug.MessageBox("Skyrim Together VR\n\n" \
                   + SkyrimTogetherUtils.GetSkyrimTogetherStatusSummary())
EndFunction

Function ShowTelemetry() global
    Debug.MessageBox("Skyrim Together VR Telemetry\n\n" \
                   + SkyrimTogetherUtils.GetSkyrimTogetherTelemetryReadout())
EndFunction

Function ShowStatusAndTelemetry() global
    ShowStatus()
    ShowTelemetry()
EndFunction

bool Function Connect(string endpoint = "127.0.0.1:10578", string password = "") global
    bool accepted = SkyrimTogetherUtils.ConnectToSkyrimTogether(endpoint, password)
    string connectionState = SkyrimTogetherUtils.GetSkyrimTogetherConnectionState()

    If accepted
        Debug.MessageBox("Skyrim Together VR\n\n" \
                       + "Connection request queued.\n" \
                       + "Endpoint: " + endpoint + "\n" \
                       + "State: " + connectionState)
    Else
        Debug.MessageBox("Skyrim Together VR\n\n" \
                       + "Connection request was not accepted.\n" \
                       + "Endpoint: " + endpoint + "\n" \
                       + "State: " + connectionState)
    EndIf

    Return accepted
EndFunction

bool Function ConnectLocalhost() global
    Return Connect(LocalhostEndpoint(), "")
EndFunction

bool Function ConnectConfigured() global
    Return Connect(DefaultEndpoint(), DefaultPassword())
EndFunction

bool Function Disconnect() global
    bool accepted = SkyrimTogetherUtils.DisconnectFromSkyrimTogether()
    string connectionState = SkyrimTogetherUtils.GetSkyrimTogetherConnectionState()

    If accepted
        Debug.MessageBox("Skyrim Together VR\n\n" \
                       + "Disconnect request queued.\n" \
                       + "State: " + connectionState)
    Else
        Debug.MessageBox("Skyrim Together VR\n\n" \
                       + "Disconnect request was not accepted.\n" \
                       + "State: " + connectionState)
    EndIf

    Return accepted
EndFunction

bool Function Toggle(string endpoint = "127.0.0.1:10578", string password = "") global
    If SkyrimTogetherUtils.IsSkyrimTogetherConnected()
        Return Disconnect()
    EndIf

    Return Connect(endpoint, password)
EndFunction

bool Function ToggleLocalhost() global
    Return Toggle(LocalhostEndpoint(), "")
EndFunction

bool Function ToggleConfigured() global
    Return Toggle(DefaultEndpoint(), DefaultPassword())
EndFunction
