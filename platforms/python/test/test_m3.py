import wasm3 as m3
import pytest

FIB32_WASM = bytes.fromhex(
    "00 61 73 6d 01 00 00 00  01 06 01 60 01 7f 01 7f"
    "03 02 01 00 07 07 01 03  66 69 62 00 00 0a 1f 01"
    "1d 00 20 00 41 02 49 04  40 20 00 0f 0b 20 00 41"
    "02 6b 10 00 20 00 41 01  6b 10 00 6a 0f 0b")

FIB64_WASM = bytes.fromhex(
    "00 61 73 6d 01 00 00 00  01 06 01 60 01 7e 01 7e"
    "03 02 01 00 07 07 01 03  66 69 62 00 00 0a 1f 01"
    "1d 00 20 00 42 02 54 04  40 20 00 0f 0b 20 00 42"
    "02 7d 10 00 20 00 42 01  7d 10 00 7c 0f 0b")

"""
(type (;0;) (func (param i32 i32) (result i32)))
(func $i (import "env" "callback") (type 0))
(func (export "run_callback") (type 0)
    local.get 0
    local.get 1
    call $i)
"""
CALLBACK_WASM = bytes.fromhex(
    "00 61 73 6d 01 00 00 00  01 07 01 60 02 7f 7f 01"
    "7f 02 10 01 03 65 6e 76  08 63 61 6c 6c 62 61 63"
    "6b 00 00 03 02 01 00 07  10 01 0c 72 75 6e 5f 63"
    "61 6c 6c 62 61 63 6b 00  01 0a 0a 01 08 00 20 00"
    "20 01 10 00 0b")

"""
(module
  (type $t0 (func (param i32 i32) (result i32)))
  (type $t1 (func))
  (type $t2 (func (param i32)))
  (type $t3 (func (param i32 i32 i32) (result i32)))
  (import "env" "pass_fptr" (func $env.pass_fptr (type $t2)))
  (import "env" "__table_base" (global $env.__table_base i32))
  (func $run_test (export "run_test") (type $t1)
    global.get $env.__table_base
    call $env.pass_fptr
    global.get $env.__table_base
    i32.const 1
    i32.add
    call $env.pass_fptr)
  (func $f2 (type $t0) (param $p0 i32) (param $p1 i32) (result i32)
    local.get $p0
    local.get $p1
    i32.add)
  (func $f3 (type $t0) (param $p0 i32) (param $p1 i32) (result i32)
    local.get $p0
    local.get $p1
    i32.mul)
  (func $test (export "call_pass_fptr") (type $t2) (param $p0 i32)
    local.get $p0
    call $env.pass_fptr
  )
  (func $dynCall_iii (export "dynCall_iii") (type $t3) (param $p0 i32) (param $p1 i32) (param $p2 i32) (result i32)
    local.get $p1
    local.get $p2
    local.get $p0
    call_indirect $table (type $t0))
  (table $table (export "table") 2 funcref)
  (elem (global.get $env.__table_base) func $f2 $f3))
"""
DYN_CALLBACK_WASM = bytes.fromhex(
    "00 61 73 6d 01 00 00 00  01 15 04 60 02 7f 7f 01"
    "7f 60 00 00 60 01 7f 00  60 03 7f 7f 7f 01 7f 02"
    "25 02 03 65 6e 76 09 70  61 73 73 5f 66 70 74 72"
    "00 02 03 65 6e 76 0c 5f  5f 74 61 62 6c 65 5f 62"
    "61 73 65 03 7f 00 03 06  05 01 00 00 02 03 04 04"
    "01 70 00 02 07 33 04 08  72 75 6e 5f 74 65 73 74"
    "00 01 0e 63 61 6c 6c 5f  70 61 73 73 5f 66 70 74"
    "72 00 04 0b 64 79 6e 43  61 6c 6c 5f 69 69 69 00"
    "05 05 74 61 62 6c 65 01  00 09 08 01 00 23 00 0b"
    "02 02 03 0a 32 05 0d 00  23 00 10 00 23 00 41 01"
    "6a 10 00 0b 07 00 20 00  20 01 6a 0b 07 00 20 00"
    "20 01 6c 0b 06 00 20 00  10 00 0b 0b 00 20 01 20"
    "02 20 00 11 00 00 0b")

