#pragma once

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <vr_common/VRAnimationGraphProtocol.h>

namespace RE { class Actor; class BSAnimationGraphManager; }

namespace SkyrimTogetherVR::GameplayAdapter::AnimationGraphs
{
// Mirrors the desktop SkyrimTogetherRebornBehaviors convention.  The bridge
// owns this small parser because the desktop BehaviorVar implementation is
// intentionally unavailable to the CommonLib-only VR runtime.
struct ConfiguredAnimationGraphDescriptor
{
    std::uint64_t OriginalKey{};
    std::string Signature;
    std::string Name;
    std::vector<std::string> Booleans;
    std::vector<std::string> Floats;
    std::vector<std::string> Integers;
};

[[nodiscard]] inline std::vector<std::string> ReadConfiguredAnimationVariableFile(const std::filesystem::path& a_path)
{
    std::vector<std::string> result;
    std::ifstream file(a_path);
    std::string line;
    while (std::getline(file, line)) {
        const auto first = line.find_first_not_of(" \t\r\n");
        if (first == std::string::npos)
            continue;
        const auto last = line.find_last_not_of(" \t\r\n");
        result.push_back(line.substr(first, last - first + 1));
    }
    return result;
}

[[nodiscard]] inline std::vector<ConfiguredAnimationGraphDescriptor> LoadConfiguredAnimationGraphDescriptors(const std::filesystem::path& a_root)
{
    std::vector<ConfiguredAnimationGraphDescriptor> result;
    std::error_code error;
    std::filesystem::directory_iterator directories(a_root, error);
    if (error)
        return result;

    std::vector<std::filesystem::directory_entry> directoryEntries;
    for (const auto& directory : directories)
        directoryEntries.push_back(directory);
    std::sort(directoryEntries.begin(), directoryEntries.end(), [](const auto& a_left, const auto& a_right) {
        return a_left.path().filename().native() < a_right.path().filename().native();
    });

    for (const auto& directory : directoryEntries)
    {
        if (!directory.is_directory(error) || error)
        {
            error.clear();
            continue;
        }

        ConfiguredAnimationGraphDescriptor descriptor;
        descriptor.Name = directory.path().filename().string();
        std::filesystem::directory_iterator files(directory.path(), error);
        if (error)
        {
            error.clear();
            continue;
        }
        std::vector<std::filesystem::directory_entry> fileEntries;
        for (const auto& file : files)
            fileEntries.push_back(file);
        std::sort(fileEntries.begin(), fileEntries.end(), [](const auto& a_left, const auto& a_right) {
            return a_left.path().filename().native() < a_right.path().filename().native();
        });
        for (const auto& file : fileEntries)
        {
            if (!file.is_regular_file(error) || error)
            {
                error.clear();
                continue;
            }
            const auto name = file.path().filename().string();
            if (name.find("__sig.txt") != std::string::npos)
            {
                auto signature = ReadConfiguredAnimationVariableFile(file.path());
                if (!signature.empty())
                {
                    descriptor.Signature = std::move(signature.front());
                    descriptor.Signature.erase(
                        std::remove_if(descriptor.Signature.begin(), descriptor.Signature.end(), [](const unsigned char a_character) { return std::isspace(a_character) != 0; }),
                        descriptor.Signature.end());
                }
            }
            else if (name.find("__hash.txt") != std::string::npos)
            {
                auto hash = ReadConfiguredAnimationVariableFile(file.path());
                if (!hash.empty())
                {
                    hash.front().erase(
                        std::remove_if(hash.front().begin(), hash.front().end(), [](const unsigned char a_character) { return std::isspace(a_character) != 0; }),
                        hash.front().end());
                    const auto begin = hash.front().data();
                    const auto end = begin + hash.front().size();
                    std::from_chars(begin, end, descriptor.OriginalKey, 10);
                }
            }
            else if (name.find("__bool.txt") != std::string::npos)
            {
                auto variables = ReadConfiguredAnimationVariableFile(file.path());
                descriptor.Booleans.insert(descriptor.Booleans.end(), std::make_move_iterator(variables.begin()), std::make_move_iterator(variables.end()));
            }
            else if (name.find("__float.txt") != std::string::npos)
            {
                auto variables = ReadConfiguredAnimationVariableFile(file.path());
                descriptor.Floats.insert(descriptor.Floats.end(), std::make_move_iterator(variables.begin()), std::make_move_iterator(variables.end()));
            }
            else if (name.find("__int.txt") != std::string::npos)
            {
                auto variables = ReadConfiguredAnimationVariableFile(file.path());
                descriptor.Integers.insert(descriptor.Integers.end(), std::make_move_iterator(variables.begin()), std::make_move_iterator(variables.end()));
            }
        }
        if (!descriptor.Signature.empty())
            result.push_back(std::move(descriptor));
    }
    return result;
}

struct AnimationGraphDescriptor
{
    std::uint64_t Key{};
    std::span<const std::string_view> Booleans{};
    std::span<const std::string_view> Floats{};
    std::span<const std::string_view> Integers{};
    std::uint64_t Digest{};
    std::uint16_t DirectionFloatIndex{};
    [[nodiscard]] constexpr std::size_t VariableCount() const noexcept
    { return Booleans.size() + Floats.size() + Integers.size(); }
};

struct ResolvedDescriptor
{
    const AnimationGraphDescriptor* Descriptor{};
    const RE::BSAnimationGraphManager* Manager{};
    [[nodiscard]] explicit operator bool() const noexcept { return Descriptor != nullptr && Manager != nullptr; }
};

[[nodiscard]] std::span<const AnimationGraphDescriptor> Catalog() noexcept;
void ResetCache() noexcept;
[[nodiscard]] bool Resolve(RE::Actor& a_actor, ResolvedDescriptor& ar_result) noexcept;
[[nodiscard]] bool ManagerMatches(RE::Actor& a_actor, const ResolvedDescriptor& a_descriptor) noexcept;
[[nodiscard]] bool Validate(RE::Actor& a_actor, const ResolvedDescriptor& a_descriptor) noexcept;
[[nodiscard]] bool Capture(RE::Actor& a_actor, AnimationGraphProtocol::SnapshotBuffer& ar_snapshot) noexcept;
// Captures a rollback snapshot only when the actor's current graph matches the
// complete snapshot that is about to be applied. This keeps preflight reads
// separate from graph mutation.
[[nodiscard]] bool CaptureForApply(RE::Actor& a_actor,
                                   const AnimationGraphProtocol::SnapshotBuffer& a_expected,
                                   AnimationGraphProtocol::SnapshotBuffer& ar_previous) noexcept;
[[nodiscard]] bool MatchesCounts(const ResolvedDescriptor& a_descriptor,
                                 const AnimationGraphProtocol::SnapshotBuffer& a_snapshot) noexcept;
[[nodiscard]] bool GetContract(const AnimationGraphDescriptor& a_descriptor,
                               std::uint64_t& ar_digest,
                               std::uint16_t& ar_directionFloatIndex) noexcept;
} // namespace SkyrimTogetherVR::GameplayAdapter::AnimationGraphs
