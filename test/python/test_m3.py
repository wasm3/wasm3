import m3

def test_classes():
    assert isinstance(m3.Environment, type)
    assert isinstance(m3.Runtime, type)
    assert isinstance(m3.Module, type)
    assert isinstance(m3.Function, type)

