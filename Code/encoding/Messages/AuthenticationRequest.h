#pragma once

#include "Message.h"
#include <Structs/Mods.h>
#include <TiltedCore/Buffer.hpp>
#include <Structs/GameId.h>
#include <Structs/TimeModel.h>
#include <Structs/GameplayCapabilities.h>

struct AuthenticationRequest final : ClientMessage
{
    static constexpr ClientOpcode Opcode = kAuthenticationRequest;

    AuthenticationRequest()
        : ClientMessage(Opcode)
    {
    }

    virtual ~AuthenticationRequest() = default;

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const AuthenticationRequest& achRhs) const noexcept
    {
        return GetOpcode() == achRhs.GetOpcode() && DiscordId == achRhs.DiscordId && SKSEActive == achRhs.SKSEActive && MO2Active == achRhs.MO2Active && Token == achRhs.Token && Version == achRhs.Version && UserMods == achRhs.UserMods && Username == achRhs.Username &&
               WorldSpaceId == achRhs.WorldSpaceId && CellId == achRhs.CellId && Level == achRhs.Level && PlayerTime == achRhs.PlayerTime &&
               GameplayProtocolRevision == achRhs.GameplayProtocolRevision && GameplayCapabilities == achRhs.GameplayCapabilities &&
               ClientSessionNonce == achRhs.ClientSessionNonce && ConnectionAttempt == achRhs.ConnectionAttempt;
    }

    uint64_t DiscordId{};
    bool SKSEActive{};
    bool MO2Active{};
    String Token{};
    String Version{};
    Mods UserMods{};
    String Username{};
    GameId WorldSpaceId{};
    GameId CellId{};
    uint16_t Level{};
    TimeModel PlayerTime{};
    uint32_t GameplayProtocolRevision{SkyrimTogether::Protocol::kGameplayProtocolRevision};
    SkyrimTogether::Protocol::GameplayCapabilityMask GameplayCapabilities{SkyrimTogether::Protocol::kClientCapabilities};
    uint64_t ClientSessionNonce{};
    uint64_t ConnectionAttempt{};
};
