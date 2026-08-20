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


//---------------------------------------------------------------------------------------------------------------------------------

typedef struct M3MemoryInfo
{
    u32     initPages;
    u32     maxPages;
    u32     pageSize;
}
M3MemoryInfo;


typedef struct M3Memory
{
    M3MemoryHeader *        mallocated;

    u32                     numPages;
    u32                     maxPages;
    u32                     pageSize;
}
M3Memory;

typedef M3Memory *          IM3Memory;


//---------------------------------------------------------------------------------------------------------------------------------

// A table's elements are opaque pointer-sized references: IM3Function for a
// funcref table, a host handle for an externref one. NULL is the null reference.
typedef struct M3Table
{
    void **                 elements;
    u32                     size;
    u32                     maxSize;            // 0 when the module declared no maximum
    m3type_t                type;

    // Every slot starts out holding this, rather than null. A table whose
    // element type is not nullable has to say what to fill itself with.
    bytes_t                 initExpr;
    u32                     initExprSize;
}
M3Table;

// Element segment modes, from the low two bits of the segment's flags
typedef enum
{
    c_m3Elem_active     = 0,        // table 0
    c_m3Elem_passive    = 1,
    c_m3Elem_activeIdx  = 2,        // explicit table index
    c_m3Elem_declarative = 3
}
M3ElementMode;

typedef struct M3ElementSegment
{
    bytes_t                 initExpr;       // active segments only: the offset
    bytes_t                 elements;       // funcidx list, or const exprs when isExpr
    void **                 resolved;       // passive segments only, for table.init

    u32                     initExprSize;
    u32                     numElements;
    u32                     tableIndex;

    m3type_t                type;
    u8                      mode;
    bool                    isExpr;
    bool                    dropped;
}
M3ElementSegment;

typedef struct M3DataSegment
{
    const u8 *              initExpr;           // wasm code
    const u8 *              data;

    u32                     initExprSize;
    u32                     memoryRegion;
    u32                     size;

    bool                    isPassive;
    bool                    dropped;            // active segments are dropped once instantiated
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
    m3type_t                type;
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

    u32                     dataCount;          // from the data count section
    bool                    hasDataCount;

    // Bitset of the functions ref.func may name: those exported, or referenced
    // by an element segment or a global initializer. One bit per function.
    u8 *                    declaredFuncs;

    //u32                     importedGlobals;
    u32                     numGlobals;
    M3Global *              globals;

    u32                     numElementSegments;
    M3ElementSegment *      elementSegments;
    bytes_t                 elementSectionEnd;

    M3Table *               tables;
    u32                     numTables;
    const char*             table0ExportName;

    M3MemoryInfo            memoryInfo;
    M3ImportInfo            memoryImport;
    bool                    memoryImported;
    bool                    memoryDeclared;     // has a memory section entry
    const char*             memoryExportName;

    //bool                    hasWasmCodeCopy;

    struct M3Module *       next;
}
M3Module;

M3Result                    Module_AddGlobal            (IM3Module io_module, IM3Global * o_global, m3type_t i_type, bool i_mutable, bool i_isImported);
M3Result                    Module_AddTable             (IM3Module io_module, m3type_t i_type, u32 i_size, u32 i_maxSize);
M3Result                    Module_DeclareFunction      (IM3Module io_module, u32 i_index);
bool                        Module_IsFunctionDeclared   (IM3Module i_module, u32 i_index);

M3Result                    Module_PreallocFunctions    (IM3Module io_module, u32 i_totalFunctions);
M3Result                    Module_AddFunction          (IM3Module io_module, u32 i_typeIndex, IM3ImportInfo i_importInfo /* can be null */);
IM3Function                 Module_GetFunction          (IM3Module i_module, u32 i_functionIndex);

void                        Module_GenerateNames        (IM3Module i_module);

void                        FreeImportInfo              (M3ImportInfo * i_info);

//---------------------------------------------------------------------------------------------------------------------------------

typedef struct M3Environment
{
//    struct M3Runtime *      runtimes;

    IM3FuncType             funcTypes;                          // linked list of unique M3FuncType structs that can be compared using pointer-equivalence

    IM3FuncType             retFuncTypes [c_m3Type_count];      // these 'point' to elements in the linked list above.
                                                                // the number of elements must match the basic types as per M3ValueType
    u16                     numFuncTypes;                       // hands out M3FuncType.canonicalIndex
    M3CodePage *            pagesReleased;

    M3SectionHandler        customSectionHandler;
}
M3Environment;

void                        Environment_Release         (IM3Environment i_environment);

// takes ownership of io_funcType and returns a pointer to the persistent version (could be same or different)
M3Result                    Environment_AddFuncType     (IM3Environment i_environment, IM3FuncType * io_funcType);

#if d_m3HasTypedRefs
M3Result                    ParseHeapType               (IM3Module i_module, m3type_t * o_heapBits, bytes_t * io_bytes, cbytes_t i_end);
#endif
M3Result                    ParseValueType              (IM3Module i_module, m3type_t * o_type, bytes_t * io_bytes, cbytes_t i_end);

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
    void *                  originStack;
    u32                     stackSize;
    u32                     numStackSlots;
    void *                  stackLimit;     // native C-stack low-water mark; Wasm calls trap past it (NULL = unset)
    IM3Function             lastCalled;     // last function that successfully executed

    void *                  userdata;

    M3Memory                memory;
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

// Establish the native C-stack limit for a top-level invocation. The outermost
// call records a low-water mark d_m3MaxNativeStack bytes into the stack; nested
// re-entrant calls (e.g. an imported function calling back into Wasm) keep the
// original mark. op_Call/op_CallIndirect trap once execution crosses it.
#if d_m3MaxNativeStack > 0
#   define d_m3StackLimitEnter(RT)                                               \
        void * _m3SavedStackLimit = (RT)->stackLimit;                           \
        if (not (RT)->stackLimit)                                               \
            (RT)->stackLimit = (u8 *) m3_NativeStackPtr () - (d_m3MaxNativeStack);
#   define d_m3StackLimitLeave(RT)  (RT)->stackLimit = _m3SavedStackLimit;
#else
#   define d_m3StackLimitEnter(RT)
#   define d_m3StackLimitLeave(RT)
#endif

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
