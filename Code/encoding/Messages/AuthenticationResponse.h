#pragma once

#include "Message.h"
#include <Structs/Mods.h>
#include <Structs/ServerSettings.h>
#include <Structs/GameplayCapabilities.h>

struct AuthenticationResponse final : ServerMessage
{
    static constexpr ServerOpcode Opcode = kAuthenticationResponse;

    enum class ResponseType : uint8_t
    {
        kAccepted,
        kWrongVersion,
        kModsMismatch,
        kClientModsDisallowed,
        kWrongPassword,
        kServerFull,
        kProtocolMismatch
    };

    AuthenticationResponse()
        : ServerMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const AuthenticationResponse& achRhs) const noexcept
    {
        return GetOpcode() == achRhs.GetOpcode() && Type == achRhs.Type && UserMods == achRhs.UserMods && Settings == achRhs.Settings && PlayerId == achRhs.PlayerId &&
               GameplayProtocolRevision == achRhs.GameplayProtocolRevision && ServerCapabilities == achRhs.ServerCapabilities && NegotiatedCapabilities == achRhs.NegotiatedCapabilities &&
               ServerInstanceNonce == achRhs.ServerInstanceNonce && ConnectionGeneration == achRhs.ConnectionGeneration &&
               ClientSessionNonce == achRhs.ClientSessionNonce && ConnectionAttempt == achRhs.ConnectionAttempt;
    }

    ResponseType Type{ResponseType::kWrongVersion};
    bool SKSEActive{false};
    bool MO2Active{false};
    String Version;
    Mods UserMods{};
    ServerSettings Settings{};
    uint32_t PlayerId{};
    uint32_t GameplayProtocolRevision{SkyrimTogether::Protocol::kGameplayProtocolRevision};
    SkyrimTogether::Protocol::GameplayCapabilityMask ServerCapabilities{};
    SkyrimTogether::Protocol::GameplayCapabilityMask NegotiatedCapabilities{};
    uint64_t ServerInstanceNonce{};
    uint64_t ConnectionGeneration{};
    uint64_t ClientSessionNonce{};
    uint64_t ConnectionAttempt{};
};
