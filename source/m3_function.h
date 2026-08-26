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

typedef struct M3FuncType
{
    struct M3FuncType *     next;

    u16                     numRets;
    u16                     numArgs;

    // Position in the environment's list of distinct function types. Equal
    // types share one M3FuncType, so this doubles as the canonical heap type
    // index a (ref $t) carries, and comparing two of them is exactly the
    // structural equivalence the spec asks for.
    u16                     canonicalIndex;

    m3type_t                types [];        // returns, then args
}
M3FuncType;

typedef M3FuncType *        IM3FuncType;


M3Result    AllocFuncType                   (IM3FuncType * o_functionType, u32 i_numTypes);
bool        AreFuncTypesEqual               (const IM3FuncType i_typeA, const IM3FuncType i_typeB);

u16         GetFuncTypeNumParams            (const IM3FuncType i_funcType);
m3type_t    GetFuncTypeParamType            (const IM3FuncType i_funcType, u16 i_index);

u16         GetFuncTypeNumResults           (const IM3FuncType i_funcType);
m3type_t    GetFuncTypeResultType           (const IM3FuncType i_funcType, u16 i_index);

#if d_m3HasTypedRefs
// The type a (ref $t) / (ref null $t) naming this function type is spelled as
m3type_t    RefTypeOfFuncType               (const IM3FuncType i_funcType, bool i_nonNull);
#endif

//---------------------------------------------------------------------------------------------------------------------------------

typedef struct M3Function
{
    struct M3Module *       module;

    M3ImportInfo            import;

    // An import linked to another module's export: the function that actually
    // implements it. Everything that calls through an import resolves to this
    // first, so the call carries the defining module with it - which is what
    // says whose linear memory the body runs against. NULL for a function with
    // a body of its own, and for an import bound to a host function (that one
    // runs against the importing module's memory).
    struct M3Function *     resolved;

    bytes_t                 wasm;
    bytes_t                 wasmEnd;

    cstr_t                  names[d_m3MaxDuplicateFunctionImpl];
    cstr_t                  export_name;                            // should be a part of "names"
    u16                     numNames;                               // maximum of d_m3MaxDuplicateFunctionImpl

    IM3FuncType             funcType;

    pc_t                    compiled;

# if (d_m3EnableCodePageRefCounting)
    IM3CodePage *           codePageRefs;                           // array of all pages used
    u32                     numCodePageRefs;
# endif

# if defined (DEBUG)
    u32                     hits;
    u32                     index;
# endif

    u16                     maxStackSlots;

    u16                     numRetSlots;
    u16                     numRetAndArgSlots;

    u16                     numLocals;                              // not including args
    u32                     numLocalBytes;

    bool                    ownsWasmCode;

    u16                     numConstantBytes;
    void *                  constants;
}
M3Function;


// The function that actually runs when this one is called: an import linked to
// another module resolves to that module's function, everything else to itself.
static inline IM3Function  Function_Implementation  (IM3Function i_function)
{
    return i_function->resolved ? i_function->resolved : i_function;
}

void        Function_Release            (IM3Function i_function);
void        Function_FreeCompiledCode   (IM3Function i_function);

cstr_t      GetFunctionImportModuleName (IM3Function i_function);
cstr_t *    GetFunctionNames            (IM3Function i_function, u16 * o_numNames);
u16         GetFunctionNumArgs          (IM3Function i_function);
m3type_t    GetFunctionArgType          (IM3Function i_function, u32 i_index);

u16         GetFunctionNumReturns       (IM3Function i_function);
u8          GetFunctionReturnType       (const IM3Function i_function, u16 i_index);

u32         GetFunctionNumArgsAndLocals (IM3Function i_function);

cstr_t      SPrintFunctionArgList       (IM3Function i_function, m3stack_t i_sp);

//---------------------------------------------------------------------------------------------------------------------------------


d_m3EndExternC

#endif /* m3_function_h */
