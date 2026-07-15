-- TODO (Force): please align into namespace
lib_dir = os.curdir()
directx_dir = lib_dir .. "/DXSDK"

if is_plat("windows") then
	-- The VR gameplay adapter owns all CommonLib/SKSE engine interaction. Keep
	-- this as a pinned in-tree target so its layout and relocation ABI cannot
	-- drift independently of the plugin build.
	includes("./CommonLibSSE-NG")
	-- CommonLib is a standalone xmake project and sets directory-wide defaults
	-- while it is parsed. Restore the host project's settings before including
	-- the remaining Skyrim Together libraries and targets.
	set_project("SkyrimTogetherVR")
	set_languages("c99", "cxx20")
	set_warnings("all")
	includes("./TiltedUI")
	includes("./TiltedReverse")
	includes("./TiltedHooks")
end

includes("./TiltedConnect")
