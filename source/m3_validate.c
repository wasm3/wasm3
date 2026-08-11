//
//  m3_validate.c
//
//  Pre-pass WebAssembly bytecode validator.
//  Implements the spec's type-checking algorithm with operand/control stacks.
//

#include "m3_validate.h"
#include "m3_exception.h"
#include "m3_info.h"

#if d_m3EnableValidation

// Sentinel type for polymorphic (unknown) operands
#define c_valUnknown        0xFF

// ---------- Control frame ----------

typedef struct {
    m3opcode_t  opcode;
    u16         height;         // operand stack height at block entry
    u16         param_count;
    u16         result_count;
    IM3FuncType type;           // block type (for params/results)
    bool        is_unreachable;
} ValCtrlFrame;

// ---------- Validator context ----------

typedef struct {
    bytes_t     wasm;
    bytes_t     wasmEnd;
    IM3Module   module;
    IM3Function function;

    u8          opd [d_m3ValStack];
    u16         opdTop;

    ValCtrlFrame ctrl [d_m3ValCtrlDepth];
    u16          ctrlTop;

    u8          localTypes [d_m3ValStack];
    u16         numLocals;
} ValCtx;

// A memory op is only valid if the module defines or imports one
static bool v_has_memory (ValCtx * v)
{
    return v->module and (v->module->memoryImported or v->module->memoryDeclared);
}

// Spec: the alignment immediate of a memory access must not be larger than the
// natural alignment of the operation. Natural alignment: 8-bit=0, 16-bit=1,
// 32-bit=2, 64-bit=3.
static u32 v_max_align (m3opcode_t opcode)
{
    switch (opcode) {
        case 0x2c: case 0x2d:   // i32.load8_s, i32.load8_u
        case 0x30: case 0x31:   // i64.load8_s, i64.load8_u
        case 0x3a:              // i32.store8
        case 0x3c:              // i64.store8
            return 0;
        case 0x2e: case 0x2f:   // i32.load16_s, i32.load16_u
        case 0x32: case 0x33:   // i64.load16_s, i64.load16_u
        case 0x3b:              // i32.store16
        case 0x3d:              // i64.store16
            return 1;
        case 0x29:              // i64.load
        case 0x2b:              // f64.load
        case 0x37:              // i64.store
        case 0x39:              // f64.store
            return 3;
        default:                // 32-bit accesses, and a safe fallback
            return 2;
    }
}

// ---------- Operand stack ----------

static M3Result v_push (ValCtx * v, u8 type)
{
    if (v->opdTop >= d_m3ValStack)
        return m3Err_functionStackOverflow;
    v->opd[v->opdTop++] = type;
    return m3Err_none;
}

static M3Result v_pop (ValCtx * v, u8 * o_type)
{
    ValCtrlFrame * f = &v->ctrl[v->ctrlTop - 1];
    if (v->opdTop == f->height) {
        if (f->is_unreachable) { *o_type = c_valUnknown; return m3Err_none; }
        return m3Err_functionStackUnderrun;
    }
    *o_type = v->opd[--v->opdTop];
    return m3Err_none;
}

static M3Result v_pop_expect (ValCtx * v, u8 expect, u8 * o_actual)
{
    u8 actual;
    M3Result r = v_pop(v, &actual);
    if (r) return r;
    if (expect != c_valUnknown && actual != c_valUnknown && actual != expect)
        return m3Err_typeMismatch;
    *o_actual = (actual == c_valUnknown) ? expect : actual;
    return m3Err_none;
}

// ---------- Control stack ----------

static M3Result v_push_ctrl (ValCtx * v, m3opcode_t op, IM3FuncType type)
{
    if (v->ctrlTop >= d_m3ValCtrlDepth)
        return m3Err_functionStackOverflow;
    ValCtrlFrame * f = &v->ctrl[v->ctrlTop++];
    f->opcode       = op;
    f->type         = type;
    f->param_count  = type ? type->numArgs : 0;
    f->result_count = type ? type->numRets : 0;
    f->height       = v->opdTop;
    f->is_unreachable = false;
    return m3Err_none;
}

