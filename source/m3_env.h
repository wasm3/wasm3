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

// Page counts are u64 because a memory64 type may declare up to 2^48 pages.
// Nothing near that can be backed, but the module still has to parse and
// validate; it is instantiation that fails.
typedef struct M3MemoryInfo
{
    u64     initPages;
    u64     maxPages;
    u32     pageSize;
    bool    hasMax;         // a declared maximum of 0 is not the same as none
    bool    isMemory64;     // addressed by i64 rather than i32
}
M3MemoryInfo;


// One linear memory. Memories belong to the module that declares them, not to
// the runtime: two modules loaded into the same runtime each get their own, and
// the interpreter reaches the one it is currently running against through the
// _mem pseudo-register (see M3MemoryHeader).
//
// Every entry is individually allocated so that a slot may later be pointed at
// a memory another module owns - growing one has to be visible through every
// name it has. 'owner' says which module allocated it, and so which module
// frees it.
typedef struct M3Memory
{
    M3MemoryHeader *        mallocated;

    u64                     numPages;
    u64                     maxPages;
    u64                     initPages;
    u32                     pageSize;
    bool                    hasMax;         // see M3MemoryInfo
    bool                    isMemory64;     // addressed by i64 rather than i32

    struct M3Module *       owner;          // the module that allocated it
    M3ImportInfo            import;         // when declared as an import
    cstr_t                  exportName;     // when exported
    bool                    imported;
}
M3Memory;

typedef M3Memory *          IM3Memory;

// The value type a memory's addresses, page counts and lengths are expressed
// in. Every instruction naming the memory takes and returns that type.
static inline m3type_t  Memory_AddrType  (const M3Memory * i_memory)
{
    return i_memory->isMemory64 ? c_m3Type_i64 : c_m3Type_i32;
}


// A module that declares no custom page size means the default one. The parser
// leaves that as zero, and InitMemory only fills it in when it allocates, so a
// memory has to be asked rather than read directly until then.
static inline u32  Memory_PageSize  (const M3Memory * i_memory)
{
    return i_memory->pageSize ? i_memory->pageSize : d_m3DefaultMemPageSize;
}


//---------------------------------------------------------------------------------------------------------------------------------

// A table type as the module declared it. Sizes stay u32 - a table64 may name
// far more entries than that, but d_m3MaxSaneTableSize refuses anything near
// the limit long before it matters.
typedef struct M3TableInfo
{
    m3type_t    elemType;
    u32         initSize;
    u32         maxSize;
    bool        hasMax;         // a declared maximum of 0 is not the same as none
    bool        isTable64;      // indexed by i64 rather than i32
}
M3TableInfo;


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

    u32                     initSize;       // the declared minimum
    bool                    hasMax;         // a declared maximum of 0 is not the same as none
    bool                    isTable64;      // indexed by i64 rather than i32

    struct M3Module *       owner;          // the module that allocated it
    M3ImportInfo            import;         // when declared as an import
    cstr_t                  exportName;     // when exported
    bool                    imported;
}
M3Table;

typedef M3Table *           IM3Table;

