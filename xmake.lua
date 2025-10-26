set_project("booksim2")
set_version("2.0.0")
add_rules("plugin.compile_commands.autoupdate", {outputdir = ".vscode"})

-- use c++11 or higher
set_languages("cxx17")
set_toolchains("clang")

-- global warnings/optimization similar to Makefile
add_rules("mode.release", "mode.debug")
set_warnings("all")

-- common include dirs and flags
local incs = {"src", "src/arbiters", "src/allocators", "src/routers", "src/networks", "src/power"}
local function apply_common()
    add_includedirs(table.unpack(incs))
end

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
    before_prepare(function (target)
        local y = path.join("src", "config.y")
        local l = path.join("src", "config.l")
        local y_c = path.join("src", "y.tab.c")
        local y_h = path.join("src", "y.tab.h")
        local lex_c = path.join("src", "lex.yy.c")
        os.tryrm(y_c)
        os.tryrm(y_h)
        os.tryrm(lex_c)
        os.vrun("bison -y -d %s -o %s", y, y_c)
        os.vrun("flex -o %s %s", lex_c, l)
        target:add("files", lex_c)
        target:add("files", y_c)
    end)
    set_kind("binary")
    apply_common()
    add_deps("allocators", "arbiters", "routers", "networks", "power")
    add_files(path.join("src", "*.cpp"))
    add_ldflags("-Wl,--start-group -lrouters -lnetworks -larbiters -lallocators -lpower -Wl,--end-group", {force = true})

task("bs2")
    set_menu {
        usage = "xmake bs2 <config_file>",
        description = "Run BookSim simulator with specified config file",
        options = {
            {'c', "--config-file", "kv", "vda_twin_config", "Config file name (without path)"}
        }
    }
    on_run(function (options)
        import("core.base.option")
        local bin = path.join(os.projectdir(), "build", "linux", "x86_64", "release", "booksim")
        local config_file = path.join(os.projectdir(), "runfiles", option.get("--config-file"))
        print("%s %s", bin, config_file)
        os.exec("%s %s", bin, config_file)
    end)