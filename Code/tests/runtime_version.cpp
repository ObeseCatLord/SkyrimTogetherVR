#if defined(_WIN32)
#include "../immersive_launcher/utils/FileVersion.inl"

#include <catch2/catch.hpp>

#include <array>
#include <cwchar>
#include <string>

namespace
{
launcher::RuntimeVersion Parse(const std::wstring& acValue)
{
    launcher::RuntimeVersion version{};
    REQUIRE(launcher::ParseRuntimeVersionString(acValue.c_str(), acValue.size() + 1, version));
    return version;
}
} // namespace

TEST_CASE("runtime version parsing is exact and bounded", "[runtime-version][skyrim-vr]")
{
    const auto supported = Parse(L"1.4.15.0");
    REQUIRE(supported.IsSupported());

    const auto maximum = Parse(L"65535.65535.65535.65535");
    REQUIRE(maximum.Major == UINT16_MAX);
    REQUIRE(maximum.Build == UINT16_MAX);

    for (const auto* invalid : {
             L"", L"1.4.15", L"1.4.15.0.1", L"+1.4.15.0", L" 1.4.15.0", L"1.4.15.0 ", L"1.4.15.0beta", L"1..15.0",
             L"65536.4.15.0", L"0.0.0.0"})
    {
        launcher::RuntimeVersion version{9, 9, 9, 9};
        REQUIRE_FALSE(launcher::ParseRuntimeVersionString(invalid, std::wcslen(invalid) + 1, version));
        REQUIRE_FALSE(version.IsValid());
    }

    const wchar_t embeddedNull[]{L'1', L'.', L'4', L'\0', L'.', L'1', L'5', L'.', L'0', L'\0'};
    launcher::RuntimeVersion embeddedVersion{9, 9, 9, 9};
    REQUIRE_FALSE(launcher::ParseRuntimeVersionString(embeddedNull, std::size(embeddedNull), embeddedVersion));
    REQUIRE_FALSE(embeddedVersion.IsValid());
}

TEST_CASE("runtime version resource prefers translated strings over fixed root", "[runtime-version][skyrim-vr]")
{
    std::array<wchar_t, MAX_PATH> modulePath{};
    const DWORD pathLength = GetModuleFileNameW(nullptr, modulePath.data(), static_cast<DWORD>(modulePath.size()));
    REQUIRE(pathLength > 0);
    REQUIRE(pathLength < modulePath.size());

    launcher::RuntimeVersion version{};
    REQUIRE(launcher::QueryFileVersion(modulePath.data(), version));
    REQUIRE(version.IsSupported());
}
#endif
