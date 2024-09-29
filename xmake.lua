add_rules("mode.debug", "mode.release")

add_rules("plugin.compile_commands.autoupdate", {
    outputdir = "build"
})

set_languages("c++20")

add_requires("lua")

set_exceptions("cxx", "objc")

add_defines("UNICODE", "_UNICODE")

if is_plat("windows") then
    add_defines("WIN32", "_WIN32", "_WINDOWS", "NOMINMAX")
    add_cxflags("/utf-8", "/Zc:__cplusplus", "/permissive", "/bigobj", "/Zc:preprocessor", "/Zc:wchar_t",
        "/Zc:forScope", "/MP")
    add_syslinks("ws2_32", "wininet")
elseif is_plat("linux") then
    add_cxflags("-Wtautological-compare")
    add_cxflags("-fno-strict-aliasing", "-fms-extensions", "-finline-functions", "-fPIC")

    add_syslinks("GL")
elseif is_plat("macosx") then
    add_cxflags("-Wtautological-compare")
    add_cxflags("-fno-strict-aliasing", "-fms-extensions", "-finline-functions", "-fPIC")
end

if is_mode("release") then
    set_runtimes("MT")
else
    set_runtimes("MTd")
end

target("nekolua")
do
    set_kind("binary")
    add_headerfiles("**.hpp")
    add_files("test.cpp", "luax.cpp")
    add_packages("lua")

    add_defines("NEKO_CFFI")

    set_targetdir("./")
    set_rundir("./")
end
