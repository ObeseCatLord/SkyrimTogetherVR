#include "ActorActionHooks.h"

namespace SkyrimTogetherVR::GameplayAdapter::ActorActionHooks
{
bool Install() noexcept
{
    // VR's PerformAction capture entry is verified, but exact replication also
    // requires ForceAction and supporting calls whose VR ABI is incompatible.
    // Do not detour a core actor path when its captured work cannot be replayed.
    BridgeEndpoint::Get().SetOptionalCapability(Capability::ExactAnimationActions, false);
    try {
        SKSE::log::info(
            "SkyrimTogetherVRGameplayBridge: ActorMediator::PerformAction capture hook is intentionally disabled; "
            "exact ForceAction replay is unavailable and the capability is disabled");
    } catch (...) {
    }
    return false;
}

void Uninstall() noexcept
{
    BridgeEndpoint::Get().SetOptionalCapability(Capability::ExactAnimationActions, false);
    Reset();
}

void Reset() noexcept
{
    // Exact action payloads are never staged while ForceAction is unavailable.
}

CommandStatus Execute(const CommandRecord&) noexcept
{
    // Preserve protocol identifiers, but reject commands before they can
    // allocate state or reach the incompatible PerformAction replay path.
    return CommandStatus::Unsupported;
}
} // namespace SkyrimTogetherVR::GameplayAdapter::ActorActionHooks
