set_project("booksim2")
set_version("2.0.0")
add_rules("plugin.compile_commands.autoupdate", {outputdir = ".vscode"})

-- use c++11 or higher
set_languages("cxx17")

-- global warnings/optimization similar to Makefile
add_rules("mode.release", "mode.debug")
set_warnings("all")

-- common include dirs and flags
local incs = {"src", "src/arbiters", "src/allocators", "src/routers", "src/networks", "src/power"}
local function apply_common()
    add_includedirs(table.unpack(incs))
    add_cxxflags("-O3", {force = true})
    add_cxxflags("-g", {force = true})
end

-- helper: generate parser sources if missing/outdated
local function gen_parser()
    local y = path.join("src", "config.y")
    local l = path.join("src", "config.l")
    local y_c = path.join("src", "y.tab.c")
    local y_h = path.join("src", "y.tab.h")
    local lex_c = path.join("src", "lex.yy.c")
    -- run bison if missing or older
    if (not os.isfile(y_c)) or os.mtime(y) > os.mtime(y_c) then
        os.vrun("bison -y -d %s", y)
    end
    -- run flex if missing or older or header newer
    if (not os.isfile(lex_c)) or os.mtime(l) > os.mtime(lex_c) or (os.isfile(y_h) and os.mtime(y_h) > os.mtime(lex_c)) then
        os.vrun("flex %s", l)
    end
end

-- parser library (flex/bison)
-- Provides yyparse/yylex and config parser

target("parser")
    set_kind("static")
    apply_common()
    before_build(function (target)
        gen_parser()
    end)
    add_files("src/y.tab.c")
    add_files("src/lex.yy.c")

-- core library (root src/*.cpp excluding main)

target("core")
    set_kind("static")
    apply_common()
    add_deps("parser")
    add_files("src/*.cpp")
    remove_files("src/main.cpp")

-- submodule: allocators

target("allocators")
    set_kind("static")
    apply_common()
    add_files("src/allocators/*.cpp")

-- submodule: arbiters

target("arbiters")
    set_kind("static")
    apply_common()
    add_files("src/arbiters/*.cpp")

-- submodule: routers

target("routers")
    set_kind("static")
    apply_common()
    add_files("src/routers/*.cpp")

-- submodule: networks

target("networks")
    set_kind("static")
    apply_common()
    add_files("src/networks/*.cpp")

-- submodule: power

target("power")
    set_kind("static")
    apply_common()
    add_files("src/power/*.cpp")

-- final binary: booksim

target("booksim")
    set_kind("binary")
    apply_common()
    add_files("src/main.cpp")
    add_deps("parser", "core", "allocators", "arbiters", "routers", "networks", "power")
    add_linkdirs("$(builddir)", "$(builddir)/linux/x86_64/$(mode)")
    -- ensure static libs resolve regardless of order using linker group
    add_ldflags("-Wl,--start-group -lrouters -lnetworks -larbiters -lallocators -lpower -lcore -lparser -Wl,--end-group", {force = true})

task("bs2")
    set_menu {
        usage = "xmake bs2 <config_file>",
        description = "Run BookSim simulator with specified config file",
        options = {
            {'c', "--config-file", "kv", "meshconfig", "Config file name (without path)"}
        }
    }
    on_run(function (options)
        import("core.base.option")
        local bin = path.join(os.projectdir(), "build", "linux", "x86_64", "release", "booksim")
        local config_file = path.join(os.projectdir(), "runfiles", option.get("--config-file"))
        print("%s %s", bin, config_file)
        os.exec("%s %s", bin, config_file)
    end)