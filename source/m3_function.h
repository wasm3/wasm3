//
//  m3_function.h
//
//  Created by Steven Massey on 4/7/21.
//  Copyright © 2021 Steven Massey. All rights reserved.
//

#ifndef m3_function_h
#define m3_function_h

#include "m3_core.h"

d_m3BeginExternC

//---------------------------------------------------------------------------------------------------------------------------------

typedef struct M3FuncType {
    struct M3FuncType* next;

    u16                numRets;
    u16                numArgs;

    // Position in the environment's list of distinct function types. Equal
    // types share one M3FuncType, so this doubles as the canonical heap type
    // index a (ref $t) carries, and comparing two of them is exactly the
    // structural equivalence the spec asks for.
    u16                canonicalIndex;

    bool               isContinuation;
    struct M3FuncType* contFuncType;

    // A member of a recursive group. The group is its identity, so it never
    // shares an entry with a type that merely matches it field for field -
    // not one outside any group, and not one in a group spelled the same way.
    bool               inRecGroup;

    m3type_t           types[];        // returns, then args
} M3FuncType;

typedef M3FuncType* IM3FuncType;


M3Result            AllocFuncType (IM3FuncType* o_functionType, u32 i_numTypes);
bool                AreFuncTypesEqual (const IM3FuncType i_typeA, const IM3FuncType i_typeB);

u16                 GetFuncTypeNumParams (const IM3FuncType i_funcType);
m3type_t            GetFuncTypeParamType (const IM3FuncType i_funcType, u16 i_index);

u16                 GetFuncTypeNumResults (const IM3FuncType i_funcType);
m3type_t            GetFuncTypeResultType (const IM3FuncType i_funcType, u16 i_index);

#if d_m3HasTypedRefs
// The type a (ref $t) / (ref null $t) naming this function type is spelled as
m3type_t RefTypeOfFuncType (const IM3FuncType i_funcType, bool i_nonNull);
#endif

//---------------------------------------------------------------------------------------------------------------------------------

// The ways a function's frame can be left standing while it waits to go on.
// Two of them can share a pc - a call's return address can be the very next
// operation, which is itself one that suspends - and they do not agree on
// what the frame holds there, so a pc alone does not name one.
//
// A snapshot writes these down, so the numbering is part of the W3S format.
typedef enum M3SafePointKind {
    safepoint_op,           // at a loop back edge, which suspends in place
    safepoint_suspend,      // just past a suspend or a switch
    safepoint_call,         // a call waiting on its callee
    safepoint_resume,       // a resume waiting on the continuation it runs
    safepoint_gas,          // at a gas charge, before the segment it pays for
} M3SafePointKind;

#if d_m3HasSnapshots

// What a snapshot needs to know about a function's compiled code, recorded by
// the compiler when the runtime is suspendable and nowhere else.
//
// A snapshot is written in Wasm's terms, not this build's: a place in a
// function is the offset of a Wasm instruction, and the frame is the Wasm
// locals and operand stack as typed values. The map is what translates. It
// says, at every place a frame can be left standing, which slot or register
// this build keeps each of those values in - so the values can be read out of
// one build's frame and scattered into another's, whatever layout each chose.

// A value the frame holds: where this build keeps it, and its type
typedef struct M3SlotValue {
    u16 slot;            // relative to the frame, or a register alias (d_m3Reg0SlotAlias, d_m3Fp0SlotAlias)
    u8  type;            // storage type: i32, i64, f32, f64, or a reference's base type
} M3SlotValue;

// Values that go with a safepoint
#  define d_m3SafePointTakenBranch    0x1     // resumes a br_if that was taken: _r0 has to hold its condition

// A place a function can be suspended at, or be waiting on something that was:
// a loop back edge, a gas charge, a call, a suspend, a switch or a resume.
//
// Its identity in a snapshot is the instruction's offset and its kind, and -
// since one br_table can branch back to several loops - how many safepoints of
// that kind the same instruction recorded before it. Safepoints are recorded in
// the order the body is read, so they are sorted by that offset.
typedef struct M3SafePoint {
    pc_t pc;
    u32  wasmOffset;     // the instruction's, from the start of the function's body
    u32  firstValue;     // into M3SnapshotMap.values
    u16  numValues;      // the live operand stack, bottom up; the locals are the map's own
    u16  numResults;     // suspend: the top values are the slots it waits in | call, resume: results not yet written
    u16  aux;            // call: where the callee's frame starts, in slots above this one | resume: its handler count
    u8   kind;           // M3SafePointKind
    u8   flags;
} M3SafePoint;

