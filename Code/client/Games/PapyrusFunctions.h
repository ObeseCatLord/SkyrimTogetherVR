#pragma once

namespace PapyrusFunctions
{

bool IsRemotePlayer(Actor* apActor);
bool IsPlayer(Actor* apActor);
bool DidLaunchSkyrimTogether() { return true; };
bool ConnectToSkyrimTogether(const char* apEndpoint, const char* apPassword);
bool DisconnectFromSkyrimTogether();
bool IsSkyrimTogetherConnected();
bool SetSkyrimTogetherConnectionConfig(const char* apEndpoint, const char* apPassword);
const char* GetSkyrimTogetherConnectionState();
const char* GetSkyrimTogetherConfiguredEndpoint();
const char* GetSkyrimTogetherConfiguredPassword();
const char* GetSkyrimTogetherStatusSummary();
const char* GetSkyrimTogetherTelemetryReadout();

} // namespace PapyrusFunctions
