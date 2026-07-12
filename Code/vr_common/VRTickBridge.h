#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace SkyrimTogetherVR::TickBridge
{
inline constexpr wchar_t kMappingHandleEnvironment[] = L"STVR_TICK_BRIDGE_HANDLE";
inline constexpr std::uint32_t kEndpointMagic = 0x52565453; // STVR
inline constexpr std::uint16_t kEndpointAbiVersion = 1;

enum class EndpointState : std::int32_t
{
    Prepared = 1,
    Ready = 2,
    Retired = 3,
    Faulted = 4,
};

enum class DispatchResult : std::uint32_t
{
    Success = 0,
    Inactive = 1,
    WrongThread = 2,
    Reentrant = 3,
    MissingClient = 4,
    InvalidEpoch = 5,
};

using DispatchCallback = std::uint32_t(__cdecl*)(std::uint64_t) noexcept;

// This is mapped only within the launcher process. Keep it plain POD so the
// SKSEVR bridge can validate it without sharing C++ runtime state.
struct alignas(8) Endpoint
{
    std::uint32_t Magic{};
    std::uint16_t AbiVersion{};
    std::uint16_t StructSize{};
    std::uint32_t PublisherProcessId{};
    volatile std::int32_t State{};
    std::uint32_t ReadyThreadId{};
    std::uint32_t Reserved0{};
    std::uint64_t Epoch{};
    std::uint64_t ImageBase{};
    std::uint64_t CallbackRva{};
    std::uint64_t Reserved[4]{};
};

static_assert(std::is_standard_layout_v<Endpoint>);
static_assert(std::is_trivially_copyable_v<Endpoint>);
static_assert(alignof(Endpoint) >= alignof(std::uint64_t));
} // namespace SkyrimTogetherVR::TickBridge
