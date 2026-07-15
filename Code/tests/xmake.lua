
target("TPTests")
    set_kind("binary")
    set_group("Tests")
    add_includedirs(
        ".", "../encoding")
    add_headerfiles("**.h")
    add_files("*.cpp|version_db.cpp")
    add_deps("SkyrimEncoding")
    add_packages(
        "tiltedcore",
        "hopscotch-map",
        "catch2",
        "mimalloc",
        "glm")

    if is_plat("windows") then
        add_includedirs("../client")
        add_defines("TP_SKYRIM_VR=1")
        add_files("version_db.cpp", "runtime_version.rc")
        add_syslinks("version")
    end
