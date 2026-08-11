#include "pch.h"

#include "VRFaceGen.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>

namespace SkyrimTogetherVR::GameplayAdapter::VRFaceGen
{
namespace
{
// Skyrim VR 1.4.15.0 targets established from the official SKSEVR 2.0.12
// source and the matching local executable. PrepareHeadPartForShaders at
// 0x003E23D0 follows this exact sequence for non-player face materials.
constexpr std::uintptr_t kCreateTintTextureRva = 0x0CAEF60;
constexpr std::uintptr_t kApplyTintMasksRva = 0x03EADA0;
constexpr std::uintptr_t kTestextureCtorRva = 0x01B0900;
constexpr std::uintptr_t kTestextureDtorRva = 0x01B1040;
constexpr std::uintptr_t kCreateRenderTextureRva = 0x0DBF9B0;
constexpr std::uintptr_t kRendererRva = 0x3181700;
constexpr std::uintptr_t kTintTextureWidthRva = 0x3185858;
constexpr std::uintptr_t kTintTextureHeightRva = 0x318585C;
constexpr std::uintptr_t kTestextureVtableRva = 0x15B58E0;
constexpr std::uintptr_t kNiSourceTextureVtableRva = 0x17F1660;

// First sixteen bytes from SkyrimVR.exe 1.4.15.0. The raw native calls below
// are unavailable if any target differs, even when its RVA is executable.
constexpr std::array<std::uint8_t, 16> kCreateTintTexturePrologue{
    0x40, 0x57, 0x48, 0x83, 0xEC, 0x30, 0x48, 0xC7, 0x44, 0x24, 0x20, 0xFE, 0xFF, 0xFF, 0xFF, 0x48,
};
constexpr std::array<std::uint8_t, 16> kApplyTintMasksPrologue{
    0x48, 0x8B, 0xC4, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x81,
};
constexpr std::array<std::uint8_t, 16> kTestextureCtorPrologue{
    0x48, 0x89, 0x4C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x30, 0x48, 0xC7, 0x44, 0x24, 0x20, 0xFE,
};
constexpr std::array<std::uint8_t, 16> kTestextureDtorPrologue{
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8D, 0x05, 0x8F, 0x48, 0x40,
};
constexpr std::array<std::uint8_t, 16> kCreateRenderTexturePrologue{
    0x40, 0x55, 0x56, 0x57, 0x48, 0x8D, 0x6C, 0x24, 0xB9, 0x48, 0x81, 0xEC, 0x90, 0x00, 0x00, 0x00,
};

using CreateTintTexture = RE::NiTexture* (*)(const RE::BSFixedString&);
using ApplyTintMasks = void (*)(const RE::BSTArray<RE::TintMask*>&, RE::NiTexture*);
using ConstructTestexture = RE::TESTexture* (*)(RE::TESTexture*);
using DestroyTestexture = RE::TESTexture* (*)(RE::TESTexture*, bool);
using CreateRenderTexture = RE::NiTexture::RendererData* (*)(RE::BSGraphics::Renderer*, std::uint32_t, std::uint32_t);

static_assert(sizeof(RE::BSTArray<RE::TintMask*>) == 0x18);
static_assert(sizeof(RE::TintMask) == 0x18);
static_assert(sizeof(RE::TESTexture) == 0x10);
static_assert(sizeof(RE::NiSourceTexture) == 0x58);
static_assert(sizeof(RE::NiTexture::RendererData) == sizeof(RE::BSGraphics::Texture));

struct EngineTargets
{
    CreateTintTexture CreateTexture{};
    ApplyTintMasks ApplyMasks{};
    ConstructTestexture ConstructTexture{};
    DestroyTestexture DestroyTexture{};
    CreateRenderTexture CreateRendererTexture{};
    RE::BSGraphics::Renderer* Renderer{};
    std::uint32_t TintTextureWidth{};
    std::uint32_t TintTextureHeight{};
    std::uintptr_t TestextureVtable{};
    std::uintptr_t NiSourceTextureVtable{};
};

[[nodiscard]] bool IsExecutableTarget(const std::uintptr_t a_address) noexcept
{
    const auto text = REL::Module::get().segment(REL::Segment::textx);
    if (a_address < text.address() || a_address - text.address() >= text.size())
        return false;

    MEMORY_BASIC_INFORMATION memory{};
    if (VirtualQuery(reinterpret_cast<const void*>(a_address), &memory, sizeof(memory)) != sizeof(memory) || memory.State != MEM_COMMIT ||
        (memory.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0)
        return false;
    constexpr DWORD kExecutable = PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    return (memory.Protect & kExecutable) != 0;
}

[[nodiscard]] bool IsReadable(const void* a_address, const std::size_t a_size) noexcept
{
    if (!a_address || a_size == 0)
        return false;

    MEMORY_BASIC_INFORMATION memory{};
    if (VirtualQuery(a_address, &memory, sizeof(memory)) != sizeof(memory) || memory.State != MEM_COMMIT || (memory.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0)
        return false;
    constexpr DWORD kReadable = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    if ((memory.Protect & kReadable) == 0)
        return false;

    const auto address = reinterpret_cast<std::uintptr_t>(a_address);
    const auto regionEnd = reinterpret_cast<std::uintptr_t>(memory.BaseAddress) + memory.RegionSize;
    return address <= regionEnd && regionEnd - address >= a_size;
}

template <std::size_t Size> [[nodiscard]] bool HasExactPrologue(const std::uintptr_t a_address, const std::array<std::uint8_t, Size>& a_expected) noexcept
{
    return IsReadable(reinterpret_cast<const void*>(a_address), a_expected.size()) &&
           std::memcmp(reinterpret_cast<const void*>(a_address), a_expected.data(), a_expected.size()) == 0;
}

[[nodiscard]] bool HasExpectedVTable(const void* a_object, const std::uintptr_t a_expected) noexcept
{
    return IsReadable(a_object, sizeof(std::uintptr_t)) && *reinterpret_cast<const std::uintptr_t*>(a_object) == a_expected;
}

[[nodiscard]] bool HasVerifiedVTable(const std::uintptr_t a_address) noexcept
{
    const auto rdata = REL::Module::get().segment(REL::Segment::rdata);
    if (a_address < rdata.address())
        return false;

    const auto offset = a_address - rdata.address();
    if (offset > rdata.size() || rdata.size() - offset < sizeof(std::uintptr_t) ||
        !IsReadable(reinterpret_cast<const void*>(a_address), sizeof(std::uintptr_t)))
        return false;

    return IsExecutableTarget(*reinterpret_cast<const std::uintptr_t*>(a_address));
}

[[nodiscard]] bool TryGetTargets(EngineTargets& a_targets) noexcept
{
    const auto& module = REL::Module::get();
    if (!REL::Module::IsVR() || module.version() != REL::Version{1, 4, 15, 0})
        return false;

    try
    {
        const REL::Relocation<CreateTintTexture> createTexture{REL::Offset(kCreateTintTextureRva)};
        const REL::Relocation<ApplyTintMasks> applyMasks{REL::Offset(kApplyTintMasksRva)};
        const REL::Relocation<ConstructTestexture> constructTexture{REL::Offset(kTestextureCtorRva)};
        const REL::Relocation<DestroyTestexture> destroyTexture{REL::Offset(kTestextureDtorRva)};
        const REL::Relocation<CreateRenderTexture> createRendererTexture{REL::Offset(kCreateRenderTextureRva)};
        auto* renderer = reinterpret_cast<RE::BSGraphics::Renderer*>(REL::Offset(kRendererRva).address());
        const auto* tintTextureWidth = reinterpret_cast<const std::uint32_t*>(REL::Offset(kTintTextureWidthRva).address());
        const auto* tintTextureHeight = reinterpret_cast<const std::uint32_t*>(REL::Offset(kTintTextureHeightRva).address());
        const auto testextureVtable = REL::Offset(kTestextureVtableRva).address();
        const auto niSourceTextureVtable = REL::Offset(kNiSourceTextureVtableRva).address();

        if (!IsExecutableTarget(createTexture.address()) || !IsExecutableTarget(applyMasks.address()) || !IsExecutableTarget(constructTexture.address()) ||
            !IsExecutableTarget(destroyTexture.address()) || !IsExecutableTarget(createRendererTexture.address()) || !IsReadable(renderer, sizeof(std::uintptr_t)) ||
            !IsReadable(tintTextureWidth, sizeof(*tintTextureWidth)) || !IsReadable(tintTextureHeight, sizeof(*tintTextureHeight)) ||
            !HasVerifiedVTable(testextureVtable) || !HasVerifiedVTable(niSourceTextureVtable))
            return false;
        if (!HasExactPrologue(createTexture.address(), kCreateTintTexturePrologue) || !HasExactPrologue(applyMasks.address(), kApplyTintMasksPrologue) ||
            !HasExactPrologue(constructTexture.address(), kTestextureCtorPrologue) || !HasExactPrologue(destroyTexture.address(), kTestextureDtorPrologue) ||
            !HasExactPrologue(createRendererTexture.address(), kCreateRenderTexturePrologue))
            return false;
        if (*tintTextureWidth == 0 || *tintTextureHeight == 0 || *tintTextureWidth > std::numeric_limits<std::uint16_t>::max() ||
            *tintTextureHeight > std::numeric_limits<std::uint16_t>::max())
            return false;

        a_targets = {
            createTexture.get(), applyMasks.get(), constructTexture.get(), destroyTexture.get(), createRendererTexture.get(), renderer, *tintTextureWidth,
            *tintTextureHeight, testextureVtable, niSourceTextureVtable,
        };
        return true;
    }
    catch (...)
    {
        return false;
    }
}

[[nodiscard]] bool IsValidInput(const std::span<const Tint> a_tints) noexcept
{
    if (a_tints.size() > SkyrimTogetherVR::GameplayBridge::kMaximumAppearanceTints)
        return false;
    for (const auto& tint : a_tints)
    {
        if (tint.Type >= static_cast<std::uint8_t>(RE::TintMask::Type::kTotal) || !std::isfinite(tint.Alpha) || tint.Alpha < 0.0F || tint.Alpha > 1.0F ||
            tint.TexturePath.size() > GameplayBridge::kMaximumAppearanceTexturePathBytes)
            return false;
    }
    return true;
}

[[nodiscard]] RE::BSLightingShaderMaterialFacegen* FindFaceMaterial(RE::Actor& a_actor, RE::BSLightingShaderProperty*& ar_shader) noexcept
{
    auto* faceObject = a_actor.GetHeadPartObject(RE::BGSHeadPart::HeadPartType::kFace);
    auto* geometry = faceObject ? faceObject->AsGeometry() : nullptr;
    auto* shader = geometry ? geometry->lightingShaderProp_cast() : nullptr;
    auto* material = shader ? shader->GetBaseMaterial() : nullptr;
    if (!shader || !material || material->GetFeature() != RE::BSShaderMaterial::Feature::kFaceGen)
        return nullptr;

    ar_shader = shader;
    return static_cast<RE::BSLightingShaderMaterialFacegen*>(material);
}

void DestroyMasks(
    const EngineTargets& a_targets, std::array<RE::TintMask*, GameplayBridge::kMaximumAppearanceTints>& a_masks,
    std::array<RE::TESTexture*, GameplayBridge::kMaximumAppearanceTints>& a_textures, const std::size_t a_count) noexcept
{
    for (std::size_t index = 0; index < a_count; ++index)
    {
        if (a_textures[index])
        {
            a_targets.DestroyTexture(a_textures[index], false);
            RE::free(a_textures[index]);
            a_textures[index] = nullptr;
        }
        RE::free(a_masks[index]);
        a_masks[index] = nullptr;
    }
}
} // namespace

bool HasVerifiedTargets() noexcept
{
    EngineTargets targets{};
    return TryGetTargets(targets);
}

CompositionResult Compose(RE::Actor& a_actor, const std::span<const Tint> a_tints) noexcept
{
    if (a_tints.empty())
        return CompositionResult::Success;
    if (!IsValidInput(a_tints))
        return CompositionResult::Rejected;

    EngineTargets targets{};
    std::array<RE::TintMask*, GameplayBridge::kMaximumAppearanceTints> masks{};
    std::array<RE::TESTexture*, GameplayBridge::kMaximumAppearanceTints> textures{};
    std::size_t constructed{};
    bool targetsReady{};
    const auto cleanup = [&]() noexcept
    {
        if (targetsReady)
            DestroyMasks(targets, masks, textures, constructed);
        constructed = 0;
    };

    try
    {
        if (!TryGetTargets(targets))
            return CompositionResult::Unavailable;
        targetsReady = true;

        RE::BSLightingShaderProperty* shader{};
        auto* material = FindFaceMaterial(a_actor, shader);
        if (!material)
            return CompositionResult::Inactive;

        // The face material remains alive until it owns the replacement texture.
        RE::NiPointer<RE::BSLightingShaderProperty> shaderOwner{shader};

        RE::BSTArray<RE::TintMask*> maskList;
        maskList.reserve(static_cast<RE::BSTArray<RE::TintMask*>::size_type>(a_tints.size()));
        for (std::size_t index = 0; index < a_tints.size(); ++index)
        {
            const auto& source = a_tints[index];
            auto* mask = RE::calloc<RE::TintMask>(1);
            auto* texture = RE::calloc<RE::TESTexture>(1);
            if (!mask || !texture)
            {
                RE::free(mask);
                RE::free(texture);
                cleanup();
                return CompositionResult::Rejected;
            }

            masks[index] = mask;
            textures[index] = texture;
            if (targets.ConstructTexture(texture) != texture || !HasExpectedVTable(texture, targets.TestextureVtable))
            {
                RE::free(mask);
                RE::free(texture);
                masks[index] = nullptr;
                textures[index] = nullptr;
                cleanup();
                return CompositionResult::Rejected;
            }
            ++constructed;

            // The verified constructor calls TESTexture::InitializeDataComponent.
            texture->textureName = source.TexturePath.c_str();
            mask->color = RE::Color(source.Color);
            mask->alpha = source.Alpha;
            mask->type = static_cast<RE::TintMask::Type>(source.Type);
            mask->texture = texture;
            maskList.push_back(mask);
        }

        RE::BSFixedString textureName{""};
        auto* renderedBase = targets.CreateTexture(textureName);
        if (!renderedBase || !HasExpectedVTable(renderedBase, targets.NiSourceTextureVtable))
        {
            cleanup();
            return CompositionResult::Rejected;
        }

        // CreateTintTexture is declared against the verified NiTexture base.
        // The exact vtable contract above proves this allocation is the source
        // texture implementation before narrowing it for rendererTexture.
        auto* rendered = static_cast<RE::NiSourceTexture*>(renderedBase);
        RE::NiPointer<RE::NiSourceTexture> renderedOwner{rendered};

        const auto rendererTexture = targets.CreateRendererTexture(targets.Renderer, targets.TintTextureWidth, targets.TintTextureHeight);
        if (!rendererTexture)
        {
            cleanup();
            return CompositionResult::Rejected;
        }
        // Both CommonLibVR renderer-texture spellings are the verified 0x28
        // allocation stored by the native remote path at NiSourceTexture+0x48.
        rendered->rendererTexture = reinterpret_cast<RE::BSGraphics::Texture*>(rendererTexture);

        targets.ApplyMasks(maskList, renderedBase);
        cleanup();

        material->tintTexture = renderedOwner;
        return CompositionResult::Success;
    }
    catch (...)
    {
        cleanup();
        return CompositionResult::Rejected;
    }
}
} // namespace SkyrimTogetherVR::GameplayAdapter::VRFaceGen