"""
(func (export "add") (param i64 i64) (result i64)
    local.get 0
    local.get 1
    i64.add
    return
)
"""
ADD_WASM = bytes.fromhex(
    "00 61 73 6d 01 00 00 00 01 07 01 60 02 7e 7e 01"
    "7e 03 02 01 00 07 07 01 03 61 64 64 00 00 0a 0a"
    "01 08 00 20 00 20 01 7c 0f 0b")

def test_classes():
    assert isinstance(m3.Environment, type)
    assert isinstance(m3.Runtime, type)
    assert isinstance(m3.Module, type)
    assert isinstance(m3.Function, type)

def test_callback():
    env = m3.Environment()
    rt = env.new_runtime(1024)
    mod = env.parse_module(CALLBACK_WASM)
    rt.load(mod)
    mem = rt.get_memory(0)

    def func(x, y):
        assert x == 123
        assert y == 456
        return x*y
    mod.link_function("env", "callback", "i(ii)", func)
    run_callback = rt.find_function("run_callback")
    assert run_callback(123, 456) == 123*456

def test_callback_member():
    class WasmRunner:
        def __init__(self, wasm):
            self.env = m3.Environment()
            self.rt = self.env.new_runtime(1024)
            self.mod = self.env.parse_module(wasm)
            self.rt.load(self.mod)
            self.mem = self.rt.get_memory(0)
            self.mod.link_function("env", "callback", "i(ii)", self.func)
            self.run_callback = self.rt.find_function("run_callback")

        def func(self, x, y):
            assert x == 987
            assert y == 654
            return x+y

    inst = WasmRunner(CALLBACK_WASM)
    assert inst.run_callback(987, 654) == 987+654

def test_dynamic_callback():
    env = m3.Environment()
    rt = env.new_runtime(1024)
    mod = env.parse_module(DYN_CALLBACK_WASM)
    rt.load(mod)
    dynCall_iii = rt.find_function("dynCall_iii")

    def pass_fptr(fptr):
        if fptr == 0:
            assert dynCall_iii(fptr, 12, 34) == 46
        elif fptr == 1:
            # TODO: call by table index directly here
            assert dynCall_iii(fptr, 12, 34) == 408
        else:
            raise Exception("Strange function ptr")

    mod.link_function("env", "pass_fptr", "v(i)", pass_fptr)

    # Indirect calls
    assert dynCall_iii(0, 12, 34) == 46
    assert dynCall_iii(1, 12, 34) == 408

    # Recursive exported function call (single calls)
    call_pass_fptr = rt.find_function("call_pass_fptr")
    base = 0
    call_pass_fptr(base+0)
    call_pass_fptr(base+1)

    # Recursive exported function call (multiple calls)
    rt.find_function("run_test")()

def test_m3(capfd):
    env = m3.Environment()
    rt = env.new_runtime(1024)
    assert isinstance(rt, m3.Runtime)
    mod = env.parse_module(FIB32_WASM)
    assert isinstance(mod, m3.Module)
    assert mod.name == '.unnamed'
    rt.load(mod)
    assert rt.get_memory(0) is None  # XXX
#     rt.print_info()
#     assert capfd.readouterr().out == """
# -- m3 runtime -------------------------------------------------
#  stack-size: 1024
#
#  module [0]  name: '.unnamed'; funcs: 1
# ----------------------------------------------------------------
# """
    with pytest.raises(RuntimeError):
        rt.find_function('not_existing')

    func = rt.find_function('fib')
    assert isinstance(func, m3.Function)
    assert func.call_argv('5') == 5
    assert func.call_argv('10') == 55
    assert func.name == 'fib'
    assert func.num_args == 1
    assert func.num_rets == 1
    assert func.arg_types == (1,)
    assert func.ret_types == (1,)
    assert func(0) == 0
    assert func(1) == 1
    rt.load(env.parse_module(ADD_WASM))
    add = rt.find_function('add')
    assert add(2, 3) == 5


def call_function(wasm, func, *args):
    env = m3.Environment()
    rt = env.new_runtime(1024)
    mod = env.parse_module(wasm)
    rt.load(mod)
    f = rt.find_function(func)
    return f.call_argv(*args)

def test_fib64():
    assert call_function(FIB64_WASM, 'fib', '5') == 5
    assert call_function(FIB64_WASM, 'fib', '10') == 55
