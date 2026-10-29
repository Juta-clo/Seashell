includes("deps/Linc/linc-targets.lua")

set_toolchains("clang")
set_toolset("cc", "clang")
set_toolset("ld", "clang")
set_policy("build.ccache", false)

add_rules("mode.debug", "mode.release")
add_cflags("-std=gnu23", "-Wall", "-Wextra")

if is_mode("debug") then
    set_symbols("debug")
    set_optimize("none")
elseif is_mode("release") then
    set_symbols("none")
    set_optimize("faster")
end

add_rules("plugin.compile_commands.autoupdate", { outputdir = "." })
add_defines("_GNU_SOURCE")
add_requires("unibilium", "lua 5.4", { system = true })

target("launch_seashell")
    set_kind("binary")
    set_default(false)
    set_targetdir("test/manual/")
    add_files("test/manual/launch_seashell.c")

target("seashell")
    set_kind("binary")
    set_targetdir(".")

    add_deps(linc_targets)
    add_packages("unibilium", "lua")
    add_ldflags("-rdynamic")
    add_includedirs("deps/Printty/include")
    add_files("src/*.c", "deps/Printty/src/*.c")

-- TESTING --

local test_bins = {
    "change_tio", "interrupt_me", "self_kill", "write_sleep"
}

for _, name in ipairs(test_bins) do
    target(name)
        set_kind("binary")
        set_default(false)
        set_targetdir("test/pty/test_bins")

        add_files("test/pty/test_bins/" .. name .. ".c")
end

target("pty_test")
    set_kind("binary")
    set_targetdir(".")
    set_default(false)

    add_deps(test_bins, { inherit = false })
    add_deps("seashell", { inherit = false })
    add_deps("launch_seashell", { inherit = false })
    add_deps(linc_targets)
    add_syslinks("ncurses", "vterm", "m")
    add_defines(
        "UNITY_FIXTURE_NO_EXTRAS",
        "UNITY_OUTPUT_COLOR",
        "UNITY_INCLUDE_CONFIG_H",
        "UNITY_REPEAT_TEST_NAME"
    )
    add_includedirs("test/pty", "deps/unity")
    add_files("test/pty/*.c", "deps/unity/*.c")

target("unit_test")
    set_kind("binary")
    set_default(false)
    set_targetdir(".")

    add_deps(linc_targets)
    add_packages("unibilium", "lua")
    add_defines("UNITY_FIXTURE_NO_EXTRAS", "UNITY_OUTPUT_COLOR")
    add_includedirs("src", "test/unit", "deps/unity", "deps/Printty/include")
    add_files(
        "src/*.c|main.c|llog.c",
        "test/unit/*.c",
        "deps/Printty/src/*.c",
        "deps/unity/*.c"
    )
