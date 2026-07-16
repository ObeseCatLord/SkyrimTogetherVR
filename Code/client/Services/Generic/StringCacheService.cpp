
#include <TiltedOnlinePCH.h>

#include <Events/ConnectedEvent.h>
#include <Events/DisconnectedEvent.h>

#include <Services/StringCacheService.h>
#include <VRRuntimeDiagnostics.h>
#include "StringCache.h"

StringCacheService::StringCacheService(entt::dispatcher& aDispatcher) noexcept
{
    m_connectedConnection = aDispatcher.sink<ConnectedEvent>().connect<&StringCacheService::HandleConnected>(this);
    m_disconnectedConnection = aDispatcher.sink<DisconnectedEvent>().connect<&StringCacheService::HandleDisconnected>(this);
    m_stringCacheUpdateConnection = aDispatcher.sink<StringCacheUpdate>().connect<&StringCacheService::HandleStringCacheUpdate>(this);
}

void StringCacheService::HandleConnected(const ConnectedEvent& acEvent) noexcept
{
    SkyrimTogetherVR::LogRuntimeCheckpoint("connected.string_cache.begin");
    // Can never be too careful
    StringCache::Get().Clear();
    SkyrimTogetherVR::LogRuntimeCheckpoint("connected.string_cache.done");
}

void StringCacheService::HandleDisconnected(const DisconnectedEvent& acEvent) noexcept
{
    StringCache::Get().Clear();
}

void StringCacheService::HandleStringCacheUpdate(const StringCacheUpdate& acMessage) noexcept
{
    StringCache::Get().Deserialize(acMessage);
}
