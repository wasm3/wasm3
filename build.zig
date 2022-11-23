const std = @import("std");

pub fn build(b: *std.build.Builder) !void {
    const target = b.standardTargetOptions(.{});
    const mode = b.standardReleaseOptions();

    const wasm3 = b.addExecutable("wasm3", null);
    wasm3.setTarget(target);
    wasm3.setBuildMode(mode);
    wasm3.install();
    wasm3.linkLibC();

    if (target.getCpuArch() == .wasm32 and target.getOsTag() == .wasi) {
        wasm3.linkSystemLibrary("wasi-emulated-process-clocks");
    }

    wasm3.addIncludePath("source");
    wasm3.addCSourceFiles(&.{
        "source/m3_api_libc.c",
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
        "platforms/app/main.c",
    }, &.{
        "-Dd_m3HasWASI",
        "-fno-sanitize=undefined", // TODO investigate UB sites in the codebase, then delete this line.
    });
}
