target("SkyrimTogetherVRGameplayBridge")
    set_kind("shared")
    set_group("Client")
    set_basename("SkyrimTogetherVRGameplayBridge")
    set_languages("c++23")

    if not is_plat("windows") then
        set_default(false)
    end

    on_config(function(target)
        local se = has_config("skyrim_se") and true or false
        local ae = has_config("skyrim_ae") and true or false
        local vr = has_config("skyrim_vr") and true or false
        if se or ae or not vr then
            raise("SkyrimTogetherVRGameplayBridge requires exclusive Skyrim VR: configure skyrim_se=n, skyrim_ae=n, skyrim_vr=y.")
        end
    end)

    add_includedirs("..")
    add_deps("commonlibsse-ng")
    add_rules("commonlibsse-ng.plugin", {
        name = "SkyrimTogetherVRGameplayBridge",
        author = "Tilted Phoques",
        description = "Skyrim Together VR CommonLib gameplay adapter",
        options = {
            struct_dependent = true,
            address_library = true,
        },
    })
    add_files("*.cpp")
    add_packages("minhook")
    add_syslinks("kernel32")
