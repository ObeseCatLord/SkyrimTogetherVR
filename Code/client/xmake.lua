
local function vr_define_value(value)
    return value and "1" or "0"
end

local function build_vr_client(name, options)
    options = options or {}
    local connection_only = options.connection_only
    if connection_only == nil then
        connection_only = true
    end
    local remote_avatar_sync = options.remote_avatar_sync or false
    local remote_avatar_actor_targets = options.remote_avatar_actor_targets or false
    local observation_services = options.observation_services or false
    local pose_service = options.pose_service or false
    local remote_player_proxy = options.remote_player_proxy or false
    local unvalidated_hooks = options.unvalidated_hooks or false
    local flat_overlay = options.flat_overlay or false

target(name)
    set_kind("static")
    set_group("Client")
    add_includedirs(".", "../", "../../Libraries/")
    set_pcxxheader("TiltedOnlinePCH.h")
    add_defines("TP_SKYRIM_VR=1")
    add_defines("TP_SKYRIM_VR_ENABLE_BRINGUP_HOOKS=1")
    add_defines("TP_SKYRIM_VR_ENABLE_UNVALIDATED_HOOKS=" .. vr_define_value(unvalidated_hooks))
    add_defines("TP_SKYRIM_VR_ENABLE_FLAT_OVERLAY=" .. vr_define_value(flat_overlay))
    add_defines("TP_SKYRIM_VR_ENABLE_CONNECTION_ONLY=" .. vr_define_value(connection_only))
    add_defines("TP_SKYRIM_VR_ENABLE_REMOTE_AVATAR_SYNC=" .. vr_define_value(remote_avatar_sync))
    add_defines("TP_SKYRIM_VR_ENABLE_DISCOVERY_SERVICE=1")
    add_defines("TP_SKYRIM_VR_ENABLE_PLAYER_CELL_SERVICE=1")
    add_defines("TP_SKYRIM_VR_ENABLE_MOVEMENT_OBSERVATION_SERVICE=" .. vr_define_value(observation_services))
    add_defines("TP_SKYRIM_VR_ENABLE_INVENTORY_OBSERVATION_SERVICE=" .. vr_define_value(observation_services))
    add_defines("TP_SKYRIM_VR_ENABLE_ACTIVATION_OBSERVATION_SERVICE=" .. vr_define_value(observation_services))
    add_defines("TP_SKYRIM_VR_ENABLE_MAGIC_OBSERVATION_SERVICE=" .. vr_define_value(observation_services))
    add_defines("TP_SKYRIM_VR_ENABLE_COMBAT_OBSERVATION_SERVICE=" .. vr_define_value(observation_services))
    add_defines("TP_SKYRIM_VR_ENABLE_PROJECTILE_OBSERVATION_SERVICE=" .. vr_define_value(observation_services))
    add_defines("TP_SKYRIM_VR_ENABLE_GRAB_OBSERVATION_SERVICE=" .. vr_define_value(observation_services))
    add_defines("TP_SKYRIM_VR_ENABLE_HIGGS_OBSERVATION_SERVICE=" .. vr_define_value(observation_services))
    add_defines("TP_SKYRIM_VR_ENABLE_SAVELOAD_OBSERVATION_SERVICE=" .. vr_define_value(observation_services))
    add_defines("TP_SKYRIM_VR_ENABLE_POSE_SERVICE=" .. vr_define_value(pose_service))
    add_defines("TP_SKYRIM_VR_ENABLE_BODY_POSE_CAPTURE=" .. vr_define_value(pose_service))
    add_defines("TP_SKYRIM_VR_ENABLE_REMOTE_PLAYER_PROXY_SERVICE=" .. vr_define_value(remote_player_proxy))
    add_defines("TP_SKYRIM_VR_ENABLE_VALIDATED_INLINE_PATCHES=0")
    add_defines("TP_SKYRIM_VR_ENABLE_REMOTE_AVATAR_ACTOR_TARGETS=" .. vr_define_value(remote_avatar_actor_targets))

    -- exclude game specifc stuff
    add_headerfiles("**.h|Games/Skyrim/**|Services/Vivox/**")
    add_files("**.cpp|Games/Skyrim/**|Services/Vivox/**")

    after_install(function(target)
        -- copy dlls
        for _, pkg_with_dlls in ipairs({"cef", "discord"}) do
            local linkdir = target:pkg(pkg_with_dlls):get("linkdirs")
            local bindir = path.join(linkdir, "..", "bin")
            os.cp(bindir, target:installdir())
        end
        -- copy ui
        local uidir = path.join(target:scriptdir(), "..", "skyrim_ui", "src")
        os.cp(path.join(uidir, "assets", "images", "cursor.dds"), path.join(target:installdir(), "bin", "assets", "images", "cursor.dds"))
        os.cp(path.join(uidir, "assets", "images", "cursor.png"), path.join(target:installdir(), "bin", "assets", "images", "cursor.png"))
        os.rm(path.join(target:installdir(), "bin", "**Tests.exe"))
    end)

    add_files("Games/Skyrim/**.cpp")
    add_headerfiles("Games/Skyrim/**.h")
    -- rather hacky:
    add_includedirs("Games/Skyrim")
    add_deps("SkyrimEncoding")
    add_deps(
        "UiProcess",
        "CommonLib",
        "BaseLib",
        "ImGuiImpl",
        "TiltedConnect",
        "TiltedReverse",
        "TiltedHooks",
        "TiltedUi",
        {inherit = true}
    )

    add_packages(
        "tiltedcore",
        "spdlog",
        "fmt",
        "hopscotch-map",
        "cryptopp",
        "gamenetworkingsockets",
        "discord",
        "imgui",
        "cef",
        "minhook",
        "entt",
        "glm",
        "mem",
        "xbyak")

    if has_config("vivox") then
        add_files("Services/Vivox/**.cpp")
        add_headerfiles("Services/Vivox/**.h")
        add_includedirs("Services/Vivox")
        add_deps("Vivox")
        add_defines("TP_VIVOX=1")
    else
        add_defines("TP_VIVOX=0")
    end

    add_syslinks(
        "version",
        "dbghelp",
        "kernel32")
end

add_requires("tiltedcore")

build_vr_client("SkyrimTogetherVRClient")
build_vr_client("SkyrimTogetherVRClientAvatarSync", {
    remote_avatar_sync = true,
    remote_avatar_actor_targets = true,
    observation_services = true,
    pose_service = true,
    remote_player_proxy = true,
})
build_vr_client("SkyrimTogetherVRGameplayClient", {
    connection_only = false,
    remote_avatar_sync = true,
    remote_avatar_actor_targets = true,
    observation_services = true,
    pose_service = true,
    remote_player_proxy = true,
})