static M3Result v_pop_ctrl (ValCtx * v, ValCtrlFrame * o_frame)
{
    if (v->ctrlTop == 0)
        return m3Err_wasmMalformed;
    ValCtrlFrame * f = &v->ctrl[v->ctrlTop - 1];
    // pop result types
    if (f->type) {
        for (u16 i = f->result_count; i > 0; i--) {
            u8 a;
            M3Result r = v_pop_expect(v, f->type->types[i - 1], &a);
            if (r) return r;
        }
    }
    if (v->opdTop != f->height)
        return m3Err_typeCountMismatch;
    if (o_frame) *o_frame = *f;
    v->ctrlTop--;
    return m3Err_none;
}

static void v_unreachable (ValCtx * v)
{
    ValCtrlFrame * f = &v->ctrl[v->ctrlTop - 1];
    v->opdTop = f->height;
    f->is_unreachable = true;
}

// Label types: loop -> params, block/if/else/func -> results
static u16 v_label_n (ValCtrlFrame * f)
{
    return (f->opcode == 0x03) ? f->param_count : f->result_count;
}

static u8 v_label_t (ValCtrlFrame * f, u16 i)
{
    if (!f->type) return c_m3Type_none;
    if (f->opcode == 0x03)
        return f->type->types[f->type->numRets + i]; // params
    return f->type->types[i]; // results
}

// Pop label types for branch target
static M3Result v_pop_labels (ValCtx * v, ValCtrlFrame * tgt)
{
    u16 n = v_label_n(tgt);
    for (u16 i = n; i > 0; i--) {
        u8 a;
        M3Result r = v_pop_expect(v, v_label_t(tgt, i - 1), &a);
        if (r) return r;
    }
    return m3Err_none;
}

// Push label types back
static M3Result v_push_labels (ValCtx * v, ValCtrlFrame * tgt)
{
    u16 n = v_label_n(tgt);
    for (u16 i = 0; i < n; i++) {
        M3Result r = v_push(v, v_label_t(tgt, i));
        if (r) return r;
    }
    return m3Err_none;
}

// ---------- Block type resolution ----------

static M3Result v_read_blocktype (ValCtx * v, IM3FuncType * o_type)
{
    if (v->wasm >= v->wasmEnd)
        return m3Err_wasmUnderrun;

    i64 type;
    M3Result r = ReadLebSigned(&type, 33, &v->wasm, v->wasmEnd);
    if (r) return r;

    if (type < 0) {
        u8 valtype;
        r = NormalizeType(&valtype, (i8)type);
        if (r) return r;
        IM3Environment env = v->module->environment;
        *o_type = env->retFuncTypes[valtype];
    } else {
        if ((u32)type >= v->module->numFuncTypes) return m3Err_wasmMalformed;
        *o_type = v->module->funcTypes[(u32)type];
    }
    return m3Err_none;
}

// ---------- Convenience ----------

static M3Result v_unop (ValCtx * v, u8 in, u8 out)
{
    u8 a; M3Result r = v_pop_expect(v, in, &a);
    if (r) return r;
    return v_push(v, out);
}

static M3Result v_binop (ValCtx * v, u8 t)
{
    u8 a; M3Result r;
    r = v_pop_expect(v, t, &a); if (r) return r;
    r = v_pop_expect(v, t, &a); if (r) return r;
    return v_push(v, t);
}

static M3Result v_relop (ValCtx * v, u8 t)
{
    u8 a; M3Result r;
    r = v_pop_expect(v, t, &a); if (r) return r;
    r = v_pop_expect(v, t, &a); if (r) return r;
    return v_push(v, c_m3Type_i32);
}

static M3Result v_testop (ValCtx * v, u8 t)
{
    return v_unop(v, t, c_m3Type_i32);
}

static M3Result v_cvtop (ValCtx * v, u8 in, u8 out)
{
    return v_unop(v, in, out);
}


// ---------- Main validation loop ----------

