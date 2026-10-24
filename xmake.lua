includes("deps/Linc/linc-targets.lua")

set_toolchains("clang")
set_toolset("cc", "clang")
set_toolset("ld", "clang")
set_policy("build.ccache", false)

add_rules("plugin.compile_commands.autoupdate", { outputdir = "." })
add_includedirs("inc")
add_cflags("-g", "-O0", "-std=gnu23", "-Wall", "-Wextra")
add_defines("_GNU_SOURCE")
add_requires("unibilium", "ncurses", "m", "vterm", { system = true })

target("seashell")
    set_kind("binary")
    set_default(false)
    set_targetdir(".")

    add_deps(linc_targets)
    add_packages("unibilium")
    add_ldflags("-rdynamic")
    add_includedirs("deps/Printty/include")
    add_files("src/*.c", "deps/Printty/src/*.c")

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

    add_deps(test_bins, { inherit = false })
    add_deps("seashell", { inherit = false })
    add_deps(linc_targets)
    add_packages("ncurses", "vterm", "m")
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
    add_packages("unibilium")
    add_defines("UNITY_FIXTURE_NO_EXTRAS", "UNITY_OUTPUT_COLOR")
    add_includedirs("test/unit", "deps/unity", "deps/Printty/include")
    add_files(
        "src/*.c|main.c|llog.c",
        "test/unit/*.c",
        "deps/Printty/src/*.c",
        "deps/unity/*.c"
    )
