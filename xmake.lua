add_rules("mode.debug", "mode.release")
add_requires("vulkansdk", "libsdl3","imgui", "stb", "assimp", "cereal", "spdlog", "eventpp", "meshoptimizer", "metis", "mikktspace", "eigen", "stduuid")
set_encodings("utf-8")

target("renderer")
    set_languages("c++20")
    set_kind("binary")
    add_files("src/**.cpp", "thirdparty/**.cpp", "thirdparty/**.c")
    add_files("thirdparty/lemon/lemon/arg_parser.cc",
              "thirdparty/lemon/lemon/base.cc",
              "thirdparty/lemon/lemon/color.cc",
              "thirdparty/lemon/lemon/lp_base.cc",
              "thirdparty/lemon/lemon/lp_skeleton.cc",
              "thirdparty/lemon/lemon/random.cc",
              "thirdparty/lemon/lemon/bits/windows.cc")
    add_headerfiles("src/**.h", "src/**.hpp")
    add_extrafiles("Asset/BuildIn/Shader/**.comp",
                    "Asset/BuildIn/Shader/**.glsl",
                    "Asset/BuildIn/Shader/**.vert",
                    "Asset/BuildIn/Shader/**.frag",
                    "Asset/BuildIn/Shader/**.geom",
                    "Asset/BuildIn/Shader/**.rgen",
                    "Asset/BuildIn/Shader/**.rmiss",
                    "Asset/BuildIn/Shader/**.rchit",
                    "Asset/BuildIn/Shader/**.hlsli")
    remove_files("thirdparty/NRD/_Build/**.cpp", "thirdparty/NRD/_Build/**.c","thirdparty/imgui/imgui_impl_glfw.cpp",
                  "thirdparty/ShaderMake/**/_Build/**.cpp")
    add_includedirs("src/Runtime/")
    add_includedirs("src/Editor/")
    add_includedirs("thirdparty/vma",
                    "thirdparty/volk",
                    "thirdparty/imgui",
                    "thirdparty/imguizmo",
                    "thirdparty/implot",
                    "thirdparty/imgui-flame-graph",
                    "thirdparty/imgui-node-editor",
                    "thirdparty/spirv_reflect",
                    "thirdparty/smhasher/src",
                    "thirdparty/NRD/Include",
                    "thirdparty/NRD/Shaders",
                    "thirdparty/NRD/Shaders/Include",
                    "thirdparty/NRD/_Shaders",
                    "thirdparty/NRD/Source",
                    "Asset/BuildIn/Shader/nrd",
                    "thirdparty/ShaderMake",
                    "thirdparty/lemon",
                    "thirdparty/MathLib")                
    add_packages("vulkansdk", "libsdl3","imgui", "stb", "assimp", "cereal", "spdlog", "eventpp", "meshoptimizer", "metis", "mikktspace", "eigen", "stduuid")
    if is_plat("windows") then
        add_defines("WIN32")
        add_syslinks("ole32")
    else
        add_syslinks("pthread")
    end

--
-- If you want to known more usage about xmake, please see https://xmake.io
--
-- ## FAQ
--
-- You can enter the project directory firstly before building project.
--
--   $ cd projectdir
--
-- 1. How to build project?
--
--   $ xmake6
--
-- 2. How to configure project?
--
--   $ xmake f -p [macosx|linux|iphoneos ..] -a [x86_64|i386|arm64 ..] -m [debug|release]
--
-- 3. Where is the build output directory?
--
--   The default output directory is `./build` and you can configure the output directory.
--
--   $ xmake f -o outputdir
--   $ xmake
--
-- 4. How to run and debug target after building project?
--
--   $ xmake run [targetname]
--   $ xmake run -d [targetname]
--
-- 5. How to install target to the system directory or other output directory?
--
--   $ xmake install
--   $ xmake install -o installdir
--
-- 6. Add some frequently-used compilation flags in xmake.lua
--
-- @code
--    -- add debug and release modes
--    add_rules("mode.debug", "mode.release")
--
--    -- add macro definition
--    add_defines("NDEBUG", "_GNU_SOURCE=1")
--
--    -- set warning all as error
--    set_warnings("all", "error")
--
--    -- set language: c99, c++11
--    set_languages("c99", "c++11")
--
--    -- set optimization: none, faster, fastest, smallest
--    set_optimize("fastest")
--
--    -- add include search directories
--    add_includedirs("/usr/include", "/usr/local/include")
--
--    -- add link libraries and search directories
--    add_links("tbox")
--    add_linkdirs("/usr/local/lib", "/usr/lib")
--
--    -- add system link libraries
--    add_syslinks("z", "pthread")
--
--    -- add compilation and link flags
--    add_cxflags("-stdnolib", "-fno-strict-aliasing")
--    add_ldflags("-L/usr/local/lib", "-lpthread", {force = true})
--
-- @endcode
--

