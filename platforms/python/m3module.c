#include "Python.h"

#include <m3_api_defs.h>
#include "wasm3.h"
/* FIXME: remove when there is a public API to get function return value */
#include "m3_env.h"

typedef struct {
    PyObject_HEAD
    IM3Environment e;
} m3_environment;

typedef struct {
    PyObject_HEAD
    m3_environment *env;
    IM3Runtime r;
} m3_runtime;

typedef struct {
    PyObject_HEAD
    m3_environment *env;
    IM3Module m;
} m3_module;

typedef struct {
    PyObject_HEAD
    IM3Function f;
    IM3Runtime r;
} m3_function;

static PyObject *M3_Environment_Type;
static PyObject *M3_Runtime_Type;
static PyObject *M3_Module_Type;
static PyObject *M3_Function_Type;

static m3_environment*
newEnvironment(PyObject *arg)
{
    m3_environment *self = PyObject_GC_New(m3_environment, (PyTypeObject*)M3_Environment_Type);
    if (self == NULL)
        return NULL;
    self->e = m3_NewEnvironment();
    return self;    
}

static void
delEnvironment(m3_environment *self)
{
    m3_FreeEnvironment(self->e);
}

static PyObject *
M3_Environment_new_runtime(m3_environment *env, PyObject *stack_size_bytes)
{
    size_t n = PyLong_AsSize_t(stack_size_bytes);
    m3_runtime *self = PyObject_GC_New(m3_runtime, (PyTypeObject*)M3_Runtime_Type);
    if (self == NULL)
        return NULL;
    Py_INCREF(env);
    self->env = env;
    self->r = m3_NewRuntime(env->e, n, NULL);
    return self;
}

static PyObject *
M3_Environment_parse_module(m3_environment *env, PyObject *bytes)
{
    Py_ssize_t size;
    char *data;
    PyBytes_AsStringAndSize(bytes, &data, &size);
    m3_module *self = PyObject_GC_New(m3_module, (PyTypeObject*)M3_Module_Type);
    if (self == NULL)
        return NULL;
    Py_INCREF(env);
    self->env = env;
    IM3Module m;
    M3Result err = m3_ParseModule(env->e, &m, data, size);
    self->m = m;
    return self;
}

static PyMethodDef M3_Environment_methods[] = {
    {"new_runtime",            (PyCFunction)M3_Environment_new_runtime,  METH_O,
        PyDoc_STR("new_runtime(stack_size_bytes) -> Runtime")},
    {"parse_module",            (PyCFunction)M3_Environment_parse_module,  METH_O,
        PyDoc_STR("new_runtime(bytes) -> Module")},
    {NULL,              NULL}           /* sentinel */
};

static PyType_Slot M3_Environment_Type_slots[] = {
    {Py_tp_doc, "The m3.Environment type"},
    {Py_tp_finalize, delEnvironment},
    {Py_tp_new, newEnvironment},
    {Py_tp_methods, M3_Environment_methods},
    {0, 0}
};

static PyObject *
M3_Runtime_load(m3_runtime *runtime, PyObject *arg)
{
    m3_module *module = (m3_module *)arg;
    M3Result err = m3_LoadModule(runtime->r, module->m);
    Py_RETURN_NONE;
}

static PyObject *
M3_Runtime_find_function(m3_runtime *runtime, PyObject *name)
{
    m3_function *self = PyObject_GC_New(m3_function, (PyTypeObject*)M3_Function_Type);
    M3Result err = m3_FindFunction(&self->f, runtime->r, PyUnicode_AsUTF8(name));
    self->r = runtime->r;
    return self;
}

static PyObject *
M3_Runtime_get_memory(m3_runtime *runtime, PyObject *index)
{
    Py_buffer view = {};
    uint32_t size;
    const uint8_t *mem = m3_GetMemory(runtime->r, &size, PyLong_AsLong(index));
    if (!mem)
        Py_RETURN_NONE;
    view.buf = (void *)mem;
    view.obj = runtime; Py_INCREF(view.obj);
    view.len = size;
    view.shape = &view.len;
    view.ndim = view.itemsize = 1;
    view.readonly = 1;

    return PyMemoryView_FromBuffer(&view);
}

