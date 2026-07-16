Scriptname SkyrimTogetherUtils Native Hidden

bool Function IsRemotePlayer(Actor actor) global native

bool Function IsPlayer(Actor actor) global native

bool Function ConnectToSkyrimTogether(string endpoint, string password = "") global native

bool Function DisconnectFromSkyrimTogether() global native

bool Function IsSkyrimTogetherConnected() global native

bool Function SetSkyrimTogetherConnectionConfig(string endpoint, string password = "") global native

string Function GetSkyrimTogetherConnectionState() global native

string Function GetSkyrimTogetherConfiguredEndpoint() global native

string Function GetSkyrimTogetherConfiguredPassword() global native

string Function GetSkyrimTogetherStatusSummary() global native

string Function GetSkyrimTogetherTelemetryReadout() global native

bool Function SendSkyrimTogetherChat(string message) global native

string Function GetSkyrimTogetherPlayerList() global native

bool Function CreateSkyrimTogetherParty() global native

bool Function LeaveSkyrimTogetherParty() global native

bool Function InviteSkyrimTogetherPartyMember(int playerId) global native

bool Function AcceptSkyrimTogetherPartyInvite(int inviterId) global native

bool Function KickSkyrimTogetherPartyMember(int playerId) global native

bool Function ChangeSkyrimTogetherPartyLeader(int playerId) global native
