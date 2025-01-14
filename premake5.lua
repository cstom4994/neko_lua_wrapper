
workspace "nekolua"
configurations {"Debug", "Release"}

buildoptions {"/utf-8", "/Zc:__cplusplus", "/permissive", "/bigobj", "/Zc:preprocessor", "/Zc:wchar_t", "/Zc:forScope",
              "/MP"}
disablewarnings {"4005", "4244", "4305", "4127", "4481", "4100", "4512", "4018", "4099"}

cppdialect "C++20"
cdialect "C17"

local arch = "x86_64"

characterset("Unicode")

location "vsbuild"

flags {"MultiProcessorCompile"}
staticruntime "off"

filter "configurations:Debug"
do
    defines {"_DEBUG", "DEBUG", "_CONSOLE"}
    symbols "On"
    architecture(arch)
end

filter "configurations:Release"
do
    defines {"_NDEBUG", "NDEBUG"}
    optimize "On"
    architecture(arch)
end

project "nekolua"
do
    kind "ConsoleApp"
    language "C++"
    targetdir "./bin"
    debugdir "./bin"

    defines("NEKO_CFFI")

    includedirs {"."}

    files {"*.hpp", "*.h", "*.cpp"}

    files {"premake5.lua"}
end