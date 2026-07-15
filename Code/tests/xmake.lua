
if is_plat("windows") then
target("TPCrashHandlerProbe")
    set_kind("binary")
    set_group("Tests")
    add_defines("TP_SKYRIM_VR=1", "TP_CRASH_HANDLER_TESTING=1")
    add_includedirs("../client", "../../build")
    add_files("probe/crash_handler_probe.cpp", "../client/CrashHandler.cpp")
    add_packages("spdlog")
    add_syslinks("dbghelp", "kernel32")
end

target("TPTests")
    set_kind("binary")
    set_group("Tests")
    add_includedirs(
        ".", "../encoding", "../client")
    add_headerfiles("**.h")
    add_files("*.cpp|version_db.cpp")
    add_deps("SkyrimEncoding")
    add_packages(
        "tiltedcore",
        "hopscotch-map",
        "catch2",
        "entt",
        "mimalloc",
        "glm")

    if is_plat("windows") then
        add_defines("TP_SKYRIM_VR=1", "TP_CRASH_HANDLER_TESTING=1")
        add_includedirs("../../build")
        add_files("version_db.cpp", "runtime_version.rc", "../client/CrashHandler.cpp")
        add_deps("TPCrashHandlerProbe")
        add_packages("spdlog")
        add_syslinks("version", "dbghelp", "kernel32")
    end
