import wasm3 as m3
import pytest

import tempfile, subprocess

def wat2wasm(wat):
    with tempfile.TemporaryDirectory() as d:
        fn_in = d + "/input.wat"
        fn_out = d + "/output.wasm"
        with open(fn_in, "wb") as f:
            f.write(wat.encode('utf8'))
        subprocess.run(["wat2wasm", "--enable-all", "-o", fn_out, fn_in], check=True)
        with open(fn_out, "rb") as f:
            return f.read()

FIB64_WASM = wat2wasm("""
(module
  (func $fib2 (param $n i64) (param $a i64) (param $b i64) (result i64)
    (if (result i64)
        (i64.eqz (get_local $n))
        (then (get_local $a))
        (else (return_call $fib2 (i64.sub (get_local $n)
                                   (i64.const 1))
                          (get_local $b)
                          (i64.add (get_local $a)
                                   (get_local $b))))))

  (func $fib (export "fib") (param i64) (result i64)
    (return_call $fib2 (get_local 0)
                (i64.const 0)   ;; seed value $a
                (i64.const 1))) ;; seed value $b
)
""")

CALLBACK_WASM = wat2wasm("""
(module
  (type (;0;) (func (param i32 i32) (result i32)))
  (func $i (import "env" "callback") (type 0))
  (func (export "run_callback") (type 0)
    local.get 0
    local.get 1
    call $i)
)
""")

DYN_CALLBACK_WASM = wat2wasm("""
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
""")


ADD_WASM = wat2wasm("""
(module
  (func (export "add") (param i64 i64) (result i64)
    local.get 0
    local.get 1
    i64.add
    return)
)
""")


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
    mod = env.parse_module(FIB64_WASM)
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
    assert func.arg_types == (2,)
    assert func.ret_types == (2,)
    assert func(0) == 0
    assert func(1) == 1
    rt.load(env.parse_module(ADD_WASM))
    add = rt.find_function('add')
    assert add(2, 3) == 5


def call_function(wasm, func, *args):
    env = m3.Environment()
    rt = env.new_runtime(4096)
    mod = env.parse_module(wasm)
    rt.load(mod)
    f = rt.find_function(func)
    return f.call_argv(*args)

def test_fib64():
    assert call_function(FIB64_WASM, 'fib', '5') == 5
    assert call_function(FIB64_WASM, 'fib', '10') == 55
    # TODO: Fails on 3.6, 3.7 ?
    #assert call_function(FIB64_WASM, 'fib', '90') == 2880067194370816120

