#pragma once

#include "Loader/ExeLoader.h"
#include <TiltedCore/Filesystem.hpp>
#include <TiltedCore/Stl.hpp>

#include <cstdint>

namespace launcher
{
namespace fs = std::filesystem;

enum class Result
{
    kSuccess,
    kBadPlatform,
    kBadInstall
};

struct RuntimeVersion
{
    uint16_t Major{};
    uint16_t Minor{};
    uint16_t Revision{};
    uint16_t Build{};

    [[nodiscard]] bool IsValid() const noexcept { return Major != 0 || Minor != 0 || Revision != 0 || Build != 0; }
};

// stays alive through the entire duration of the game.
struct LaunchContext
{
    fs::path exePath;
    fs::path gamePath;
    ExeLoader::TEntryPoint gameMain = nullptr;

    void SetLoaded() { isLoaded = true; }   // If loaded, need to spoof GetModuleFileName*(nullptr)
    bool GetLoaded();

  private:
    bool isLoaded{false};
};

LaunchContext* GetLaunchContext();

bool LoadProgram(LaunchContext&, RuntimeVersion&);
int StartUp(int argc, char** argv);

void InitClient();

bool HandleArguments(int, char**, bool&);

} // namespace launcher
