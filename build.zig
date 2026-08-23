const std = @import("std");

pub fn build(b: *std.Build) !void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});

    const libm3_only = b.option(bool, "libm3", "Build libwasm3 only") orelse false;
    const build_wasi = b.option(enum {simple, metawasi, uvwasi}, "build_wasi", "How to enable wasi");

    const libwasm3 = b.addLibrary(.{
        .linkage = .static,
        .name = "m3",
        .root_module = b.createModule(.{
            .optimize = optimize,
            .target = target,
            .omit_frame_pointer = if (optimize == .Debug) null else true,
            .stack_check = if (optimize == .Debug) null else false,
            .stack_protector = if (optimize == .Debug) null else false,
            .sanitize_c = .off, // fno-sanitize=undefined
        }),
    });
    libwasm3.root_module.addCMacro("d_m3HasTracer", "");
    if (optimize == .Debug)
        libwasm3.root_module.addCMacro("DEBUG", "1");

    if (build_wasi) |wasi| {
        switch (wasi) {
            .simple => libwasm3.root_module.addCMacro("d_m3HasWASI", ""),
            .metawasi => libwasm3.root_module.addCMacro("d_m3HasMetaWASI", ""),
            .uvwasi => @panic("build.zig does not yet support this option."),
        }
    }
    if (libwasm3.rootModuleTarget().cpu.arch.isWasm()) {
        if (libwasm3.rootModuleTarget().os.tag == .wasi) {
            libwasm3.root_module.addCMacro("_WASI_EMULATED_PROCESS_CLOCKS", "");
            libwasm3.root_module.linkSystemLibrary("wasi-emulated-process-clocks", .{});
        }
    }
    libwasm3.root_module.addIncludePath(b.path("source"));
    libwasm3.root_module.addCSourceFiles(.{
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
            "source/m3_validate.c",
            "source/m3_core.c",
            "source/m3_env.c",
            "source/m3_exec.c",
            "source/m3_function.c",
            "source/m3_info.c",
            "source/m3_module.c",
            "source/m3_parse.c",
        },
        .flags = if (libwasm3.rootModuleTarget().cpu.arch.isWasm())
            &cflags ++ [_][]const u8{
                "-Xclang",
                "-target-feature",
                "-Xclang",
                "+tail-call",

                "-Xclang",
                "-target-feature",
                "-Xclang",
                "+bulk-memory",

                "-Xclang",
                "-target-feature",
                "-Xclang",
                "+nontrapping-fptoint",

                "-Xclang",
                "-target-feature",
                "-Xclang",
                "+sign-ext",

                "-Xclang",
                "-target-feature",
                "-Xclang",
                "+multivalue",

                "-Xclang",
                "-target-feature",
                "-Xclang",
                "+mutable-globals",
            }
        else
            &cflags,
    });
    libwasm3.root_module.linkSystemLibrary("m", .{});
    libwasm3.root_module.link_libc = true;

    if (!libm3_only) {
        const wasm3 = b.addExecutable(.{
            .name = "wasm3",
            .root_module = b.createModule(.{
                .target = target,
                .optimize = optimize,
                .omit_frame_pointer = if (optimize == .Debug) null else true,
                .stack_check = if (optimize == .Debug) null else false,
                .stack_protector = if (optimize == .Debug) null else false,
            }),
        });
        wasm3.stack_size = 8388608;
        for (libwasm3.root_module.include_dirs.items) |dir| {
            wasm3.root_module.addIncludePath(dir.path);
        }
        wasm3.root_module.addCSourceFile(.{
            .file = .{ .cwd_relative = "platforms/app/main.c" },
            .flags = &cflags,
        });
        wasm3.root_module.addCMacro("d_m3HasTracer", "");
        if (optimize == .Debug)
            wasm3.root_module.addCMacro("DEBUG", "1");

        if (build_wasi) |wasi| {
            switch (wasi) {
                .simple => wasm3.root_module.addCMacro("d_m3HasWASI", ""),
                .metawasi => wasm3.root_module.addCMacro("d_m3HasMetaWASI", ""),
                .uvwasi => @panic("build.zig does not yet support this option."),
            }
        }
        wasm3.root_module.linkLibrary(libwasm3);
        b.installArtifact(wasm3);
    } else b.installArtifact(libwasm3);
}

const cflags = [_][]const u8{
    "-Wall",
    "-Wextra",
    "-Wparentheses",
    "-Wundef",
    "-Wpointer-arith",
    "-Wstrict-aliasing=2",
    "-Werror=implicit-function-declaration",
    "-Wno-unused-function",
    "-Wno-unused-variable",
    "-Wno-unused-parameter",
    "-Wno-date-time",
    "-Wno-missing-field-initializers",
    "-std=gnu99",
};