// The value type a table's indexes and sizes are expressed in - table64 is the
// same choice as memory64, made per table.
static inline m3type_t  Table_AddrType  (const M3Table * i_table)
{
    return i_table->isTable64 ? c_m3Type_i64 : c_m3Type_i32;
}

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

    // An import linked to another module's export: the global that actually
    // holds the value. Reads and writes go there, so a mutable global stays one
    // cell seen under two names. NULL for anything else, including an import a
    // host supplied a value for through m3_LinkGlobal.
    struct M3Global *       resolved;

    union
    {
        i32 i32Value;
        i64 i64Value;
#if d_m3HasFloat
        f64 f64Value;
        f32 f32Value;
#endif
        void * refValue;
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

#if d_m3HasExceptionHandling

// An exception tag, as the tag section declares it. Tags are compared by
// identity, and this struct's address is that identity: a thrown exception
// carries the M3Tag * it was created from, and a catch clause matches when the
// pointers are equal. Tags are per-module and never merged, so a tag imported
// from another module is a fresh tag rather than an alias of the one exported
// there - wasm3 links imports against host functions, not against modules.
typedef struct M3Tag
{
    M3ImportInfo            import;
    IM3FuncType             type;           // results must be empty; params are the payload
    cstr_t                  name;           // export name, if any
    bool                    imported;
}
M3Tag;

typedef M3Tag *             IM3Tag;


// A thrown exception: the tag that identifies it plus the payload the throw
// site popped off the stack, one u64 per value (32-bit values are stored
// zero-extended, floats as their bit pattern).
//
// Exception objects belong to the runtime, not to the code that caught them.
//
// One caught by a clause that does not reify it - catch or catch_all rather
// than their _ref forms - is unreachable the moment its payload has been copied
// out, and is released there. Anything else stays on the runtime's list until
// the outermost m3_Call() returns, which is the last moment an exnref can still
// be reached from the Wasm stack. An exnref parked in a global or a table
// outlives that and is left dangling - wasm3 has no collector to say otherwise.
typedef struct M3Exception
{
    struct M3Exception *    next;           // the runtime's allocation list,
    struct M3Exception *    prev;           //   doubly linked so one can leave early
    IM3Tag                  tag;
    u32                     numArgs;
    bool                    reified;        // an exnref to this has been handed out
    u64                     args [];
}
M3Exception;


M3Exception *               NewException                (IM3Runtime io_runtime, IM3Tag i_tag, u32 i_numArgs);
void                        FreeException               (IM3Runtime io_runtime, M3Exception * i_exception);
void                        FreeExceptions              (IM3Runtime io_runtime);

#endif // d_m3HasExceptionHandling


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

#if d_m3HasExceptionHandling
    u32                     numTags;
    M3Tag *                 tags;
#endif

    u32                     numElementSegments;
    M3ElementSegment *      elementSegments;
    bytes_t                 elementSectionEnd;

    // The module's table index space, imported entries first. Entries are
    // borrowed pointers, individually allocated so a slot can be pointed at a
    // table another module owns - see M3Table.owner, and M3Module.memories,
    // which works the same way.
    IM3Table *              tables;
    u32                     numTables;

    // The module's memory index space: imported entries first, then declared
    // ones, exactly as the Wasm index space orders them. Entries are borrowed
    // pointers - see M3Memory.owner.
    IM3Memory *             memories;
    u32                     numMemories;

    // The interpreter always carries a valid memory header in _mem - the
    // stack-limit check, the backtrace recorder and the call ops all reach the
    // runtime through it. A module that declares no memory still needs one, so
    // it gets this zero-length stand-in rather than a NULL _mem.
    M3Memory                emptyMemory;

    // memories[0], or emptyMemory when there are none. Resolved once by
    // InitMemory - the index space is fixed by then and linking has already
    // repointed whatever it was going to - so the call ops can read it straight
    // instead of branching. NULL until the module is loaded.
    IM3Memory               memory0;

    //bool                    hasWasmCodeCopy;

    struct M3Module *       next;
}
M3Module;

M3Result                    Module_AddMemory            (IM3Module io_module, IM3Memory * o_memory, const M3MemoryInfo * i_info, bool i_isImported);

// Memory 0 of a module - the one the interpreter's _mem register tracks while
// that module's code is running. Never NULL once the module has been loaded.
static inline IM3Memory  Module_Memory0  (IM3Module i_module)
{
    return i_module->memory0;
}

static inline M3MemoryHeader *  Module_MemoryHeader  (IM3Module i_module)
{
    return i_module->memory0->mallocated;
}

M3Result                    Module_AddGlobal            (IM3Module io_module, IM3Global * o_global, m3type_t i_type, bool i_mutable, bool i_isImported);
M3Result                    Module_AddTable             (IM3Module io_module, IM3Table * o_table, const M3TableInfo * i_info, bool i_isImported);
#if d_m3HasExceptionHandling
M3Result                    Module_AddTag               (IM3Module io_module, IM3Tag * o_tag, IM3FuncType i_type, bool i_isImported);
#endif
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

    u32                     memoryLimit;

#if d_m3EnableStrace >= 2
    u32                     callDepth;
#endif

#if d_m3HasExceptionHandling
    u32                     tryDepth;           // number of try regions whose body is executing
    M3Exception *           pendingException;   // the exception currently unwinding, if any
    M3Exception *           exceptions;         // the ones it still holds
    u32                     exceptionNesting;   // RunCodeChecked() recursion depth
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

M3Result                    ResizeMemory                (IM3Runtime io_runtime, IM3Memory io_memory, u64 i_numPages);

typedef void *              (* ModuleVisitor)           (IM3Module i_module, void * i_info);
void *                      ForEachModule               (IM3Runtime i_runtime, ModuleVisitor i_visitor, void * i_info);

void *                      v_FindFunction              (IM3Module i_module, void * i_info);

IM3CodePage                 AcquireCodePage             (IM3Runtime io_runtime);
IM3CodePage                 AcquireCodePageWithCapacity (IM3Runtime io_runtime, u32 i_lineCount);
void                        ReleaseCodePage             (IM3Runtime io_runtime, IM3CodePage i_codePage);

d_m3EndExternC

#endif // m3_env_h
