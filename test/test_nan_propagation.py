#!/usr/bin/env python3
"""Test f32/f64 min/max NaN propagation in wasm3 (github #405).

The spec leaves the sign and payload of a NaN produced by fmin/fmax
non-deterministic, but wasm3 used to always return a positive canonical NaN
while every other engine (and wasm3's own add/mul/div, which ride on the
hardware) propagates the incoming NaN. That divergence is what #405 reports:
f32.max(-1.458e38, -nan) yielded 0x7fc00000 instead of 0xffc00000.

Usage: test_nan_propagation.py [path-to-wasm3]
"""

import os
import struct
import subprocess
import sys
import tempfile

WASM3 = (
    sys.argv[1]
    if len(sys.argv) > 1
    else os.path.join(
        os.path.dirname(os.path.abspath(__file__)), "..", "build", "wasm3"
    )
)

tests_passed = 0
tests_failed = 0


def leb128_u(val):
    """Encode unsigned LEB128."""
    result = bytearray()
    while True:
        byte = val & 0x7F
        val >>= 7
        if val != 0:
            byte |= 0x80
        result.append(byte)
        if val == 0:
            break
    return bytes(result)


def make_module(sections):
    """Create a wasm module from sections list [(id, bytes), ...]."""
    data = b"\x00asm\x01\x00\x00\x00"
    for sec_id, sec_data in sections:
        data += bytes([sec_id]) + leb128_u(len(sec_data)) + sec_data
    return data


# opcodes
F32_MIN, F32_MAX = 0x96, 0x97
F64_MIN, F64_MAX = 0xA4, 0xA5


def make_binop_module(cases):
    """One exported nullary function per case, each folding two constants.

    cases = [(name, is_f64, opcode, a_bits, b_bits, expected_bits), ...]
    """
    types = b"\x02" + bytes([0x60, 0, 1, 0x7D]) + bytes([0x60, 0, 1, 0x7C])
    funcs = leb128_u(len(cases)) + b"".join(leb128_u(c[1]) for c in cases)
    exports = leb128_u(len(cases)) + b"".join(
        leb128_u(len(c[0])) + c[0].encode() + b"\x00" + leb128_u(i)
        for i, c in enumerate(cases)
    )
    bodies = []
    for name, is_f64, op, a, b, expected in cases:
        const, pack = (0x44, "<Q") if is_f64 else (0x43, "<I")
        code = (
            b"\x00"
            + bytes([const])
            + struct.pack(pack, a)
            + bytes([const])
            + struct.pack(pack, b)
            + bytes([op])
            + b"\x0b"
        )
        bodies.append(leb128_u(len(code)) + code)
    code_sec = leb128_u(len(bodies)) + b"".join(bodies)
    return make_module([(1, types), (3, funcs), (7, exports), (10, code_sec)])


def run_cases(cases):
    """Invoke every case through the repl, which prints raw float bits."""
    global tests_passed, tests_failed

    wasm = make_binop_module(cases)
    with tempfile.NamedTemporaryFile(suffix=".wasm", delete=False) as f:
        f.write(wasm)
        f.flush()
        try:
            r = subprocess.run(
                [WASM3, "--repl", f.name],
                input="".join(":invoke " + c[0] + "\n" for c in cases),
                capture_output=True,
                text=True,
                timeout=10,
            )
        finally:
            os.unlink(f.name)

    results = [
        line.split("Result: ")[1].split(":")[0]
        for line in r.stderr.splitlines()
        if "Result: " in line
    ]

    if len(results) != len(cases):
        print(f"FAIL: expected {len(cases)} results, got {len(results)}")
        print(r.stderr)
        tests_failed += len(cases)
        return

    for (name, is_f64, op, a, b, expected), got in zip(cases, results):
        got = int(got)
        width = 16 if is_f64 else 8
        if got == expected:
            tests_passed += 1
            print(f"PASS: {name}")
        else:
            tests_failed += 1
            print(
                f"FAIL: {name} -- got 0x{got:0{width}x}, expected 0x{expected:0{width}x}"
            )


