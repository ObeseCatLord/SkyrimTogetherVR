if is_plat("windows") then
    includes("client")
    includes("vrik_bridge")
    includes("higgs_bridge")
    includes("planck_bridge")
    includes("immersive_elf")
    includes("immersive_launcher")
    includes("tp_process")
else
    local vr_targets = {
        "SkyrimTogetherVRClient",
        "SkyrimTogetherVRClientAvatarSync",
        "SkyrimTogetherVRGameplayClient",
        "SkyrimTogetherVRVrikBridge",
        "SkyrimTogetherVRHiggsBridge",
        "SkyrimTogetherVRPlanckBridge",
        "SkyrimVRImmersiveLauncher",
        "SkyrimVRImmersiveLauncherAvatarSync",
        "SkyrimVRImmersiveLauncherGameplay",
        "ImmersiveElf",
        "TPProcess",
    }

    local mode = "releasedbg"
    if is_mode("debug") then
        mode = "debug"
    elseif is_mode("release") then
        mode = "release"
    end

    local function build_wine_target(name)
        target(name)
            set_kind("phony")
            set_group("Client")
            set_default(false)
            on_build(function(target)
                local script = path.join(os.projectdir(), "BuildSkyrimTogetherVR-Wine.sh")
                os.execv(script, {
                    "-Mode", mode,
                    "-Targets", target:name(),
                    "-NoPackage"
                })
            end)
    end

    for _, target_name in ipairs(vr_targets) do
        build_wine_target(target_name)
    end
end

includes("common")
includes("components")
includes("base")
includes("admin_protocol")
includes("server_runner")
includes("server")
includes("encoding")
includes("tests")
