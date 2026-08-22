Scriptname SkyrimTogetherUtils Native Hidden

bool Function IsRemotePlayer(Actor actor) global native

bool Function IsPlayer(Actor actor) global native

bool Function ConnectToSkyrimTogether(string endpoint, string password = "") global native

bool Function DisconnectFromSkyrimTogether() global native

bool Function OpenSkyrimTogetherVRControlMenu() global native

bool Function IsSkyrimTogetherConnected() global native

bool Function SetSkyrimTogetherConnectionConfig(string endpoint, string password = "") global native

string Function GetSkyrimTogetherConnectionState() global native

string Function GetSkyrimTogetherConfiguredEndpoint() global native

bool Function ConnectToConfiguredSkyrimTogether() global native

string Function GetSkyrimTogetherStatusSummary() global native

string Function GetSkyrimTogetherTelemetryReadout() global native

bool Function SendSkyrimTogetherChat(string message) global native

string Function GetSkyrimTogetherPlayerList() global native

bool Function CreateSkyrimTogetherParty() global native

bool Function LeaveSkyrimTogetherParty() global native

bool Function InviteSkyrimTogetherPartyMember(int playerId) global native

bool Function AcceptSkyrimTogetherPartyInvite(int inviterId) global native

bool Function DeclineSkyrimTogetherPartyInvite(int inviterId) global native

bool Function KickSkyrimTogetherPartyMember(int playerId) global native

bool Function ChangeSkyrimTogetherPartyLeader(int playerId) global native

bool Function SetSkyrimTogetherTime(int hours, int minutes) global native

bool Function TeleportSkyrimTogetherToPlayer(int playerId) global native

string Function GetSkyrimTogetherPartySummary() global native

string Function GetSkyrimTogetherPlayersSummary() global native

string Function GetSkyrimTogetherInviteList() global native

string Function GetSkyrimTogetherControlSummary() global native
