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
    if is_plat("linux") then
        add_ldflags("-Wl,--start-group -lrouters -lnetworks -larbiters -lallocators -lpower -Wl,--end-group", {force = true})
    end

task("bs2")
    set_menu {
        usage = "xmake bs2 <config_file>",
        description = "Run BookSim simulator with specified config file",
        options = {
            {'d', "deterministic", "k", nil, "Deterministic"},
            {'C', "config-file", "kv", "chiplet", "Config file name (without path and extension)"},
            {'R', "rc-function", "kv", "dor", "Routing computation function (dor)"},
            {'A', "va-function", "kv", "vda", "VC alloc function (vda, red, mvn, rc)"},
            {'t', "topology", "kv", "twin", "Topology (twin, mesh, p2p)"},
            {'T', "traffic", "kv", "uniform", "Traffic type (uniform, bitcomp, shuffle)"},
            {'I', "injection-rate", "kv", "0.001", "Injection rate"},
            {'W', "watch-out", "kv", "", "Watch out file name (without path and extension)"},
            {'X', "watch-flits", "kv", "", "Watch flits name"},
            {'L', "latency-threshold", "kv", "512.0", "Latency threshold"},
            {'S', "inject-rate-step", "kv", "", "Sim from --injection-rate by --inject-rate-step until booksim return non-zero value"}
        }
    }
    on_run(function (options)
        import("core.base.option")
        local bin = path.join(os.projectdir(), "build", os.host(), os.arch(), "release", "booksim")
        local config_file = path.join(os.projectdir(), "runfiles", option.get("config-file") .. "config")
        local opts = {config_file}
        if option.get("deterministic") then table.join2(opts, { "deterministic=1" }) end
        table.join2(opts, { "topology=" .. option.get("config-file") .. "_" .. option.get("topology") })
        
        if option.get("watch-out") ~= "" then table.join2(opts, { "watch_out=" .. option.get("watch-out") }) end
        if option.get("watch-out") ~= "" then table.join2(opts, { "watch_flits=" .. option.get("watch-flits") }) end
        if option.get("va-function") == "rc" then table.join2(opts, { "use_rc_buffer=1", "num_vcs=4" }) end
        if option.get("va-function") == "vda" or option.get("va-function") == "red" then table.join2(opts, { "wait_for_tail_credit=1"}) end
        table.join2(opts, { "routing_function=" .. option.get("rc-function") .. "_" .. option.get("va-function") })
        table.join2(opts, { "latency_thres=" .. option.get("latency-threshold") })
        table.join2(opts, { "traffic=" .. option.get("traffic") })

        local log_file = ""
        if option.get("deterministic") then 
            log_file = "ord"
        else
            log_file = "rnd"
        end
        local log_dir = path.join(os.projectdir(), "logs")
        if not os.exists(log_dir) then os.mkdir(log_dir) end
        local tfc_dir = path.join(log_dir, option.get("traffic"))
        if not os.exists(tfc_dir) then os.mkdir(tfc_dir) end
        local topo_dir = path.join(tfc_dir, option.get("topology"))
        if not os.exists(topo_dir) then os.mkdir(topo_dir) end
        local va_dir = path.join(topo_dir, option.get("va-function"))
        if not os.exists(va_dir) then os.mkdir(va_dir) end

        local iter_count = 0
        local step = 0.0
        if option.get("inject-rate-step") ~= "" then
            iter_count = 1000
            step = tonumber(option.get("inject-rate-step"))
        end

        for i = 0,iter_count,1 do
            local iter_inj_rate = tonumber(option.get("injection-rate")) + step * i
            local iter_log = log_file .. "_" .. iter_inj_rate .. ".log"
            local iter_opts = {}
            table.join2(iter_opts, opts)
            table.join2(iter_opts, { "injection_rate=" .. iter_inj_rate })
            print("%s %s", bin, table.concat(iter_opts, " "))
            print(path.join(va_dir, iter_log))
            os.execv(bin, iter_opts, {stdout = path.join(va_dir, iter_log)})
        end
    end)