local function istable(t) return type(t) == 'table' end

local function build_launcher()
    set_kind("binary")
    set_group("Client")
    set_symbols("debug", "hidden")

    add_ldflags(
        "/FORCE:MULTIPLE",
        "/IGNORE:4254,4006",
        "/DYNAMICBASE:NO",
        "/SAFESEH:NO",
        "/LARGEADDRESSAWARE",
        "/INCREMENTAL:NO",
        "/LAST:.zdata",
        "/SUBSYSTEM:WINDOWS",
        "/ENTRY:mainCRTStartup", { force = true })
    add_includedirs(
        ".",
        "../",
        "../../Libraries/")
    add_headerfiles("**.h")
    add_files(
        "**.cpp",
        "launcher.rc")
    add_deps(
        "ImmersiveElf",
        "TiltedReverse",
        "TiltedHooks",
        "TiltedUi",
        "ImGuiImpl",
        "CommonLib")
    add_links("ntdll_x64")
    add_linkdirs(".")
    add_syslinks(
        "user32",
        "shell32",
        "comdlg32",
        "bcrypt",
        "ole32",
        "dxgi",
        "d3d11",
        "gdi32",
        "SetupAPI",
        "Powrprof",
        "Cfgmgr32",
        "Propsys",
        "delayimp")

    add_packages(
        "tiltedcore",
        "spdlog",
        "minhook",
        "hopscotch-map",
        "cryptopp",
        "glm",
        "cef",
        "mem")
end

target("SkyrimVRImmersiveLauncher")
    set_basename("SkyrimTogetherVR")
    add_defines("TARGET_PREFIX=\"stvr\"")
    add_defines("TP_SKYRIM_VR=1")
    add_defines("TP_SKYRIM_VR_ENABLE_BRINGUP_HOOKS=1")
    add_defines("TP_SKYRIM_VR_ENABLE_UNVALIDATED_HOOKS=0")
    add_defines("TP_SKYRIM_VR_ENABLE_FLAT_OVERLAY=0")
    add_defines("TP_SKYRIM_VR_ENABLE_CONNECTION_ONLY=1")
    add_deps("SkyrimTogetherVRClient")
    add_ldflags("/WHOLEARCHIVE:SkyrimTogetherVRClient", { force = true })
    build_launcher()

target("SkyrimVRImmersiveLauncherAvatarSync")
    set_basename("SkyrimTogetherVRAvatarSync")
    add_defines("TARGET_PREFIX=\"stvr\"")
    add_defines("TP_SKYRIM_VR=1")
    add_defines("TP_SKYRIM_VR_ENABLE_BRINGUP_HOOKS=1")
    add_defines("TP_SKYRIM_VR_ENABLE_UNVALIDATED_HOOKS=0")
    add_defines("TP_SKYRIM_VR_ENABLE_FLAT_OVERLAY=0")
    add_defines("TP_SKYRIM_VR_ENABLE_CONNECTION_ONLY=1")
    add_defines("TP_SKYRIM_VR_ENABLE_REMOTE_AVATAR_SYNC=1")
    add_defines("TP_SKYRIM_VR_ENABLE_REMOTE_AVATAR_ACTOR_TARGETS=1")
    add_deps("SkyrimTogetherVRClientAvatarSync")
    add_ldflags("/WHOLEARCHIVE:SkyrimTogetherVRClientAvatarSync", { force = true })
    build_launcher()

target("SkyrimVRImmersiveLauncherGameplay")
    set_basename("SkyrimTogetherVRGameplay")
    add_defines("TARGET_PREFIX=\"stvr\"")
    add_defines("TP_SKYRIM_VR=1")
    add_defines("TP_SKYRIM_VR_ENABLE_BRINGUP_HOOKS=1")
    add_defines("TP_SKYRIM_VR_ENABLE_UNVALIDATED_HOOKS=0")
    add_defines("TP_SKYRIM_VR_ENABLE_FLAT_OVERLAY=0")
    add_defines("TP_SKYRIM_VR_ENABLE_CONNECTION_ONLY=0")
    add_defines("TP_SKYRIM_VR_ENABLE_REMOTE_AVATAR_SYNC=1")
    add_defines("TP_SKYRIM_VR_ENABLE_REMOTE_AVATAR_ACTOR_TARGETS=1")
    add_deps("SkyrimTogetherVRGameplayClient")
    add_ldflags("/WHOLEARCHIVE:SkyrimTogetherVRGameplayClient", { force = true })
    build_launcher()