static M3Result v_validate_body (ValCtx * v)
{
    M3Result r = m3Err_none;
    u8 a;

    while (v->wasm < v->wasmEnd)
    {
        m3opcode_t opcode;
        r = Read_opcode(&opcode, &v->wasm, v->wasmEnd);
        if (r) return r;

        switch (opcode)
        {
        // ---- Control ----
        case 0x00: // unreachable
            v_unreachable(v);
            break;

        case 0x01: // nop
            break;

        case 0x02: // block
        case 0x03: // loop
        case 0x04: // if
        {
            IM3FuncType bt;
            r = v_read_blocktype(v, &bt);
            if (r) return r;
            if (opcode == 0x04) {
                r = v_pop_expect(v, c_m3Type_i32, &a);
                if (r) return r;
            }
            // Pop block params from caller stack
            if (bt) {
                for (u16 i = bt->numArgs; i > 0; i--) {
                    r = v_pop_expect(v, bt->types[bt->numRets + i - 1], &a);
                    if (r) return r;
                }
            }
            r = v_push_ctrl(v, opcode, bt);
            if (r) return r;
            // Push params inside block
            if (bt) {
                for (u16 i = 0; i < bt->numArgs; i++) {
                    r = v_push(v, bt->types[bt->numRets + i]);
                    if (r) return r;
                }
            }
            break;
        }

        case 0x05: // else
        {
            ValCtrlFrame frame;
            r = v_pop_ctrl(v, &frame);
            if (r) return r;
            if (frame.opcode != 0x04)
                return m3Err_wasmMalformed;
            r = v_push_ctrl(v, 0x05, frame.type);
            if (r) return r;
            if (frame.type) {
                for (u16 i = 0; i < frame.type->numArgs; i++) {
                    r = v_push(v, frame.type->types[frame.type->numRets + i]);
                    if (r) return r;
                }
            }
            break;
        }

        case 0x0b: // end
        {
            ValCtrlFrame frame;
            r = v_pop_ctrl(v, &frame);
            if (r) return r;
            // Push results
            if (frame.type) {
                for (u16 i = 0; i < frame.result_count; i++) {
                    r = v_push(v, frame.type->types[i]);
                    if (r) return r;
                }
            }
            // If this was the outermost frame, we're done
            if (v->ctrlTop == 0)
                return m3Err_none;
            break;
        }

        case 0x0c: // br
        {
            u32 depth;
            r = ReadLEB_u32(&depth, &v->wasm, v->wasmEnd);
            if (r) return r;
            if (depth >= v->ctrlTop) return m3Err_wasmMalformed;
            ValCtrlFrame * tgt = &v->ctrl[v->ctrlTop - 1 - depth];
            r = v_pop_labels(v, tgt);
            if (r) return r;
            v_unreachable(v);
            break;
        }

        case 0x0d: // br_if
        {
            u32 depth;
            r = ReadLEB_u32(&depth, &v->wasm, v->wasmEnd);
            if (r) return r;
            if (depth >= v->ctrlTop) return m3Err_wasmMalformed;
            r = v_pop_expect(v, c_m3Type_i32, &a);
            if (r) return r;
            ValCtrlFrame * tgt = &v->ctrl[v->ctrlTop - 1 - depth];
            r = v_pop_labels(v, tgt);
            if (r) return r;
            r = v_push_labels(v, tgt);
            if (r) return r;
            break;
        }

        case 0x0e: // br_table
        {
            u32 count;
            r = ReadLEB_u32(&count, &v->wasm, v->wasmEnd);
            if (r) return r;
            u32 defDepth = 0;
            u16 arity = 0;
            // First pass: read all depths and validate arity + types match default
            bytes_t savedPos = v->wasm;
            // Read all targets to find the default (last one)
            for (u32 i = 0; i <= count; i++) {
                u32 d;
                r = ReadLEB_u32(&d, &v->wasm, v->wasmEnd);
                if (r) return r;
                if (d >= v->ctrlTop) return m3Err_wasmMalformed;
                if (i == count) defDepth = d;
            }
            // Now validate all labels match the default's types
            ValCtrlFrame * defTgt = &v->ctrl[v->ctrlTop - 1 - defDepth];
            arity = v_label_n(defTgt);
            v->wasm = savedPos;
            for (u32 i = 0; i <= count; i++) {
                u32 d;
                r = ReadLEB_u32(&d, &v->wasm, v->wasmEnd);
                if (r) return r;
                ValCtrlFrame * t = &v->ctrl[v->ctrlTop - 1 - d];
                u16 n = v_label_n(t);
                if (n != arity) return m3Err_typeCountMismatch;
                // Spec: label types must be identical, not just same arity
                for (u16 j = 0; j < n; j++) {
                    if (v_label_t(t, j) != v_label_t(defTgt, j))
                        return m3Err_typeMismatch;
                }
            }
            r = v_pop_expect(v, c_m3Type_i32, &a);
            if (r) return r;
            ValCtrlFrame * dt = &v->ctrl[v->ctrlTop - 1 - defDepth];
            r = v_pop_labels(v, dt);
            if (r) return r;
            v_unreachable(v);
            break;
        }

        case 0x0f: // return
        {
            IM3FuncType ft = v->function->funcType;
            if (ft) {
                for (u16 i = ft->numRets; i > 0; i--) {
                    r = v_pop_expect(v, ft->types[i - 1], &a);
                    if (r) return r;
                }
            }
            v_unreachable(v);
            break;
        }

        // ---- Call ----
        case 0x10: // call
        {
            u32 idx;
            r = ReadLEB_u32(&idx, &v->wasm, v->wasmEnd);
            if (r) return r;
            if (idx >= v->module->numFunctions) return m3Err_wasmMalformed;
            IM3FuncType ft = v->module->functions[idx].funcType;
            if (ft) {
                for (u16 i = ft->numArgs; i > 0; i--) {
                    r = v_pop_expect(v, ft->types[ft->numRets + i - 1], &a);
                    if (r) return r;
                }
                for (u16 i = 0; i < ft->numRets; i++) {
                    r = v_push(v, ft->types[i]);
                    if (r) return r;
                }
            }
            break;
        }

        case 0x11: // call_indirect
        {
            u32 typeIdx;
            r = ReadLEB_u32(&typeIdx, &v->wasm, v->wasmEnd);
            if (r) return r;
            u32 tableIdx;
            r = ReadLEB_u32(&tableIdx, &v->wasm, v->wasmEnd);
            if (r) return r;
            if (typeIdx >= v->module->numFuncTypes) return m3Err_wasmMalformed;
            // Spec: table must exist (MVP requires table index 0 and table must be defined)
            if (tableIdx != 0) return m3Err_wasmMalformed;
            if (!v->module->hasTable) return m3Err_wasmMalformed;
            IM3FuncType ft = v->module->funcTypes[typeIdx];
            r = v_pop_expect(v, c_m3Type_i32, &a); // table index operand
            if (r) return r;
            if (ft) {
                for (u16 i = ft->numArgs; i > 0; i--) {
                    r = v_pop_expect(v, ft->types[ft->numRets + i - 1], &a);
                    if (r) return r;
                }
                for (u16 i = 0; i < ft->numRets; i++) {
                    r = v_push(v, ft->types[i]);
                    if (r) return r;
                }
            }
            break;
        }

        // ---- Parametric ----
        case 0x1a: // drop
            r = v_pop(v, &a);
            if (r) return r;
            break;

        case 0x1b: // select
        {
            r = v_pop_expect(v, c_m3Type_i32, &a);
            if (r) return r;
            u8 t2;
            r = v_pop(v, &t2);
            if (r) return r;
            u8 t1;
            r = v_pop_expect(v, t2, &t1);
            if (r) return r;
            r = v_push(v, (t2 == c_valUnknown) ? t1 : t2);
            if (r) return r;
            break;
        }

        // ---- Variable ----
        case 0x20: // local.get
        {
            u32 idx;
            r = ReadLEB_u32(&idx, &v->wasm, v->wasmEnd);
            if (r) return r;
            if (idx >= v->numLocals) return m3Err_wasmMalformed;
            r = v_push(v, v->localTypes[idx]);
            if (r) return r;
            break;
        }

        case 0x21: // local.set
        {
            u32 idx;
            r = ReadLEB_u32(&idx, &v->wasm, v->wasmEnd);
            if (r) return r;
            if (idx >= v->numLocals) return m3Err_wasmMalformed;
            r = v_pop_expect(v, v->localTypes[idx], &a);
            if (r) return r;
            break;
        }

        case 0x22: // local.tee
        {
            u32 idx;
            r = ReadLEB_u32(&idx, &v->wasm, v->wasmEnd);
            if (r) return r;
            if (idx >= v->numLocals) return m3Err_wasmMalformed;
            r = v_pop_expect(v, v->localTypes[idx], &a);
            if (r) return r;
            r = v_push(v, v->localTypes[idx]);
            if (r) return r;
            break;
        }

        case 0x23: // global.get
        {
            u32 idx;
            r = ReadLEB_u32(&idx, &v->wasm, v->wasmEnd);
            if (r) return r;
            if (idx >= v->module->numGlobals) return m3Err_wasmMalformed;
            r = v_push(v, v->module->globals[idx].type);
            if (r) return r;
            break;
        }

        case 0x24: // global.set
        {
            u32 idx;
            r = ReadLEB_u32(&idx, &v->wasm, v->wasmEnd);
            if (r) return r;
            if (idx >= v->module->numGlobals) return m3Err_wasmMalformed;
            r = v_pop_expect(v, v->module->globals[idx].type, &a);
            if (r) return r;
            break;
        }

        // ---- Memory load ----
        case 0x28: case 0x29: case 0x2a: case 0x2b: // i32/i64/f32/f64.load
        case 0x2c: case 0x2d: case 0x2e: case 0x2f: // i32.load8/16 s/u
        case 0x30: case 0x31: case 0x32: case 0x33: // i64.load8/16 s/u
        case 0x34: case 0x35:                         // i64.load32 s/u
        {
            u32 align, offset;
            r = ReadLEB_u32(&align, &v->wasm, v->wasmEnd); if (r) return r;
            r = ReadLEB_u32(&offset, &v->wasm, v->wasmEnd); if (r) return r;
            if (align > v_max_align(opcode)) return m3Err_wasmMalformed;
            if (not v_has_memory(v)) return m3Err_wasmMalformed;
            r = v_pop_expect(v, c_m3Type_i32, &a); if (r) return r;
            u8 result;
            if      (opcode == 0x28) result = c_m3Type_i32;
            else if (opcode == 0x29) result = c_m3Type_i64;
            else if (opcode == 0x2a) result = c_m3Type_f32;
            else if (opcode == 0x2b) result = c_m3Type_f64;
            else if (opcode <= 0x2f) result = c_m3Type_i32;
            else                     result = c_m3Type_i64;
            r = v_push(v, result);
            if (r) return r;
            break;
        }

        // ---- Memory store ----
        case 0x36: case 0x37: case 0x38: case 0x39: // i32/i64/f32/f64.store
        case 0x3a: case 0x3b:                         // i32.store8/16
        case 0x3c: case 0x3d: case 0x3e:             // i64.store8/16/32
        {
            u32 align, offset;
            r = ReadLEB_u32(&align, &v->wasm, v->wasmEnd); if (r) return r;
            r = ReadLEB_u32(&offset, &v->wasm, v->wasmEnd); if (r) return r;
            if (align > v_max_align(opcode)) return m3Err_wasmMalformed;
            if (not v_has_memory(v)) return m3Err_wasmMalformed;
            u8 valtype;
            if      (opcode == 0x36) valtype = c_m3Type_i32;
            else if (opcode == 0x37) valtype = c_m3Type_i64;
            else if (opcode == 0x38) valtype = c_m3Type_f32;
            else if (opcode == 0x39) valtype = c_m3Type_f64;
            else if (opcode <= 0x3b) valtype = c_m3Type_i32;
            else                     valtype = c_m3Type_i64;
            r = v_pop_expect(v, valtype, &a); if (r) return r;
            r = v_pop_expect(v, c_m3Type_i32, &a); if (r) return r;
            break;
        }

        // ---- Memory size/grow ----
        case 0x3f: // memory.size
        {
            u32 memidx;
            r = ReadLEB_u32(&memidx, &v->wasm, v->wasmEnd); if (r) return r;
            if (memidx != 0 or not v_has_memory(v)) return m3Err_wasmMalformed;
            r = v_push(v, c_m3Type_i32); if (r) return r;
            break;
        }
        case 0x40: // memory.grow
        {
            u32 memidx;
            r = ReadLEB_u32(&memidx, &v->wasm, v->wasmEnd); if (r) return r;
            if (memidx != 0 or not v_has_memory(v)) return m3Err_wasmMalformed;
            r = v_pop_expect(v, c_m3Type_i32, &a); if (r) return r;
            r = v_push(v, c_m3Type_i32); if (r) return r;
            break;
        }

        // ---- Constants ----
        case 0x41: { // i32.const
            i32 val;
            r = ReadLEB_i32(&val, &v->wasm, v->wasmEnd); if (r) return r;
            r = v_push(v, c_m3Type_i32); if (r) return r;
            break;
        }
        case 0x42: { // i64.const
            i64 val;
            r = ReadLEB_i64(&val, &v->wasm, v->wasmEnd); if (r) return r;
            r = v_push(v, c_m3Type_i64); if (r) return r;
            break;
        }
        case 0x43: { // f32.const
            if (v->wasm + 4 > v->wasmEnd) return m3Err_wasmUnderrun;
            v->wasm += 4;
            r = v_push(v, c_m3Type_f32); if (r) return r;
            break;
        }
        case 0x44: { // f64.const
            if (v->wasm + 8 > v->wasmEnd) return m3Err_wasmUnderrun;
            v->wasm += 8;
            r = v_push(v, c_m3Type_f64); if (r) return r;
            break;
        }


        // ---- i32 comparison ----
        case 0x45: r = v_testop(v, c_m3Type_i32); break; // i32.eqz
        case 0x46: case 0x47: case 0x48: case 0x49: case 0x4a:
        case 0x4b: case 0x4c: case 0x4d: case 0x4e: case 0x4f:
            r = v_relop(v, c_m3Type_i32); break;

        // ---- i64 comparison ----
        case 0x50: r = v_testop(v, c_m3Type_i64); break; // i64.eqz
        case 0x51: case 0x52: case 0x53: case 0x54: case 0x55:
        case 0x56: case 0x57: case 0x58: case 0x59: case 0x5a:
            r = v_relop(v, c_m3Type_i64); break;

        // ---- f32 comparison ----
        case 0x5b: case 0x5c: case 0x5d: case 0x5e: case 0x5f: case 0x60:
            r = v_relop(v, c_m3Type_f32); break;

        // ---- f64 comparison ----
        case 0x61: case 0x62: case 0x63: case 0x64: case 0x65: case 0x66:
            r = v_relop(v, c_m3Type_f64); break;

        // ---- i32 unary ----
        case 0x67: case 0x68: case 0x69: // clz, ctz, popcnt
            r = v_unop(v, c_m3Type_i32, c_m3Type_i32); break;

        // ---- i32 binary ----
        case 0x6a: case 0x6b: case 0x6c: case 0x6d: case 0x6e: case 0x6f:
        case 0x70: case 0x71: case 0x72: case 0x73: case 0x74: case 0x75:
        case 0x76: case 0x77: case 0x78: // add..rotr
            r = v_binop(v, c_m3Type_i32); break;

        // ---- i64 unary ----
        case 0x79: case 0x7a: case 0x7b: // clz, ctz, popcnt
            r = v_unop(v, c_m3Type_i64, c_m3Type_i64); break;

        // ---- i64 binary ----
        case 0x7c: case 0x7d: case 0x7e: case 0x7f: case 0x80: case 0x81:
        case 0x82: case 0x83: case 0x84: case 0x85: case 0x86: case 0x87:
        case 0x88: case 0x89: case 0x8a: // add..rotr
            r = v_binop(v, c_m3Type_i64); break;

        // ---- f32 unary ----
        case 0x8b: case 0x8c: case 0x8d: case 0x8e: case 0x8f:
        case 0x90: case 0x91: // abs, neg, ceil, floor, trunc, nearest, sqrt
            r = v_unop(v, c_m3Type_f32, c_m3Type_f32); break;

        // ---- f32 binary ----
        case 0x92: case 0x93: case 0x94: case 0x95: case 0x96:
        case 0x97: case 0x98: // add, sub, mul, div, min, max, copysign
            r = v_binop(v, c_m3Type_f32); break;

        // ---- f64 unary ----
        case 0x99: case 0x9a: case 0x9b: case 0x9c: case 0x9d:
        case 0x9e: case 0x9f: // abs, neg, ceil, floor, trunc, nearest, sqrt
            r = v_unop(v, c_m3Type_f64, c_m3Type_f64); break;

        // ---- f64 binary ----
        case 0xa0: case 0xa1: case 0xa2: case 0xa3: case 0xa4:
        case 0xa5: case 0xa6: // add, sub, mul, div, min, max, copysign
            r = v_binop(v, c_m3Type_f64); break;

        // ---- Conversions ----
        case 0xa7: r = v_cvtop(v, c_m3Type_i64, c_m3Type_i32); break; // i32.wrap/i64
        case 0xa8: case 0xa9:   // i32.trunc_s/f32, i32.trunc_u/f32
            r = v_cvtop(v, c_m3Type_f32, c_m3Type_i32); break;
        case 0xaa: case 0xab:   // i32.trunc_s/f64, i32.trunc_u/f64
            r = v_cvtop(v, c_m3Type_f64, c_m3Type_i32); break;
        case 0xac: case 0xad:   // i64.extend_s/i32, i64.extend_u/i32
            r = v_cvtop(v, c_m3Type_i32, c_m3Type_i64); break;
        case 0xae: case 0xaf:   // i64.trunc_s/f32, i64.trunc_u/f32
            r = v_cvtop(v, c_m3Type_f32, c_m3Type_i64); break;
        case 0xb0: case 0xb1:   // i64.trunc_s/f64, i64.trunc_u/f64
            r = v_cvtop(v, c_m3Type_f64, c_m3Type_i64); break;
        case 0xb2: case 0xb3:   // f32.convert_s/i32, f32.convert_u/i32
            r = v_cvtop(v, c_m3Type_i32, c_m3Type_f32); break;
        case 0xb4: case 0xb5:   // f32.convert_s/i64, f32.convert_u/i64
            r = v_cvtop(v, c_m3Type_i64, c_m3Type_f32); break;
        case 0xb6:              // f32.demote/f64
            r = v_cvtop(v, c_m3Type_f64, c_m3Type_f32); break;
        case 0xb7: case 0xb8:   // f64.convert_s/i32, f64.convert_u/i32
            r = v_cvtop(v, c_m3Type_i32, c_m3Type_f64); break;
        case 0xb9: case 0xba:   // f64.convert_s/i64, f64.convert_u/i64
            r = v_cvtop(v, c_m3Type_i64, c_m3Type_f64); break;
        case 0xbb:              // f64.promote/f32
            r = v_cvtop(v, c_m3Type_f32, c_m3Type_f64); break;
        case 0xbc:              // i32.reinterpret/f32
            r = v_cvtop(v, c_m3Type_f32, c_m3Type_i32); break;
        case 0xbd:              // i64.reinterpret/f64
            r = v_cvtop(v, c_m3Type_f64, c_m3Type_i64); break;
        case 0xbe:              // f32.reinterpret/i32
            r = v_cvtop(v, c_m3Type_i32, c_m3Type_f32); break;
        case 0xbf:              // f64.reinterpret/i64
            r = v_cvtop(v, c_m3Type_i64, c_m3Type_f64); break;

        // ---- Sign-extension (MVP post) ----
        case 0xc0: case 0xc1:   // i32.extend8_s, i32.extend16_s
            r = v_unop(v, c_m3Type_i32, c_m3Type_i32); break;
        case 0xc2: case 0xc3: case 0xc4: // i64.extend8/16/32_s
            r = v_unop(v, c_m3Type_i64, c_m3Type_i64); break;

        // ---- 0xFC prefix (saturating truncations + bulk memory) ----
        case 0xfc:
        {
            u32 sub;
            r = ReadLEB_u32(&sub, &v->wasm, v->wasmEnd);
            if (r) return r;
            switch (sub) {
            case 0x00: case 0x01: // i32.trunc_sat_f32_s/u
                r = v_cvtop(v, c_m3Type_f32, c_m3Type_i32); break;
            case 0x02: case 0x03: // i32.trunc_sat_f64_s/u
                r = v_cvtop(v, c_m3Type_f64, c_m3Type_i32); break;
            case 0x04: case 0x05: // i64.trunc_sat_f32_s/u
                r = v_cvtop(v, c_m3Type_f32, c_m3Type_i64); break;
            case 0x06: case 0x07: // i64.trunc_sat_f64_s/u
                r = v_cvtop(v, c_m3Type_f64, c_m3Type_i64); break;
            case 0x0a: // memory.copy
            {
                u32 dst, src;
                r = ReadLEB_u32(&dst, &v->wasm, v->wasmEnd); if (r) return r;
                r = ReadLEB_u32(&src, &v->wasm, v->wasmEnd); if (r) return r;
                r = v_pop_expect(v, c_m3Type_i32, &a); if (r) return r; // n
                r = v_pop_expect(v, c_m3Type_i32, &a); if (r) return r; // src
                r = v_pop_expect(v, c_m3Type_i32, &a); if (r) return r; // dst
                break;
            }
            case 0x0b: // memory.fill
            {
                u32 memidx;
                r = ReadLEB_u32(&memidx, &v->wasm, v->wasmEnd); if (r) return r;
                r = v_pop_expect(v, c_m3Type_i32, &a); if (r) return r; // n
                r = v_pop_expect(v, c_m3Type_i32, &a); if (r) return r; // val
                r = v_pop_expect(v, c_m3Type_i32, &a); if (r) return r; // dst
                break;
            }
            default:
                // Unknown FC sub-opcode: skip validation (allow forward compat)
                break;
            }
            break;
        }

        default:
            // Unknown opcode - skip rather than fail for forward compat
            // (the compiler will reject truly unsupported ops later)
            break;

        } // switch

        if (r) return r;

    } // while

    // If we ran out of bytes without hitting the final end
    return m3Err_wasmMalformed;
}

