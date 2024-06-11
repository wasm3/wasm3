const std = @import("std");

pub fn build(b: *std.Build) !void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});

    const libm3_only = b.option(bool, "libm3", "Build libwasm3 only") orelse false;

    const libwasm3 = b.addStaticLibrary(.{
        .name = "m3",
        .target = target,
        .optimize = optimize,
    });
    libwasm3.root_module.sanitize_c = false; // fno-sanitize=undefined
    libwasm3.defineCMacro("d_m3HasTracer", null);

    if (libwasm3.rootModuleTarget().isWasm()) {
        if (libwasm3.rootModuleTarget().os.tag == .wasi) {
            libwasm3.defineCMacro("d_m3HasWASI", null);
            libwasm3.linkSystemLibrary("wasi-emulated-process-clocks");
        }
    }
    libwasm3.addIncludePath(b.path("source"));
    libwasm3.addCSourceFiles(.{
        .files = &.{
            "source/m3_api_libc.c",
            "source/extensions/m3_extensions.c",
            "source/m3_api_meta_wasi.c",
            "source/m3_api_tracer.c",
            "source/m3_api_uvwasi.c",
            "source/m3_api_wasi.c",
            "source/m3_bind.c",
            "source/m3_code.c",
            "source/m3_compile.c",
            "source/m3_core.c",
            "source/m3_env.c",
            "source/m3_exec.c",
            "source/m3_function.c",
            "source/m3_info.c",
            "source/m3_module.c",
            "source/m3_parse.c",
        },
        .flags = if (libwasm3.rootModuleTarget().isWasm())
            &cflags ++ [_][]const u8{
                "-Xclang",
                "-target-feature",
                "-Xclang",
                "+tail-call",
            }
        else
            &cflags,
    });
    libwasm3.linkSystemLibrary("m");
    libwasm3.linkLibC();

    if (!libm3_only) {
        const wasm3 = b.addExecutable(.{
            .name = "wasm3",
            .target = target,
            .optimize = optimize,
        });
        for (libwasm3.root_module.include_dirs.items) |dir| {
            wasm3.addIncludePath(dir.path);
        }
        wasm3.addCSourceFile(.{
            .file = .{ .cwd_relative = "platforms/app/main.c" },
            .flags = &cflags,
        });

        wasm3.linkLibrary(libwasm3);
        b.installArtifact(wasm3);
    } else b.installArtifact(libwasm3);
}

const cflags = [_][]const u8{
    "-Wall",
    "-Wextra",
    "-Wpedantic",
    "-Wparentheses",
    "-Wundef",
    "-Wpointer-arith",
    "-Wstrict-aliasing=2",
    "-std=gnu11",
};