static PyObject *
M3_Runtime_print_info(m3_runtime *runtime)
{
    m3_PrintRuntimeInfo(runtime->r);
    Py_RETURN_NONE;
}

static PyMethodDef M3_Runtime_methods[] = {
    {"load",            (PyCFunction)M3_Runtime_load,  METH_O,
        PyDoc_STR("load(module) -> None")},
    {"find_function", (PyCFunction)M3_Runtime_find_function,  METH_O,
        PyDoc_STR("find_function(name) -> Function")},
    {"get_memory",     (PyCFunction)M3_Runtime_get_memory,  METH_O,
        PyDoc_STR("get_memory(index) -> memoryview")},
    {"print_info",     (PyCFunction)M3_Runtime_print_info,  METH_NOARGS,
        PyDoc_STR("print_info()")},
    {NULL,              NULL}           /* sentinel */
};

static PyType_Slot M3_Runtime_Type_slots[] = {
    {Py_tp_doc, "The m3.Runtime type"},
    // {Py_tp_finalize, delRuntime},
    // {Py_tp_new, newRuntime},
    {Py_tp_methods, M3_Runtime_methods},
    {0, 0}
};

static PyObject*
Module_name(m3_module *self, void * closure)
{
    return PyUnicode_FromString(self->m->name);
}

static PyGetSetDef M3_Module_properties[] = {
    {"name", (getter) Module_name, NULL, "module name", NULL},
    {0},
};

static PyType_Slot M3_Module_Type_slots[] = {
    {Py_tp_doc, "The m3.Module type"},
    // {Py_tp_finalize, delModule},
    // {Py_tp_new, newModule},
    // {Py_tp_methods, M3_Module_methods},
    {Py_tp_getset, M3_Module_properties},
    {0, 0}
};

static void
put_arg_on_stack(u64 *s, u8 type, PyObject *arg)
{
    switch (type) {
        case c_m3Type_i32:  *(i32*)(s) = PyLong_AsLong(arg);  break;
        case c_m3Type_i64:  *(i64*)(s) = PyLong_AsLong(arg); break;
        case c_m3Type_f32:  *(f32*)(s) = PyFloat_AsDouble(arg);  break;
        case c_m3Type_f64:  *(f64*)(s) = PyFloat_AsDouble(arg);  break;
    }
}

static PyObject *
get_result_from_stack(u8 type, m3stack_t stack)
{
    switch (type) {
        case c_m3Type_none: Py_RETURN_NONE;
        case c_m3Type_i32: return PyLong_FromLong(*(i32*)(stack));
        case c_m3Type_i64: return PyLong_FromLong(*(i64*)(stack));
        case c_m3Type_f32: return PyFloat_FromDouble(*(f32*)(stack));
        case c_m3Type_f64: return PyFloat_FromDouble(*(f64*)(stack));
        default:
            PyErr_Format(PyExc_TypeError, "unknown return type %d", (int)type);
            return NULL;
    }   
}

#define MAX_ARGS 8
static PyObject *
M3_Function_call_argv(m3_function *func, PyObject *args)
{
    Py_ssize_t size = PyList_GET_SIZE(args), i;
    const char* argv[8];
    for(i = 0; i< size;++i) {
        argv[i] = PyUnicode_AsUTF8(PyTuple_GET_ITEM(args, i));
    }
    M3Result res = m3_CallWithArgs(func->f, size, argv);
    return get_result_from_stack(func->f->funcType->returnType, func->r->stack);
}

static PyObject*
Function_name(m3_function *self, void * closure)
{
    return PyUnicode_FromString(self->f->name);
}

static PyObject*
Function_num_args(m3_function *self, void * closure)
{
    return PyLong_FromLong(self->f->funcType->numArgs);
}

static PyObject*
Function_return_type(m3_function *self, void * closure)
{
    return PyLong_FromLong(self->f->funcType->returnType);
}

static PyObject*
Function_arg_types(m3_function *self, void * closure)
{
    M3FuncType *type = self->f->funcType;
    Py_ssize_t n = type->numArgs;
    PyObject *ret = PyTuple_New(n);
    if (ret) {
        Py_ssize_t i;
        for (i = 0; i < n; ++i) {
            PyTuple_SET_ITEM(ret, i, PyLong_FromLong(type->argTypes[i]));
        }
    }
    return ret;
}