# fmt: off

NUM32   = 0xFEDB7F8C          # ordinary negative number, the operand from #405
PNAN32  = 0x7FC00000          # +nan, canonical
NNAN32  = 0xFFC00000          # -nan, canonical
SNAN32  = 0x7F800001          # signaling NaN, payload 1
ARITH32 = 0xFFC00042          # negative arithmetic NaN, non-canonical payload

NUM64   = 0xC02E000000000000
PNAN64  = 0x7FF8000000000000
NNAN64  = 0xFFF8000000000000
SNAN64  = 0x7FF0000000000001

print("=== Testing f32 min/max NaN propagation ===")
run_cases([
    # the case from github #405, and its mirror images
    ("max_num_nnan",  0, F32_MAX, NUM32,  NNAN32, NNAN32),
    ("max_nnan_num",  0, F32_MAX, NNAN32, NUM32,  NNAN32),
    ("min_num_nnan",  0, F32_MIN, NUM32,  NNAN32, NNAN32),
    ("min_nnan_num",  0, F32_MIN, NNAN32, NUM32,  NNAN32),
    ("max_num_pnan",  0, F32_MAX, NUM32,  PNAN32, PNAN32),
    # with two NaN operands the first one wins
    ("max_nnan_pnan", 0, F32_MAX, NNAN32, PNAN32, NNAN32),
    # a signaling NaN must come out quieted, keeping its sign and payload
    ("max_snan",      0, F32_MAX, NUM32,  SNAN32, 0x7FC00001),
    ("min_snan",      0, F32_MIN, SNAN32, NUM32,  0x7FC00001),
    # an arithmetic NaN passes through untouched
    ("max_arith",     0, F32_MAX, NUM32,  ARITH32, ARITH32),
])

print("\n=== Testing f64 min/max NaN propagation ===")
run_cases([
    ("d_max_num_nnan", 1, F64_MAX, NUM64,  NNAN64, NNAN64),
    ("d_max_nnan_num", 1, F64_MAX, NNAN64, NUM64,  NNAN64),
    ("d_min_num_nnan", 1, F64_MIN, NUM64,  NNAN64, NNAN64),
    ("d_min_nnan_num", 1, F64_MIN, NNAN64, NUM64,  NNAN64),
    ("d_max_num_pnan", 1, F64_MAX, NUM64,  PNAN64, PNAN64),
    ("d_max_snan",     1, F64_MAX, NUM64,  SNAN64, 0x7FF8000000000001),
])

print("\n=== Testing min/max on non-NaN operands ===")
run_cases([
    ("max_1_2",     0, F32_MAX, 0x3F800000, 0x40000000, 0x40000000),
    ("min_1_2",     0, F32_MIN, 0x3F800000, 0x40000000, 0x3F800000),
    # zeros are signed: max(-0,+0) = +0, min(-0,+0) = -0
    ("max_nz_pz",   0, F32_MAX, 0x80000000, 0x00000000, 0x00000000),
    ("min_nz_pz",   0, F32_MIN, 0x80000000, 0x00000000, 0x80000000),
    ("max_pz_nz",   0, F32_MAX, 0x00000000, 0x80000000, 0x00000000),
    ("min_pz_nz",   0, F32_MIN, 0x00000000, 0x80000000, 0x80000000),
    ("d_max_1_2",   1, F64_MAX, 0x3FF0000000000000, 0x4000000000000000, 0x4000000000000000),
    ("d_min_1_2",   1, F64_MIN, 0x3FF0000000000000, 0x4000000000000000, 0x3FF0000000000000),
    ("d_max_nz_pz", 1, F64_MAX, 0x8000000000000000, 0x0000000000000000, 0x0000000000000000),
    ("d_min_nz_pz", 1, F64_MIN, 0x8000000000000000, 0x0000000000000000, 0x8000000000000000),
])

# fmt: on

print(f"\n=== Results: {tests_passed} passed, {tests_failed} failed ===")
sys.exit(0 if tests_failed == 0 else 1)
