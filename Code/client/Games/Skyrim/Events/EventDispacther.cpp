#include <TiltedOnlinePCH.h>

#include <Events/EventDispatcher.h>

#ifndef TP_SKYRIM_VR
#define TP_SKYRIM_VR 0
#endif

namespace details
{
void InternalRegisterSink(void* apEventDispatcher, void* apSink) noexcept
{
#if TP_SKYRIM_VR
    TP_UNUSED(apEventDispatcher);
    TP_UNUSED(apSink);
    spdlog::critical("SkyrimTogetherVR blocked an unvalidated VR BSTEventSource::AddEventSink request");
#else
    TP_THIS_FUNCTION(TRegisterSink, void, void, void* apSink);

    // SkyrimVM ctor RegisterSinks
    POINTER_SKYRIMSE(TRegisterSink, s_registerSink, 54425);

    TiltedPhoques::ThisCall(s_registerSink, apEventDispatcher, apSink);
#endif
}

void InternalUnRegisterSink(void* apEventDispatcher, void* apSink) noexcept
{
#if TP_SKYRIM_VR
    TP_UNUSED(apEventDispatcher);
    TP_UNUSED(apSink);
    spdlog::critical("SkyrimTogetherVR blocked an unvalidated VR BSTEventSource::RemoveEventSink request");
#else
    TP_THIS_FUNCTION(TUnRegisterSink, void, void, void* apSink);

    // SkyrimVM dtor UnRegisterSinks
    POINTER_SKYRIMSE(TUnRegisterSink, s_unregisterSink, 54522);

    TiltedPhoques::ThisCall(s_unregisterSink, apEventDispatcher, apSink);
#endif
}

void InternalPushEvent(void* apEventDispatcher, void* apEvent) noexcept
{
    TP_THIS_FUNCTION(TPushEvent, void, void, void* apSink);

    // "Failed to setup moving reference because it has no parent cell or no 3D" after interlocked
    POINTER_SKYRIMSE(TPushEvent, s_pushEvent, 19364);

    TiltedPhoques::ThisCall(s_pushEvent, apEventDispatcher, apEvent);
}
} // namespace details

EventDispatcherManager* EventDispatcherManager::Get() noexcept
{
    using TGetEventDispatcherManager = EventDispatcherManager*();

    POINTER_SKYRIMSE(TGetEventDispatcherManager, s_getEventDispatcherManager, 14298);

    return s_getEventDispatcherManager.Get()();
}
