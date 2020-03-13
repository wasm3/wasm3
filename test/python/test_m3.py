import m3
FIB32_WASM = (b'\x00asm\x01\x00\x00\x00\x01\x06\x01`\x01\x7f\x01\x7f'
              b'\x03\x02\x01\x00\x07\x07\x01\x03fib\x00\x00\n\x1f\x01'
              b'\x1d\x00 \x00A\x02I\x04@ \x00\x0f\x0b \x00A\x02k\x10'
              b'\x00 \x00A\x01k\x10\x00j\x0f\x0b')
def test_classes():
    assert isinstance(m3.Environment, type)
    assert isinstance(m3.Runtime, type)
    assert isinstance(m3.Module, type)
    assert isinstance(m3.Function, type)

def test_environment():
    env = m3.Environment()
    rt = env.new_runtime(1024)
    assert isinstance(rt, m3.Runtime)
    mod = env.parse_module(FIB32_WASM)
    assert isinstance(mod, m3.Module)
    rt.load(mod)
    func = rt.find_function('fib')
    assert isinstance(func, m3.Function)
    assert func.call_argv('5') == 5
    assert func.call_argv('10') == 55