static PyGetSetDef M3_Function_properties[] = {
    {"name", (getter) Function_name, NULL, "function name", NULL },
    {"num_args", (getter) Function_num_args, NULL, "number of args", NULL },
    {"return_type", (getter) Function_return_type, NULL, "return type", NULL },
    {"arg_types", (getter) Function_arg_types, NULL, "types of args", NULL },
    {NULL}  /* Sentinel */
};

static PyMethodDef M3_Function_methods[] = {
    {"call_argv", (PyCFunction)M3_Function_call_argv,  METH_VARARGS,
        PyDoc_STR("call_argv(args...) -> result")},
    {NULL, NULL}           /* sentinel */
};

static PyObject*
M3_Function_call(m3_function *self, PyObject *args, PyObject *kwargs)
{
    u32 i;
    IM3Function f = self->f;
    M3FuncType *type = f->funcType;
    // args are always 64-bit aligned
    u64 * stack = (u64 *) self->r->stack;
    for (i = 0; i < type->numArgs; ++i) {
        put_arg_on_stack(&stack[i], type->argTypes[i], PyTuple_GET_ITEM(args, i));
    }
    Call(f->compiled, (m3stack_t) stack, self->r->memory.mallocated, d_m3OpDefaultArgs);
    return get_result_from_stack(f->funcType->returnType, self->r->stack);
}

static PyType_Slot M3_Function_Type_slots[] = {
    {Py_tp_doc, "The m3.Function type"},
    // {Py_tp_finalize, delFunction},
    // {Py_tp_new, newFunction},
    {Py_tp_call, M3_Function_call},
    {Py_tp_methods, M3_Function_methods},
    {Py_tp_getset, M3_Function_properties},
    {0, 0}
};

static PyType_Spec M3_Environment_Type_spec = {
    "m3.Environment",
    sizeof(m3_environment),
    0,
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,
    M3_Environment_Type_slots
};

static PyType_Spec M3_Runtime_Type_spec = {
    "m3.Runtime",
    sizeof(m3_runtime),
    0,
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,
    M3_Runtime_Type_slots
};

static PyType_Spec M3_Module_Type_spec = {
    "m3.Module",
    sizeof(m3_module),
    0,
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,
    M3_Module_Type_slots
};

static PyType_Spec M3_Function_Type_spec = {
    "m3.Function",
    sizeof(m3_function),
    0,
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,
    M3_Function_Type_slots
};

static int
m3_modexec(PyObject *m)
{
    M3_Environment_Type = PyType_FromSpec(&M3_Environment_Type_spec);
    if (M3_Environment_Type == NULL)
        goto fail;
    M3_Runtime_Type = PyType_FromSpec(&M3_Runtime_Type_spec);
    if (M3_Runtime_Type == NULL)
        goto fail;
    M3_Module_Type = PyType_FromSpec(&M3_Module_Type_spec);
    if (M3_Module_Type == NULL)
        goto fail;
    M3_Function_Type = PyType_FromSpec(&M3_Function_Type_spec);
    if (M3_Function_Type == NULL)
        goto fail;
    PyModule_AddStringMacro(m, M3_VERSION);
    PyModule_AddObject(m, "Environment", M3_Environment_Type);
    PyModule_AddObject(m, "Runtime", M3_Runtime_Type);
    PyModule_AddObject(m, "Module", M3_Module_Type);
    PyModule_AddObject(m, "Function", M3_Function_Type);
    return 0;
 fail:
    Py_XDECREF(m);
    return -1;
}

static PyModuleDef_Slot m3_slots[] = {
    {Py_mod_exec, m3_modexec},
    {0, NULL}
};

PyDoc_STRVAR(m3_doc,
"m3 - wasm3 bindings");

static struct PyModuleDef m3module = {
    PyModuleDef_HEAD_INIT,
    "m3",
    m3_doc,
    0,
    0, // methods
    m3_slots,
    NULL,
    NULL,
    NULL
};

PyMODINIT_FUNC
PyInit_m3(void)
{
    return PyModuleDef_Init(&m3module);
}