// A loop or a try_table, which the interpreter keeps a native frame for while
// its body runs, and which a snapshot names by the instruction's offset
typedef struct M3BlockStart {
    pc_t pc;             // what the frame records: a loop's body, a try_table's clause table
    u32  wasmOffset;
    u16  numClauses;     // try_table
    u8   opcode;         // c_waOp_loop or c_waOp_tryTable
} M3BlockStart;

typedef struct M3SnapshotMap {
    M3SlotValue*  locals;            // the arguments, then the declared locals
    u32           numLocals;

    M3SafePoint*  safePoints;
    u32           numSafePoints;

    M3SlotValue*  values;
    u32           numValues;

    M3BlockStart* blocks;
    u32           numBlocks;
} M3SnapshotMap;

void SnapshotMap_Free (M3SnapshotMap* i_map);

#endif // d_m3HasSnapshots

typedef struct M3Function {
    struct M3Module*   module;

    M3ImportInfo       import;

    // The linear memory a host function bound to this import addresses. Set to
    // the module's memory 0 when the host function is bound - what a bare i32
    // guest pointer means when nothing says otherwise - and repointed by
    // m3_BindImportMemory for a host module whose ABI names a memory instead.
    // Holds the memory rather than its index: a slot can alias another module's
    // memory, and growing one reallocates behind it. NULL for anything that is
    // not an import bound to a host function.
    struct M3Memory*   hostMemory;

    // An import linked to another module's export: the function that actually
    // implements it. Everything that calls through an import resolves to this
    // first, so the call carries the defining module with it - which is what
    // says whose linear memory the body runs against. NULL for a function with
    // a body of its own, and for an import bound to a host function (that one
    // runs against the importing module's memory).
    struct M3Function* resolved;

    bytes_t            wasm;
    bytes_t            wasmEnd;

    cstr_t             names[d_m3MaxDuplicateFunctionImpl];
    cstr_t             export_name;                            // should be a part of "names"
    u16                numNames;                               // maximum of d_m3MaxDuplicateFunctionImpl

    IM3FuncType        funcType;

    pc_t               compiled;

#if (d_m3EnableCodePageRefCounting)
    IM3CodePage* codePageRefs;                           // array of all pages used
    u32          numCodePageRefs;
#endif

#if defined(DEBUG)
    u32 hits;
    u32 index;
#endif

    u16   maxStackSlots;

    u16   numRetSlots;
    u16   numRetAndArgSlots;

    u16   numLocals;                              // not including args
    u32   numLocalBytes;

    bool  ownsWasmCode;

    u16   numConstantBytes;
    void* constants;

#if d_m3HasSnapshots
    M3SnapshotMap* snapshotMap;                   // NULL unless compiled suspendable
#endif
} M3Function;


// The function that actually runs when this one is called: an import linked to
// another module resolves to that module's function, everything else to itself.
static inline
IM3Function Function_Implementation (IM3Function i_function)
{
    return i_function->resolved ? i_function->resolved : i_function;
}

void     Function_Release (IM3Function i_function);
void     Function_FreeCompiledCode (IM3Function i_function);

cstr_t   GetFunctionImportModuleName (IM3Function i_function);
cstr_t*  GetFunctionNames (IM3Function i_function, u16* o_numNames);
u16      GetFunctionNumArgs (IM3Function i_function);
m3type_t GetFunctionArgType (IM3Function i_function, u32 i_index);

u16      GetFunctionNumReturns (IM3Function i_function);
u8       GetFunctionReturnType (const IM3Function i_function, u16 i_index);

u32      GetFunctionNumArgsAndLocals (IM3Function i_function);

cstr_t   SPrintFunctionArgList (IM3Function i_function, m3stack_t i_sp);
cstr_t   SPrintFunctionRetList (IM3FuncType i_funcType, m3stack_t i_sp);

//---------------------------------------------------------------------------------------------------------------------------------


d_m3EndExternC

#endif /* m3_function_h */