// ---------- Public entry point ----------

M3Result  ValidateFunction  (IM3Function i_function)
{
    if (!i_function->wasm) return m3Err_none;

    IM3FuncType funcType = i_function->funcType;
    IM3Module   module   = i_function->module;

    // Set up context on stack
    ValCtx v;
    memset(&v, 0, sizeof(v));
    v.module   = module;
    v.function = i_function;
    v.wasm     = i_function->wasm;
    v.wasmEnd  = i_function->wasmEnd;

    // Skip code size LEB
    u32 size;
    M3Result r = ReadLEB_u32(&size, &v.wasm, v.wasmEnd);
    if (r) return r;

    // Parse locals
    u32 numLocalBlocks;
    r = ReadLEB_u32(&numLocalBlocks, &v.wasm, v.wasmEnd);
    if (r) return r;

    // First: params. Running out of room has to be an error, not a truncation:
    // a short localTypes would make later local.get indices read as unknown
    u16 numParams = funcType ? funcType->numArgs : 0;
    if (numParams > d_m3ValStack) return m3Err_functionStackOverflow;
    for (u16 i = 0; i < numParams; i++) {
        v.localTypes[v.numLocals++] = funcType->types[funcType->numRets + i];
    }

    // Then: declared locals
    for (u32 b = 0; b < numLocalBlocks; b++) {
        u32 count;
        r = ReadLEB_u32(&count, &v.wasm, v.wasmEnd);
        if (r) return r;
        i8 waType;
        r = ReadLEB_i7(&waType, &v.wasm, v.wasmEnd);
        if (r) return r;
        u8 normalized;
        r = NormalizeType(&normalized, waType);
        if (r) return r;
        if (count > (u32) (d_m3ValStack - v.numLocals)) return m3Err_functionStackOverflow;
        for (u32 c = 0; c < count; c++) {
            v.localTypes[v.numLocals++] = normalized;
        }
    }

    // Push the function-level control frame
    r = v_push_ctrl(&v, 0x00, funcType); // opcode 0x00 marks function frame
    if (r) return r;

    // Push params onto operand stack (they're part of the function body's initial stack)
    // Actually per the spec, locals are indexed but not on the operand stack.
    // The function frame's params are NOT pushed to the operand stack.
    // Only block params would be pushed (and for the function frame there are no block params
    // since the function body's "block type" has results = function returns, params = 0).
    // The function frame's label_types = results (since it's not a loop).

    // Validate the body
    r = v_validate_body(&v);
    if (r) return r;

    // After validation, control stack should be empty
    if (v.ctrlTop != 0)
        return m3Err_wasmMalformed;

    return m3Err_none;
}

#else // !d_m3EnableValidation

M3Result  ValidateFunction  (IM3Function i_function)
{
    (void)i_function;
    return m3Err_none;
}

#endif // d_m3EnableValidation
