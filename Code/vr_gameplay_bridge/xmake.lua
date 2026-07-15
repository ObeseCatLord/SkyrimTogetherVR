target("SkyrimTogetherVRGameplayBridge")
    set_kind("shared")
    set_group("Client")
    set_basename("SkyrimTogetherVRGameplayBridge")
    set_languages("c++23")

    if not is_plat("windows") then
        set_default(false)
    end

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
    add_syslinks("kernel32")
