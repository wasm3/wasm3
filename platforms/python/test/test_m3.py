import m3

FIB32_WASM = bytes.fromhex(
    "00 61 73 6d 01 00 00 00 01 06 01 60 01 7f 01 7f"
    "03 02 01 00 07 07 01 03 66 69 62 00 00 0a 1f 01"
    "1d 00 20 00 41 02 49 04 40 20 00 0f 0b 20 00 41"
    "02 6b 10 00 20 00 41 01 6b 10 00 6a 0f 0b")

FIB64_WASM = bytes.fromhex(
    "00 61 73 6d 01 00 00 00 01 06 01 60 01 7e 01 7e"
    "03 02 01 00 07 07 01 03 66 69 62 00 00 0a 1f 01"
    "1d 00 20 00 42 02 54 04 40 20 00 0f 0b 20 00 42"
    "02 7d 10 00 20 00 42 01 7d 10 00 7c 0f 0b")

# (module
#     (func (param i64 i64) (result i64)
#         local.get 0
#         local.get 1
#         i64.add
#         return
#     )
#     (export "add" (func 0)))
ADD_WASM = bytes.fromhex(
    "00 61 73 6d 01 00 00 00 01 07 01 60 02 7e 7e 01"
    "7e 03 02 01 00 07 07 01 03 61 64 64 00 00 0a 0a"
    "01 08 00 20 00 20 01 7c 0f 0b")

def test_classes():
    assert isinstance(m3.Environment, type)
    assert isinstance(m3.Runtime, type)
    assert isinstance(m3.Module, type)
    assert isinstance(m3.Function, type)

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
    func = rt.find_function('fib')
    assert isinstance(func, m3.Function)
    assert func.call_argv('5') == 5
    assert func.call_argv('10') == 55
    assert func.name == 'fib'
    assert func.num_args == 1
    assert func.return_type == 1
    assert func.arg_types == (1,)
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
