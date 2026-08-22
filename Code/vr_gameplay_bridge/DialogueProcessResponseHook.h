#pragma once

#include "VrHookDetachPolicy.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>

namespace SkyrimTogetherVR::GameplayAdapter::DialogueProcessResponseHookPolicy
{
// This is the directly verified SkyrimVR.exe 1.4.15.0 AIProcess body.  The
// generated desktop alias at 0x6D96F0 and the nearby helper at 0x681F80 are
// different functions and are deliberately not acceptable hook targets.
inline constexpr std::uintptr_t kProcessResponseVrRva = 0x066DD50;
inline constexpr std::size_t kProcessResponseVrFunctionSpan = 0x27A;
inline constexpr std::array<std::uint8_t, 19> kProcessResponseVrPrologue{
    0x48, 0x8B, 0xC4, 0x48, 0x89, 0x50, 0x10, 0x56, 0x57,
    0x41, 0x54, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x40,
};
inline constexpr std::uintptr_t kRejectedGeneratedProcessResponseVrRva = 0x06D96F0;
inline constexpr std::uintptr_t kRejectedProcessResponseHelperVrRva = 0x0681F80;

// MSVC lowers this nontrivial by-value BSTSmartPointer<DialogueItem> to a
// pointer in RDX to caller-allocated smart-pointer storage. The detour must
// forward that storage pointer unchanged, or destruct that exact object in
// place if it suppresses the native call.
inline constexpr bool kDialogueItemArgumentIsIndirectStorage = true;
inline constexpr std::size_t kByValueSmartPointerStorageBytes = 8;
inline constexpr std::size_t kFifthArgumentStackOffset = 0x28;
inline constexpr std::size_t kShimShadowAndSaveBytes = 0x68;

enum class Disposition : std::uint8_t
{
    ForwardToOriginal,
    SuppressAndDestroyDialogueItemStorage,
};

[[nodiscard]] constexpr bool IsPinnedTarget(const std::uintptr_t a_rva) noexcept
{
    return a_rva == kProcessResponseVrRva;
}

[[nodiscard]] constexpr bool IsSpanWithin(
    const std::uintptr_t a_start,
    const std::uintptr_t a_size,
    const std::uintptr_t a_spanStart,
    const std::uintptr_t a_spanSize) noexcept
{
    if (a_size == 0 || a_spanSize == 0 || a_spanStart < a_start ||
        a_start > std::numeric_limits<std::uintptr_t>::max() - a_size)
        return false;
    const auto offset = a_spanStart - a_start;
    return offset <= a_size && a_spanSize <= a_size - offset;
}

[[nodiscard]] constexpr Disposition Decide(
    const bool a_hasTalkingActor,
    const bool a_isAvatarManagerManagedRemote,
    const bool a_isAuthoritativeDialogueReplay) noexcept
{
    return a_hasTalkingActor && a_isAvatarManagerManagedRemote && !a_isAuthoritativeDialogueReplay ?
               Disposition::SuppressAndDestroyDialogueItemStorage :
               Disposition::ForwardToOriginal;
}

[[nodiscard]] constexpr std::uint8_t DetourOwnedDestructionCount(const Disposition a_disposition) noexcept
{
    return a_disposition == Disposition::SuppressAndDestroyDialogueItemStorage ? 1 : 0;
}

[[nodiscard]] constexpr std::uint8_t NativeOwnedDestructionCount(const Disposition a_disposition) noexcept
{
    return a_disposition == Disposition::ForwardToOriginal ? 1 : 0;
}

// RDX is an address of the nontrivial by-value argument object, not an object
// pointer contained by that argument. This destruction path therefore invokes
// the smart pointer destructor at the indirect storage address exactly once.
template <class T> void DestroyIndirectStorage(T* const a_storage) noexcept
{
    if (a_storage)
        std::destroy_at(a_storage);
}
} // namespace SkyrimTogetherVR::GameplayAdapter::DialogueProcessResponseHookPolicy

namespace SkyrimTogetherVR::GameplayAdapter::DialogueProcessResponseHook
{
// Use this only around a future authoritative replay that intentionally calls
// AIProcess::ProcessResponse. Existing voice/subtitle replay calls bypass this
// body, so they do not enter the scope.
class ScopedAuthoritativeDialogueReplay final
{
public:
    ScopedAuthoritativeDialogueReplay() noexcept;
    ~ScopedAuthoritativeDialogueReplay() noexcept;

    ScopedAuthoritativeDialogueReplay(const ScopedAuthoritativeDialogueReplay&) = delete;
    ScopedAuthoritativeDialogueReplay& operator=(const ScopedAuthoritativeDialogueReplay&) = delete;

private:
    bool _entered{};
};

[[nodiscard]] bool IsAuthoritativeDialogueReplay() noexcept;
[[nodiscard]] bool Install() noexcept;
[[nodiscard]] bool Uninstall() noexcept;
} // namespace SkyrimTogetherVR::GameplayAdapter::DialogueProcessResponseHook
