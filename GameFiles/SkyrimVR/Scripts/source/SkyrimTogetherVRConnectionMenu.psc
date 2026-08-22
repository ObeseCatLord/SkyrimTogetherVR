Scriptname SkyrimTogetherVRConnectionMenu

; VR control surface for quest, book, lesser-power, or MCM bindings.
; The VR ESP spell-effect entry point calls OpenControlMenu().

bool Function OpenControlMenu() global
    Return SkyrimTogetherUtils.OpenSkyrimTogetherVRControlMenu()
EndFunction

string Function DefaultEndpoint() global
    Return SkyrimTogetherUtils.GetSkyrimTogetherConfiguredEndpoint()
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

Function ShowPlayers() global
    Debug.MessageBox("Skyrim Together VR Players\n\n" \
                   + SkyrimTogetherUtils.GetSkyrimTogetherPlayersSummary())
EndFunction

; These pages are safe to bind to the existing lesser-power/menu entries. They
; only present native snapshots; all party and command authority remains native.
Function ShowControls() global
    Debug.MessageBox("Skyrim Together VR Controls\n\n" \
                   + SkyrimTogetherUtils.GetSkyrimTogetherControlSummary())
EndFunction

Function ShowParty() global
    Debug.MessageBox("Skyrim Together VR Party\n\n" \
                   + SkyrimTogetherUtils.GetSkyrimTogetherPartySummary())
EndFunction

Function ShowInvitations() global
    Debug.MessageBox("Skyrim Together VR Invitations\n\n" \
                   + SkyrimTogetherUtils.GetSkyrimTogetherInviteList())
EndFunction

bool Function SendChat(string message) global
    Return SkyrimTogetherUtils.SendSkyrimTogetherChat(message)
EndFunction

bool Function CreateParty() global
    Return SkyrimTogetherUtils.CreateSkyrimTogetherParty()
EndFunction

bool Function LeaveParty() global
    Return SkyrimTogetherUtils.LeaveSkyrimTogetherParty()
EndFunction

bool Function InvitePlayer(int playerId) global
    Return SkyrimTogetherUtils.InviteSkyrimTogetherPartyMember(playerId)
EndFunction

bool Function AcceptInvite(int inviterId) global
    Return SkyrimTogetherUtils.AcceptSkyrimTogetherPartyInvite(inviterId)
EndFunction

bool Function DeclineInvite(int inviterId) global
    Return SkyrimTogetherUtils.DeclineSkyrimTogetherPartyInvite(inviterId)
EndFunction

bool Function KickPlayer(int playerId) global
    Return SkyrimTogetherUtils.KickSkyrimTogetherPartyMember(playerId)
EndFunction

bool Function ChangeLeader(int playerId) global
    Return SkyrimTogetherUtils.ChangeSkyrimTogetherPartyLeader(playerId)
EndFunction

bool Function SetTime(int hours, int minutes) global
    Return SkyrimTogetherUtils.SetSkyrimTogetherTime(hours, minutes)
EndFunction

bool Function TeleportToPlayer(int playerId) global
    Return SkyrimTogetherUtils.TeleportSkyrimTogetherToPlayer(playerId)
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
    bool accepted = SkyrimTogetherUtils.ConnectToConfiguredSkyrimTogether()
    string connectionState = SkyrimTogetherUtils.GetSkyrimTogetherConnectionState()
    string endpoint = DefaultEndpoint()

    If accepted
        Debug.MessageBox("Skyrim Together VR\n\n" \
                       + "Configured connection request queued.\n" \
                       + "Endpoint: " + endpoint + "\n" \
                       + "State: " + connectionState)
    Else
        Debug.MessageBox("Skyrim Together VR\n\n" \
                       + "Configured connection request was not accepted.\n" \
                       + "Endpoint: " + endpoint + "\n" \
                       + "State: " + connectionState)
    EndIf

    Return accepted
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
    If SkyrimTogetherUtils.IsSkyrimTogetherConnected()
        Return Disconnect()
    EndIf

    Return ConnectConfigured()
EndFunction
