//
//  m3_env.h
//
//  Created by Steven Massey on 4/19/19.
//  Copyright © 2019 Steven Massey. All rights reserved.
//

#ifndef m3_env_h
#define m3_env_h

#include "m3_core.h"
#include "wasm3.h"
#include "m3_code.h"
#include "m3_compile.h"

d_m3BeginExternC


//---------------------------------------------------------------------------------------------------------------------------------

// dynamic array guy macros
// might extract these into a separate header
#define da_realloc(TYPE, ARR) do {                                                          \
    (ARR)->entries = m3_ReallocArray(TYPE, (ARR)->entries, (ARR)->cap * 2 + 1, (ARR)->cap); \
    (ARR)->cap = (ARR)->cap * 2 + 1;                                                        \
} while(0)

#define da_push(TYPE, ARR, VAL) do {        \
    if ((ARR)->count >= (ARR)->cap) {       \
        da_realloc(TYPE, ARR);              \
    }                                       \
    (ARR)->entries[(ARR)->count++] = (VAL); \
} while(0)

#define da_free(ARR) m3_Free((ARR)->entries)

// Parser output
typedef struct M3MemoryInfo
{
    u32                     initPages;
    u32                     maxPages;
    u32                     pageSize;
}
M3MemoryInfo;

// Instance
typedef struct M3Memory
{
    M3MemoryHeader *        mallocated;

    u32                     numPages;
    u32                     maxPages;
    u32                     pageSize;
}
M3Memory, * IM3Memory;

typedef struct M3Memories
{
    M3Memory *              entries;

    u32                     count;
    u32                     cap;
}
M3Memories;

// map of module memidx to runtime memories index
typedef struct M3MemoryTableEntry {
    u32                     internalIndex;
    M3MemoryInfo            memoryInfo;

    bool                    imported;
    M3ImportInfo            import;

    bool                    exported;
    cstr_t                  exportName;
} M3MemoryTableEntry;

typedef struct M3MemoryTable
{
    M3MemoryTableEntry *    entries;
    u32                     count;
    u32                     cap;
}
M3MemoryTable;

//---------------------------------------------------------------------------------------------------------------------------------

typedef struct M3DataSegment
{
    const u8 *              initExpr;           // wasm code
    const u8 *              data;

    u32                     initExprSize;
    u32                     memoryRegion;
    u32                     memIdx;
    u32                     size;
}
M3DataSegment;

//---------------------------------------------------------------------------------------------------------------------------------

typedef struct M3Global
{
    M3ImportInfo            import;

    union
    {
        i32 i32Value;
        i64 i64Value;
#if d_m3HasFloat
        f64 f64Value;
        f32 f32Value;
#endif
    };

    cstr_t                  name;
    bytes_t                 initExpr;       // wasm code
    u32                     initExprSize;
    u8                      type;
    bool                    imported;
    bool                    isMutable;
}
M3Global;


//---------------------------------------------------------------------------------------------------------------------------------
typedef struct M3Module
{
    struct M3Runtime *      runtime;
    struct M3Environment *  environment;

    bytes_t                 wasmStart;
    bytes_t                 wasmEnd;

    cstr_t                  name;

    u32                     numFuncTypes;
    IM3FuncType *           funcTypes;              // array of pointers to list of FuncTypes

    u32                     numFuncImports;
    u32                     numFunctions;
    u32                     allFunctions;           // allocated functions count
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
    const char*             table0ExportName;

    M3MemoryTable           memoryTable;

    //bool                    hasWasmCodeCopy;

    struct M3Module *       next;
}
M3Module;

M3Result                    Module_AddGlobal            (IM3Module io_module, IM3Global * o_global, u8 i_type, bool i_mutable, bool i_isImported);

M3Result                    Module_PreallocFunctions    (IM3Module io_module, u32 i_totalFunctions);
M3Result                    Module_AddFunction          (IM3Module io_module, u32 i_typeIndex, IM3ImportInfo i_importInfo /* can be null */);
IM3Function                 Module_GetFunction          (IM3Module i_module, u32 i_functionIndex);
IM3Memory                   Module_GetMemory            (IM3Module i_module, u32 i_memoryIndex);

void                        Module_GenerateNames        (IM3Module i_module);

void                        FreeImportInfo              (M3ImportInfo * i_info);

//---------------------------------------------------------------------------------------------------------------------------------

typedef struct M3Environment
{
//    struct M3Runtime *      runtimes;

    IM3FuncType             funcTypes;                          // linked list of unique M3FuncType structs that can be compared using pointer-equivalence

    IM3FuncType             retFuncTypes [c_m3Type_unknown];    // these 'point' to elements in the linked list above.
                                                                // the number of elements must match the basic types as per M3ValueType
    M3CodePage *            pagesReleased;

    M3SectionHandler        customSectionHandler;
}
M3Environment;

void                        Environment_Release         (IM3Environment i_environment);

// takes ownership of io_funcType and returns a pointer to the persistent version (could be same or different)
void                        Environment_AddFuncType     (IM3Environment i_environment, IM3FuncType * io_funcType);

//---------------------------------------------------------------------------------------------------------------------------------

// What other runtimes usually call a "Store"
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
    void *                  originStack;
    u32                     stackSize;
    u32                     numStackSlots;
    IM3Function             lastCalled;     // last function that successfully executed

    void *                  userdata;

    M3Memories              memories;
    u32                     memoryLimit;

#if d_m3EnableStrace >= 2
    u32                     callDepth;
#endif

    M3ErrorInfo             error;
#if d_m3VerboseErrorMessages
    char                    error_message[256]; // the actual buffer. M3ErrorInfo can point to this
#endif

#if d_m3RecordBacktraces
    M3BacktraceInfo         backtrace;
#endif

	u32						newCodePageSequence;
}
M3Runtime;

void                        InitRuntime                 (IM3Runtime io_runtime, u32 i_stackSizeInBytes);
void                        Runtime_Release             (IM3Runtime io_runtime);

M3Result                    ResizeMemory                (IM3Runtime io_runtime, u32 i_numPages, u32 i_internalIndex);

typedef void *              (* ModuleVisitor)           (IM3Module i_module, void * i_info);
void *                      ForEachModule               (IM3Runtime i_runtime, ModuleVisitor i_visitor, void * i_info);

void *                      v_FindFunction              (IM3Module i_module, const char * const i_name);

IM3CodePage                 AcquireCodePage             (IM3Runtime io_runtime);
IM3CodePage                 AcquireCodePageWithCapacity (IM3Runtime io_runtime, u32 i_lineCount);
void                        ReleaseCodePage             (IM3Runtime io_runtime, IM3CodePage i_codePage);

d_m3EndExternC

#endif // m3_env_h
