-- YT2Ogg - YouTube to Opus Converter
-- Cross-platform build with xmake

set_xmakever("2.8.0")
set_project("YT2Ogg")
set_version("1.0.0")

set_license("MIT")

-- Global options
set_languages("c++26")

-- =====================
-- Main executable
-- =====================
target("YT2Ogg")
    set_kind("binary")
    add_rules("qt.widgetapp")
    
    -- Source files
    add_files("src/*.cpp")
    add_files("src/*.h")
    add_frameworks("QtNetwork")
    
    -- Platform libraries
    if is_plat("linux") then
        add_syslinks("pthread", "dl", "rt")
        add_cxflags("-ffunction-sections", "-fdata-sections", "-fno-semantic-interposition", "-O3")
        add_ldflags("-Wl,--gc-sections", "-Wl,-rpath,$ORIGIN")
    elseif is_plat("macos") then
        add_frameworks("Cocoa", "AppKit")
    elseif is_plat("windows") then
        add_syslinks("winmm", "ws2_32", "advapi32", "shell32")
    end
    
    -- Compiler optimizations (release)
    set_optimize("fastest")
    set_symbols("none")
    
    -- Install to bin/ alongside executable
    set_installdir("@bindir")
