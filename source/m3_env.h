//
//  m3_env.h
//
//  Created by Steven Massey on 4/19/19.
//  Copyright © 2019 Steven Massey. All rights reserved.
//

#ifndef m3_env_h
#define m3_env_h

#include "wasm3.h"
#include "m3_code.h"
#include "m3_compile.h"

d_m3BeginExternC

M3Result    AllocFuncType                   (IM3FuncType * o_functionType, u32 i_numTypes);
bool        AreFuncTypesEqual               (const IM3FuncType i_typeA, const IM3FuncType i_typeB);


//---------------------------------------------------------------------------------------------------------------------------------
typedef struct M3Function
{
    struct M3Module *       module;

    M3ImportInfo            import;

    bytes_t                 wasm;
    bytes_t                 wasmEnd;

    u16                     numNames;                               // maximum of d_m3MaxDuplicateFunctionImpl
    cstr_t                  names[d_m3MaxDuplicateFunctionImpl];

    IM3FuncType             funcType;

    pc_t                    compiled;

#if (d_m3EnableCodePageRefCounting)
    IM3CodePage *           codePageRefs;                           // array of all pages used
    u32                     numCodePageRefs;
#endif

#if defined(DEBUG)
    u32                     hits;
#endif

#if d_m3EnableStrace >= 2
    u16                     index;
#endif
    u16                     maxStackSlots;

    u16                     numArgSlots;

    u16                     numLocals;                              // not including args
    u16                     numLocalBytes;

    void *                  constants;
    u16                     numConstantBytes;

    //bool                    ownsWasmCode;
}
M3Function;

void        Function_Release            (IM3Function i_function);
void        Function_FreeCompiledCode   (IM3Function i_function);

cstr_t      GetFunctionImportModuleName (IM3Function i_function);
cstr_t *    GetFunctionNames            (IM3Function i_function, u16 * o_numNames);
u32         GetFunctionNumArgs          (IM3Function i_function);
u32         GetFunctionNumReturns       (IM3Function i_function);

u32         GetFunctionNumArgsAndLocals (IM3Function i_function);

cstr_t      SPrintFunctionArgList       (IM3Function i_function, m3stack_t i_sp);


//---------------------------------------------------------------------------------------------------------------------------------

typedef struct M3MemoryInfo
{
    u32     initPages;
    u32     maxPages;
}
M3MemoryInfo;


typedef struct M3Memory
{
    M3MemoryHeader *        mallocated;

    u32                     numPages;
    u32                     maxPages;
}
M3Memory;

typedef M3Memory *          IM3Memory;


//---------------------------------------------------------------------------------------------------------------------------------

typedef struct M3DataSegment
{
    const u8 *              initExpr;       // wasm code
    const u8 *              data;

    u32                     initExprSize;
    u32                     memoryRegion;
    u32                     size;
}
M3DataSegment;


void FreeImportInfo (M3ImportInfo * i_info);

//---------------------------------------------------------------------------------------------------------------------------------

typedef struct M3Global
{
    M3ImportInfo            import;

    union
    {
        i64 intValue;
#if d_m3HasFloat
        f64 f64Value;
        f32 f32Value;
#endif
    };

    bytes_t                 initExpr;       // wasm code
    u32                     initExprSize;
    u8                      type;
    bool                    imported;
    bool                    isMutable;
}
M3Global;
typedef M3Global *          IM3Global;


//---------------------------------------------------------------------------------------------------------------------------------
typedef struct M3Module
{
    struct M3Runtime *      runtime;
    struct M3Environment *  environment;

    bytes_t                 wasmStart;
    bytes_t                 wasmEnd;

    cstr_t                  name;

    u32                     numFuncTypes;
    IM3FuncType *           funcTypes;          // array of pointers to list of FuncTypes

    u32                     numImports;
    //IM3Function *           imports;   b         // notice: "I" prefix. imports are pointers to functions in another module.

    u32                     numFunctions;
    M3Function *            functions;

    i32                     startFunction;

    u32                     numDataSegments;
    M3DataSegment *         dataSegments;

    //u32                     importedGlobals;
    u32                     numGlobals;
    M3Global *              globals;

    u32                     numElementSegments;
    bytes_t                 elementSection;
    bytes_t                 elementSectionEnd;

    IM3Function *           table0;
    u32                     table0Size;

    M3MemoryInfo            memoryInfo;
    bool                    memoryImported;

    //bool                    hasWasmCodeCopy;

    struct M3Module *       next;
}
M3Module;

M3Result                    Module_AddGlobal            (IM3Module io_module, IM3Global * o_global, u8 i_type, bool i_mutable, bool i_isImported);

M3Result                    Module_AddFunction          (IM3Module io_module, u32 i_typeIndex, IM3ImportInfo i_importInfo /* can be null */);
IM3Function                 Module_GetFunction          (IM3Module i_module, u32 i_functionIndex);

//---------------------------------------------------------------------------------------------------------------------------------

typedef struct M3Environment
{
//    struct M3Runtime *      runtimes;

    IM3FuncType             funcTypes;          // linked list

    IM3FuncType             retFuncTypes[5];    // the number of elements must match the basic types as per M3ValueType

    M3CodePage *            pagesReleased;
}
M3Environment;

void                        Environment_Release         (IM3Environment i_environment);

// takes ownership of io_funcType and returns a pointer to the persistent version (could be same or different)
void                        Environment_AddFuncType     (IM3Environment i_environment, IM3FuncType * io_funcType);

//---------------------------------------------------------------------------------------------------------------------------------

typedef struct M3Runtime
{
    M3Compilation           compilation;

    IM3Environment          environment;

    M3CodePage *            pagesOpen;      // linked list of code pages with writable space on them
    M3CodePage *            pagesFull;      // linked list of at-capacity pages

    u32                     numCodePages;
    u32                     numActiveCodePages;

    IM3Module               modules;        // linked list of imported modules

    void *                  stack;
    u32                     stackSize;
    u32                     numStackSlots;
    IM3Function             lastCalled;     // last function that successfully executed

    void *                  userdata;

    M3Memory                memory;
    u32                     memoryLimit;

#if d_m3EnableStrace >= 2
    u32                     callDepth;
#endif

    M3ErrorInfo             error;
#if d_m3VerboseLogs
    char                    error_message[256]; // the actual buffer. M3ErrorInfo can point to this
#endif

#if d_m3RecordBacktraces
    M3BacktraceInfo         backtrace;
#endif
}
M3Runtime;

void                        InitRuntime                 (IM3Runtime io_runtime, u32 i_stackSizeInBytes);
void                        Runtime_Release             (IM3Runtime io_runtime);

M3Result                    ResizeMemory                (IM3Runtime io_runtime, u32 i_numPages);

typedef void *              (* ModuleVisitor)           (IM3Module i_module, void * i_info);
void *                      ForEachModule               (IM3Runtime i_runtime, ModuleVisitor i_visitor, void * i_info);

void *                      v_FindFunction              (IM3Module i_module, const char * const i_name);

IM3CodePage                 AcquireCodePage             (IM3Runtime io_runtime);
IM3CodePage                 AcquireCodePageWithCapacity (IM3Runtime io_runtime, u32 i_lineCount);
void                        ReleaseCodePage             (IM3Runtime io_runtime, IM3CodePage i_codePage);

d_m3EndExternC

#endif // m3_env_h
