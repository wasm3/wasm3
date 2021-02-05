//
//  m3_compile.h
//
//  Created by Steven Massey on 4/17/19.
//  Copyright © 2019 Steven Massey. All rights reserved.
//

#ifndef m3_compile_h
#define m3_compile_h

#include "m3_code.h"
#include "m3_exec_defs.h"

d_m3BeginExternC

enum
{
    c_waOp_block                = 0x02,
    c_waOp_loop                 = 0x03,
    c_waOp_if                   = 0x04,
    c_waOp_else                 = 0x05,
    c_waOp_end                  = 0x0b,
    c_waOp_branch               = 0x0c,
    c_waOp_branchTable          = 0x0e,
    c_waOp_branchIf             = 0x0d,
    c_waOp_call                 = 0x10,
    c_waOp_getLocal             = 0x20,
    c_waOp_setLocal             = 0x21,
    c_waOp_teeLocal             = 0x22,
};

typedef struct M3FuncType
{
    struct M3FuncType *     next;

    u32                     numRets;
    u32                     numArgs;
    u8                      types[];        // returns, then args
}
M3FuncType;

typedef M3FuncType *        IM3FuncType;

#define d_FuncRetType(ftype,i)  ((ftype)->types[(i)])
#define d_FuncArgType(ftype,i)  ((ftype)->types[(ftype)->numRets + (i)])

//-----------------------------------------------------------------------------------------------------------------------------------

// since the end location of a block is unknown when a branch is compiled, writing
// the actual address must deferred. A linked-list of patch locations is kept in
// M3CompilationScope. When the block compilation exits, it patches these addresses.
typedef struct M3BranchPatch
{
    struct M3BranchPatch *          next;
    pc_t *                          location;
}
M3BranchPatch;

typedef M3BranchPatch *             IM3BranchPatch;


typedef struct M3CompilationScope
{
    struct M3CompilationScope *     outer;

    pc_t                            pc;                 // used by ContinueLoop's
    IM3BranchPatch                  patches;
    i32                             depth;
//    i32                             loopDepth;
    i16                             initStackIndex;
    IM3FuncType                     type;
    m3opcode_t                      opcode;
    bool                            isPolymorphic;
}
M3CompilationScope;

typedef M3CompilationScope *        IM3CompilationScope;

// double the slot count when using 32-bit slots, since every wasm stack element could be a 64-bit type
//static const u16 c_m3MaxFunctionSlots = d_m3MaxFunctionStackHeight * (d_m3Use32BitSlots + 1);

typedef struct
{
    IM3Runtime          runtime;
    IM3Module           module;

    bytes_t             wasm;
    bytes_t             wasmEnd;

    M3CompilationScope  block;

    IM3Function         function;

    IM3CodePage         page;

    IM3BranchPatch      releasedPatches;

    u32                 numEmits;
    u32                 numOpcodes;

    u16                 firstDynamicStackIndex;
    u16                 stackIndex;                 // current stack index

    u16                 firstConstSlotIndex;
    u16                 maxConstSlotIndex;             // as const's are encountered during compilation this tracks their location in the "real" stack

    u16                 firstLocalSlotIndex;
    u16                 firstDynamicSlotIndex;      // numArgs + numLocals + numReservedConstants. the first mutable slot available to the compiler.

    m3slot_t            constants                   [d_m3MaxConstantTableSize];

    // 'wasmStack' holds slot locations
    u16                 wasmStack                   [d_m3MaxFunctionStackHeight];
    u8                  typeStack                   [d_m3MaxFunctionStackHeight];

    // 'm3Slots' contains allocation usage counts
    u8                  m3Slots                     [d_m3MaxFunctionSlots];

    u16                 maxAllocatedSlotPlusOne;

    u16                 regStackIndexPlusOne        [2];

    m3opcode_t          previousOpcode;
}
M3Compilation;

typedef M3Compilation *                 IM3Compilation;

typedef M3Result (* M3Compiler)         (IM3Compilation, m3opcode_t);


//-----------------------------------------------------------------------------------------------------------------------------------


typedef struct M3OpInfo
{
#ifdef DEBUG
    const char * const      name;
#endif

    i8                      stackOffset;
    u8                      type;

    // for most operations:
    // [0]= top operand in register, [1]= top operand in stack, [2]= both operands in stack
    IM3Operation            operations [4];

    M3Compiler              compiler;
}
M3OpInfo;

typedef const M3OpInfo *    IM3OpInfo;

extern const M3OpInfo c_operations [];
extern const M3OpInfo c_operationsFC [];

static inline
const M3OpInfo* GetOpInfo(m3opcode_t opcode) {
    switch (opcode >> 8) {
    case 0x00: return &c_operations[opcode];
    case 0xFC: return &c_operationsFC[opcode & 0xFF];
    default:   return NULL;
    }
}

// TODO: This helper should be removed, when MultiValue is implemented
static inline
u8 GetSingleRetType(IM3FuncType ftype) {
    return (ftype && ftype->numRets) ? ftype->types[0] : c_m3Type_none;
}

#ifdef DEBUG
    #define M3OP(...)       { __VA_ARGS__ }
    #define M3OP_RESERVED   { "reserved" }
#else
    // Strip-off name
    #define M3OP(name, ...) { __VA_ARGS__ }
    #define M3OP_RESERVED   { 0 }
#endif

#if d_m3HasFloat
    #define M3OP_F          M3OP
#elif d_m3NoFloatDynamic
    #define M3OP_F(n,o,t,op,...)        M3OP(n, o, t, { op_Unsupported, op_Unsupported, op_Unsupported, op_Unsupported }, __VA_ARGS__)
#else
    #define M3OP_F(...)     { 0 }
#endif

//-----------------------------------------------------------------------------------------------------------------------------------

u16         GetTypeNumSlots             (u8 i_type);
void        AlignSlotIndexToType        (u16 * io_slotIndex, u8 i_type);

bool        IsRegisterAllocated         (IM3Compilation o, u32 i_register);
bool        IsRegisterLocation          (i16 i_location);
bool        IsFpRegisterLocation        (i16 i_location);
bool        IsIntRegisterLocation       (i16 i_location);

bool        IsStackPolymorphic          (IM3Compilation o);

M3Result    CompileBlock                (IM3Compilation io, IM3FuncType i_blockType, u8 i_blockOpcode);

M3Result    Compile_BlockStatements     (IM3Compilation io);
M3Result    Compile_Function            (IM3Function io_function);

u16         GetMaxUsedSlotPlusOne       (IM3Compilation o);

d_m3EndExternC

#endif // m3_compile_h
