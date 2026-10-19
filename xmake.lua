set_xmakever("2.8.0")
set_project("YT2Ogg")
set_version("1.0.0")

set_license("MIT")

set_languages("c++26")

target("YT2Ogg")
    set_kind("binary")

    before_build(function (target)
        local generated_dir = path.join(target:autogendir(), "slint")
        local generated_header = path.join(generated_dir, "generated_ui.h")
        os.mkdir(generated_dir)
        os.execv("slint-compiler", {
            "--style", "native",
            "src/ui/mainwindow.slint",
            "-o", generated_header
        })
        target:add("includedirs", generated_dir)
    end)
    
    add_files("src/*.cpp")
    local slint_include_dir = os.getenv("SLINT_INCLUDE_DIR")
    local slint_library_dir = os.getenv("SLINT_LIBRARY_DIR")
    if slint_include_dir then
        add_includedirs(slint_include_dir)
    elseif is_plat("linux") then
        add_includedirs("/usr/include/slint")
    end
    if is_plat("windows") then
        add_includedirs("build/slint-generated", {tools = {"gcc"}})
    end
    if slint_library_dir then
        add_linkdirs(slint_library_dir)
    elseif is_plat("linux") then
        add_linkdirs("/usr/lib")
    end
    add_links("slint_cpp")
    if is_plat("linux") then
        add_syslinks("pthread", "dl", "rt")
        add_cxflags("-flto", "-ffunction-sections", "-fdata-sections", "-fno-semantic-interposition", "-fno-ident")
        add_ldflags("-flto", "-Wl,--gc-sections", "-Wl,--as-needed", "-Wl,--strip-all", "-Wl,-rpath,$ORIGIN")
    elseif is_plat("macos") then
        add_frameworks("Cocoa", "AppKit")
        add_cxflags("-flto=thin")
        add_ldflags("-Wl,-dead_strip", "-Wl,-dead_strip_dylibs", "-Wl,-S")
    elseif is_plat("windows") then
        add_syslinks("shell32", "ole32", "uuid", "ws2_32", "ntdll", "uxtheme", "imm32")
        add_cxflags("-ffunction-sections", "-fdata-sections", {tools = {"gcc"}})
        add_cxflags("-include", "src/slint-static/disable_import.h",
                    {tools = {"gcc"}, force = true})
        add_ldflags("-Wl,--gc-sections", "-static", "-static-libgcc",
                    "-static-libstdc++", "-mwindows", {tools = {"gcc"}})
        add_cxflags("/GL", "/Gy", "/Zl", {tools = {"cl", "clang_cl"}})
        add_ldflags("/LTCG", "/OPT:REF", "/OPT:ICF", "/SUBSYSTEM:WINDOWS",
                    {tools = {"cl", "clang_cl"}})
        set_runtimes("MT")
        if os.isfile("assets/yt-dlp.exe") and os.isfile("assets/yt-dlp-version.txt") then
            add_files("src/yt-dlp.rc")
        end
    end
    
    set_optimize("smallest")
    set_symbols("none")
    
    set_installdir("@bindir")
